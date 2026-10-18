// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/dbus/matchrule.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/misc.h>
#include <pwd.h>
#include <unistd.h>
#include <array>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>

// Sogou NG publishes its final, transformed candidates through CPIS keyflow,
// rather than Fcitx InputPanel. Do not use the raw engine's candidate indices.
class SogouPanel {
public:
 static constexpr const char *inputMethod="com.sogou.ime.ng.fcitx5.deepin";
 struct Data {std::string composition;std::vector<std::string> words;bool prev=false,next=false;};
 SogouPanel(fcitx::dbus::Bus &bus,fcitx::EventLoop &loop,std::function<void()> changed):bus_(bus),loop_(loop),changed_(std::move(changed)){
  if(auto *user=getpwuid(getuid()))uid_=std::string(user->pw_name)+"#im.module=fcitx5|im.name="+inputMethod;
  signal_=bus_.addMatch(fcitx::dbus::MatchRule("com.cpis.panel","/com/cpis/panel","com.cpis.panel"),[this](fcitx::dbus::Message &m){
   if(!active_||m.signature()!="sayu")return true;
   std::string uid;std::vector<uint8_t> bytes;uint32_t size;m>>uid>>bytes>>size;
   if(!m||uid!=uid_||size!=bytes.size()||bytes.size()>4096)return true;
   const auto name=m.member();
   if(name=="UpdateUi"||name=="Preedit"||name=="Commit"){invalidate();}
   if(name=="ShowWindow"&&ready_){std::string window(bytes.begin(),bytes.end());if(!window.empty()&&window.back()=='\0')window.pop_back();if(window=="composition")hide();}
   return true;
  });
  owner_=bus_.addMatch(fcitx::dbus::MatchRule("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","NameOwnerChanged",{"com.cpis.panel"}),[this](fcitx::dbus::Message &m){
   std::string name,oldOwner,newOwner;m>>name>>oldOwner>>newOwner;if(!active_)return true;
   ++generation_;queries_.clear();defer_.reset();inFlight_=false;ready_=false;hidden_=false;action_=false;data_={};changed_();if(!newOwner.empty())schedule();return true;
  });
 }
 ~SogouPanel(){setActive(false,0,true);}
 void setActive(bool active,uint64_t epoch,bool restore){
  active=active&&!uid_.empty();
  if(active==active_&&(!active||epoch==epoch_))return;
  if(active_&&hidden_&&restore&&(!data_.composition.empty()||publishedRevision_!=revision_))command("Show","composition");
  ++generation_;active_=active;epoch_=epoch;ready_=false;hidden_=false;inFlight_=false;action_=false;data_={};queries_.clear();actionSlot_.reset();defer_.reset();mismatches_=0;
  if(active_)schedule();
 }
 bool ready()const{return active_&&ready_;}
 const Data &data()const{return data_;}
 bool selectable()const{return ready()&&publishedRevision_==revision_&&!action_;}
 void invalidate(){if(!active_)return;++revision_;mismatches_=0;schedule();}
 bool select(int index,std::function<void()> commit){
  if(!selectable()||index<0||size_t(index)>=data_.words.size())return false;
  action_=true;const auto gen=generation_,rev=revision_;
  auto m=method("RewriteEngineStat");m<<uid_<<std::string("result_candidate_focus")<<std::to_string(index)<<false;
  actionSlot_=m.callAsync(300000,[this,gen,rev,commit=std::move(commit)](fcitx::dbus::Message &r){
   if(gen!=generation_)return true;
   action_=false;int32_t code=-1;if(!r.isError()&&r.signature()=="i")r>>code;
   if(r&&code==0&&active_&&rev==revision_)commit();
   schedule();return true;
  });return true;
 }
 void schedule(){
  if(!active_||defer_)return;
  defer_=loop_.addDeferEvent([this](fcitx::EventSource *){defer_.reset();refresh();return true;});
 }
private:
 fcitx::dbus::Message method(const char *name){return bus_.createMethodCall("com.cpis.panel","/com/cpis/panel","com.cpis.panel",name);}
 void command(const char *name,const std::string &window){auto m=method(name);m<<uid_<<window;m.send();}
 void hide(){command("Hide","composition");hidden_=true;}
 void refresh(){
  if(!active_||inFlight_)return;
  inFlight_=true;queries_.clear();const auto gen=generation_,rev=revision_;
  struct Batch {int remaining=5;bool good=true;std::array<std::string,5> values;};auto batch=std::make_shared<Batch>();
  constexpr std::array<const char *,5> keys={"result_composition","result_candidate_result","result_prevPageExists","result_nextPageExists","result_composition"};
  for(size_t n=0;n<keys.size();++n){auto m=method("AcquireEngineStat");m<<uid_<<std::string(keys[n]);
   queries_.push_back(m.callAsync(300000,[this,batch,n,gen,rev](fcitx::dbus::Message &r){
    if(gen!=generation_)return true;
    int32_t code=-1;if(!r.isError()&&r.signature()=="si")r>>batch->values[n]>>code;
    batch->good&=r&&!r.isError()&&code==0&&batch->values[n].size()<=65536;
    if(--batch->remaining)return true;
    inFlight_=false;
    if(rev!=revision_){schedule();return true;}
    if(batch->good&&batch->values[0]!=batch->values[4]){if(++mismatches_<3){schedule();return true;}batch->good=false;}
    Data next;
    if(batch->good&&(batch->values[2]=="true"||batch->values[2]=="false")&&(batch->values[3]=="true"||batch->values[3]=="false")){
     next.composition=batch->values[0];std::istringstream stream(batch->values[1]);std::string word;
     while(std::getline(stream,word)){if(word.empty()||next.words.size()==128){batch->good=false;break;}next.words.push_back(word);}
     next.prev=batch->values[2]=="true";next.next=batch->values[3]=="true";
    }else batch->good=false;
    if(!batch->good){if(hidden_)command("Show","composition");ready_=false;hidden_=false;data_={};changed_();return true;}
    const bool changed=!ready_||next.composition!=data_.composition||next.words!=data_.words||next.prev!=data_.prev||next.next!=data_.next;
    ready_=true;publishedRevision_=rev;mismatches_=0;data_=std::move(next);hide();if(changed)changed_();return true;
   }));
  }
 }
 fcitx::dbus::Bus &bus_;fcitx::EventLoop &loop_;std::function<void()> changed_;std::string uid_;Data data_;
 bool active_=false,ready_=false,hidden_=false,inFlight_=false,action_=false;uint64_t generation_=0,epoch_=0,revision_=0,publishedRevision_=0;unsigned mismatches_=0;
 std::unique_ptr<fcitx::dbus::Slot> signal_,owner_,actionSlot_;std::vector<std::unique_ptr<fcitx::dbus::Slot>> queries_;std::unique_ptr<fcitx::EventSource> defer_;
};
