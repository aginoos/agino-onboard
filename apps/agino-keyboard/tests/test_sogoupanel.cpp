// SPDX-License-Identifier: GPL-3.0-or-later
#include "../addon/sogoupanel.h"
#include <fcitx-utils/dbus/objectvtable.h>
#include <fcitx-utils/eventloopinterface.h>
#include <iostream>
#include <stdexcept>
using namespace fcitx;
void require(bool value,const char *what){if(!value)throw std::runtime_error(what);}
struct Panel:dbus::ObjectVTable<Panel>{
 std::string uid;std::map<std::string,std::string> values{{"result_composition","ni'hao"},{"result_candidate_result","你好\n你号"},{"result_prevPageExists","false"},{"result_nextPageExists","true"}};
 bool delayed=false,failFirst=false,inconsistent=false;int reads=0,compositions=0,rewrites=0;std::vector<std::pair<dbus::Message,std::string>> pending;std::vector<std::string> hidden,shown;
 Panel(){uid=std::string(getpwuid(getuid())->pw_name)+"#im.module=fcitx5|im.name="+SogouPanel::inputMethod;}
 void reply(dbus::Message &m,const std::string &key){++reads;if(key=="result_composition")++compositions;if(failFirst&&key=="result_composition"&&compositions%2==1){auto r=m.createError("org.cpis.TestFailure","query failed");r.send();return;}auto r=m.createReply();r<<(inconsistent&&key=="result_composition"?std::to_string(compositions):values[key])<<int32_t(0);r.send();}
 dbus::ObjectVTableMethod acquire{this,"AcquireEngineStat","ss","si",[this](dbus::Message m){std::string id,key;m>>id>>key;require(id==uid,"CPIS context UID");if(delayed)pending.emplace_back(std::move(m),key);else reply(m,key);return true;}};
 dbus::ObjectVTableMethod rewrite{this,"RewriteEngineStat","sssb","i",[this](dbus::Message m){++rewrites;auto r=m.createReply();r<<int32_t(0);r.send();return true;}};
 int32_t hide(const std::string &,const std::string &window){hidden.push_back(window);return 0;}
 int32_t show(const std::string &,const std::string &window){shown.push_back(window);return 0;}
 FCITX_OBJECT_VTABLE_METHOD(hide,"Hide","ss","i");FCITX_OBJECT_VTABLE_METHOD(show,"Show","ss","i");
 FCITX_OBJECT_VTABLE_SIGNAL(update,"UpdateUi","sayu");
 void notify(){update(uid,std::vector<uint8_t>{},uint32_t(0));}
 void release(){delayed=false;auto messages=std::move(pending);pending.clear();for(auto &[m,key]:messages)reply(m,key);}
};
int main(){try{
 EventLoop loop;dbus::Bus server(dbus::BusType::Session),client(dbus::BusType::Session);server.attachEventLoop(&loop);client.attachEventLoop(&loop);server.requestName("com.cpis.panel",dbus::RequestNameFlag::None);Panel fixture;server.addObjectVTable("/com/cpis/panel","com.cpis.panel",fixture);
 int commits=0,reads=0;auto adapter=std::make_unique<SogouPanel>(client,loop,[]{});adapter->setActive(true,1,false);
 std::vector<std::pair<const char *,std::function<bool()>>> steps{
 {"initial publication",[&]{if(!adapter->ready())return false;fixture.values["result_candidate_result"]="新词\n第二词";fixture.delayed=true;fixture.notify();return true;}},
 {"dirty revision",[&]{if(fixture.pending.size()!=5)return false;require(!adapter->select(0,[&]{++commits;}),"dirty revision must reject old index");fixture.release();return true;}},
 {"new publication",[&]{if(adapter->data().words.front()!="新词")return false;require(adapter->select(1,[&]{++commits;}),"current selection");return true;}},
 {"selection",[&]{if(commits!=1)return false;require(fixture.rewrites==1,"only one valid focus rewrite");require(fixture.hidden.front()=="composition","hide correct logical composition window");fixture.values["result_composition"]="";fixture.values["result_candidate_result"]="";fixture.notify();return true;}},
 {"empty publication",[&]{if(!adapter->data().composition.empty())return false;fixture.failFirst=true;fixture.values["result_composition"]="n";fixture.notify();return true;}},
 {"partial query failure",[&]{if(adapter->ready()||fixture.shown.empty())return false;reads=fixture.reads;return true;}},
 {"no busy retry",[&]{require(fixture.reads==reads,"failed batch must not busy retry");fixture.failFirst=false;fixture.notify();return true;}},
 {"recovery",[&]{if(!adapter->ready())return false;fixture.inconsistent=true;fixture.notify();return true;}},
 {"bounded inconsistent retry",[&]{if(adapter->ready())return false;require(fixture.reads<reads+30,"bounded traffic");fixture.inconsistent=false;fixture.notify();return true;}},
 {"recovery and teardown",[&]{if(!adapter->ready())return false;fixture.shown.clear();adapter.reset();return true;}},
 {"teardown restored popup",[&]{if(fixture.shown.empty())return false;require(fixture.shown.back()=="composition","restore correct popup");return true;}}
 };
 size_t step=0;std::string error;auto deadline=now(CLOCK_MONOTONIC)+1000000;
 auto timer=loop.addTimeEvent(CLOCK_MONOTONIC,now(CLOCK_MONOTONIC)+1000,0,[&](EventSourceTime *t,uint64_t){try{if(steps[step].second()){if(++step==steps.size()){loop.exit();return true;}deadline=now(CLOCK_MONOTONIC)+1000000;}else if(now(CLOCK_MONOTONIC)>=deadline)throw std::runtime_error(steps[step].first);t->setNextInterval(step==6?30000:1000);t->setOneShot();}catch(const std::exception &e){error=e.what();loop.exit();}return true;});
 loop.exec();require(error.empty(),error.c_str());require(step==steps.size(),"incomplete scenario");std::cout<<"Sogou wire tests passed: context, dirty selection, failure, bounded retry, recovery, teardown\n";return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
