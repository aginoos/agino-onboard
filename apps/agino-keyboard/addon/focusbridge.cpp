// SPDX-License-Identifier: GPL-3.0-or-later
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/focusgroup.h>
#include <fcitx/userinterfacemanager.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/instance.h>
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/dbus/objectvtable.h>
#include <fcitx-utils/dbus/matchrule.h>
#include <fcitx-utils/log.h>
#include <unistd.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <random>
#include <unordered_set>
#include "sogoupanel.h"
using namespace fcitx;
using Snapshot=std::tuple<uint64_t,uint64_t,bool,bool,std::string,std::string,std::vector<std::string>,bool,bool,std::string,int32_t,int32_t>;
// An empty, unfocused context clears an existing popup without changing the
// active composition or sending empty preedit text to the target application.
class PopupClearContext final:public InputContext {
public:
 PopupClearContext(Instance *instance,InputContext *target):InputContext(instance->inputContextManager(),"agino-keyboard-popup-clear"),group_(target->display(),instance->inputContextManager()),frontend_(target->frontend()){setFocusGroup(&group_);created();}
 ~PopupClearContext()override{destroy();}
 const char *frontend()const override{return frontend_.c_str();}
private:
 void commitStringImpl(const std::string &)override{}
 void deleteSurroundingTextImpl(int,unsigned int)override{}
 void forwardKeyImpl(const ForwardKeyEvent &)override{}
 void updatePreeditImpl()override{}
 FocusGroup group_;std::string frontend_;
};
class FocusBridge:public AddonInstance,public dbus::ObjectVTable<FocusBridge> {
public:
 explicit FocusBridge(Instance *instance):instance_(instance),bus_(dbus::BusType::Session){
  testHooks_=std::getenv("AGINO_TEST_HOOKS")!=nullptr;
  std::error_code ec;for(const char *path:{AGINO_TRUSTED_CONTROLLER,AGINO_TRUSTED_UI})allowed_.push_back(std::filesystem::weakly_canonical(path,ec).string());
  sogou_=std::make_unique<SogouPanel>(bus_,instance_->eventLoop(),[this]{sogouChanged();});
  epoch_=(uint64_t(std::random_device{}()&0xfffff)<<32);bus_.attachEventLoop(&instance_->eventLoop());
  // During fcitx5 -r the old addon owns this name until it exits.
  // Queue the new connection instead of failing addon initialization.
  bus_.requestName("org.agino.Keyboard1",dbus::RequestNameFlag::Queue);
  bus_.addObjectVTable("/org/agino/Keyboard1","org.agino.Keyboard1",*this);
  ownerWatch_=bus_.addMatch(dbus::MatchRule("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","NameOwnerChanged"),[this](dbus::Message &message){
   std::string name,oldOwner,newOwner;message>>name>>oldOwner>>newOwner;
   if(!name.empty()&&name.front()==':'&&newOwner.empty())releaseSender(name);
   return true;
  });
  auto watch=[this](EventType t,EventWatcherPhase phase,auto fn){handlers_.push_back(instance_->watchEvent(t,phase,fn));};
  watch(EventType::InputContextFocusIn,EventWatcherPhase::PostInputMethod,[this](Event &e){restorePanel(false);releaseAll();focusRef_=suspended_?TrackableObjectReference<InputContext>{}:static_cast<InputContextEvent &>(e).inputContext()->watch();++epoch_;++candidate_;limit_=128;if(!panelSender_.empty())capturePanel(true);notify();});
  for(auto t:{EventType::InputContextFocusOut,EventType::InputContextDestroyed})
   watch(t,EventWatcherPhase::PreInputMethod,[this](Event &e){if(focus()!=static_cast<InputContextEvent &>(e).inputContext())return;restorePanel(false);releaseAll();focusRef_.unwatch();lastList_.reset();lastPreedit_.clear();lastWords_.clear();lastTexts_.clear();++epoch_;++candidate_;notify();});
  watch(EventType::InputContextCapabilityChanged,EventWatcherPhase::PostInputMethod,[this](Event &e){if(focus()==static_cast<InputContextEvent &>(e).inputContext()){if(editable()!=lastEditable_||sensitive()!=lastSensitive_){releaseAll();++epoch_;++candidate_;}notify();}});
  watch(EventType::InputContextReset,EventWatcherPhase::PostInputMethod,[this](Event &e){if(focus()==static_cast<InputContextEvent &>(e).inputContext()){releaseAll();++epoch_;++candidate_;notify();}});
  watch(EventType::InputContextUpdateUI,EventWatcherPhase::PostInputMethod,[this](Event &e){if(focus()==static_cast<InputContextEvent &>(e).inputContext()){refreshCandidates();notify();}});
  watch(EventType::InputContextFlushUI,EventWatcherPhase::PreInputMethod,[this](Event &e){if(!panelSender_.empty()&&focus()==static_cast<InputContextEvent &>(e).inputContext())capturePanel(false);});
  watch(EventType::InputContextKeyEvent,EventWatcherPhase::PreInputMethod,[this](Event &e){if(focus()==static_cast<KeyEvent &>(e).inputContext())sogou_->invalidate();if(!static_cast<KeyEvent &>(e).isVirtual())physical();});
  for(auto t:{EventType::InputContextInputMethodActivated,EventType::InputContextInputMethodDeactivated,EventType::InputMethodGroupChanged})
   watch(t,EventWatcherPhase::PostInputMethod,[this](Event &){++candidate_;notify();});
 }
 ~FocusBridge() override {handlers_.clear();panelSender_.clear();sogou_->setActive(false,0,editable()&&!sensitive()&&instance_->currentInputMethod()==SogouPanel::inputMethod);restorePanel(false);releaseAll();bus_.flush();}
 InputContext *focus()const{return focusRef_.get();}
 bool editable()const{return !suspended_&&focus()&&focus()->hasFocus()&&!focus()->capabilityFlags().test(CapabilityFlag::NoOnScreenKeyboard);}
 bool sensitive()const{return safe_||(focus()&&focus()->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive));}
 Snapshot getSnapshot(){
  const bool useSogou=!panelSender_.empty()&&editable()&&!sensitive()&&instance_->currentInputMethod()==SogouPanel::inputMethod;
  const bool hadSogou=sogou_->ready();sogou_->setActive(useSogou,epoch_,editable()&&!sensitive()&&instance_->currentInputMethod()==SogouPanel::inputMethod);
  if(hadSogou&&!useSogou){restorePanel(false);if(!panelSender_.empty())capturePanel(false);}
  std::vector<std::string> words; std::string preedit;bool prev=false,next=false;
  if(editable()&&!sensitive()){
   preedit=instance_->outputFilter(focus(),focus()->inputPanel().preedit()).toString();
   if(preedit.empty())preedit=instance_->outputFilter(focus(),focus()->inputPanel().clientPreedit()).toString();
   auto list=focus()->inputPanel().candidateList();
   if(list){auto bulk=list->toBulk();int total=bulk?bulk->totalSize():list->size();
    for(int n=0;n<(total<0?limit_:std::min(total,limit_));++n){try{const auto &word=bulk?bulk->candidateFromAll(n):list->candidate(n);words.push_back(instance_->outputFilter(focus(),word.text()).toString());}catch(...){break;}}
    if(bulk){next=total>int(words.size());if(total<0&&int(words.size())==limit_){try{bulk->candidateFromAll(limit_);next=true;}catch(...){next=false;}}if(limit_>=1024)next=false;}else if(auto pages=list->toPageable()){prev=pages->hasPrev();next=pages->hasNext();}
   }
  }
  if(sogou_->ready()){const auto &s=sogou_->data();words=s.words;preedit=s.composition;prev=s.prev;next=s.next;}
  auto rect=focus()?focus()->cursorRect():Rect{};
  return {epoch_,candidate_,editable(),sensitive(),preedit,editable()?instance_->currentInputMethod():"",words,prev,next,sensitive()?"sensitive":"unknown",rect.left(),rect.top()};
 }
 void refreshCandidates(){
  auto list=editable()?focus()->inputPanel().candidateList():nullptr;
  auto preedit=editable()?focus()->inputPanel().clientPreedit().toString():std::string();
  std::vector<const CandidateWord*> identities;std::vector<std::string> texts;
  if(list)for(int n=0;n<list->size();++n){try{const auto &w=list->candidate(n);identities.push_back(&w);texts.push_back(w.text().toString());}catch(...){break;}}
  if(list!=lastList_||preedit!=lastPreedit_||identities!=lastWords_||texts!=lastTexts_){++candidate_;limit_=128;}
  lastList_=list;lastPreedit_=preedit;lastWords_=identities;lastTexts_=texts;
 }
 void notify(){const auto s=getSnapshot();lastEditable_=std::get<2>(s);lastSensitive_=std::get<3>(s);changed(std::get<0>(s),std::get<1>(s),std::get<2>(s),std::get<3>(s),std::get<4>(s),std::get<5>(s),std::get<6>(s),std::get<7>(s),std::get<8>(s),std::get<9>(s),std::get<10>(s),std::get<11>(s));}
 // Caller authentication: state-changing methods accept only the keyboard programs of the same user (the bus daemon vouches for uid and pid).
 bool trustedUid(const std::string &sender){return busCredential("GetConnectionUnixUser",sender)==getuid();}
 uint64_t busCredential(const char *member,const std::string &sender){
  auto msg=bus_.createMethodCall("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus",member);msg<<sender;
  auto reply=msg.call(500000);uint32_t value=0;
  return reply&&!reply.isError()&&(reply>>value)?uint64_t(value):~uint64_t(0);
 }
 bool trustedExe(uint64_t pid){
  std::error_code ec;auto path=std::filesystem::read_symlink("/proc/"+std::to_string(pid)+"/exe",ec).string();
  // After a package upgrade the running program's file is replaced, and the kernel appends this suffix.
  static const std::string gone=" (deleted)";
  if(ec||path.empty())return false;
  if(path.size()>gone.size()&&path.compare(path.size()-gone.size(),gone.size(),gone)==0)path.resize(path.size()-gone.size());
  path=std::filesystem::weakly_canonical(path,ec).string();
  return !ec&&std::find(allowed_.begin(),allowed_.end(),path)!=allowed_.end();
 }
 bool trusted(){
  const auto sender=currentMessage()?currentMessage()->sender():std::string();
  if(sender.empty())return false;
  if(trusted_.count(sender))return true;
  const auto pid=busCredential("GetConnectionUnixProcessID",sender);
  if(trustedUid(sender)&&(testHooks_||(pid!=~uint64_t(0)&&trustedExe(pid)))){trusted_.insert(sender);return true;}
  if(denied_.insert(sender).second)FCITX_WARN()<<"agino-keyboard: rejected a call from untrusted bus name "<<sender<<" (pid "<<pid<<")";
  return false;
 }
 bool sendKey(uint32_t sym,uint32_t code,uint32_t mods,bool release,uint64_t epoch){
  if(!trusted()||epoch!=epoch_||!editable())return false;
  // A key of the keyboard: a symbol (X11 or Unicode up to U+10FFFF without the surrogates), a keycode of the X11 range, the modifier bits of X11.
  if(sym==0||sym>0x0110ffff||(sym>=0x0100d800&&sym<=0x0100dfff)||code>255)return false;
  mods&=0x1fff;
  const auto sender=currentMessage()?currentMessage()->sender():std::string();
  if(sender.empty())return false;
  Key key(static_cast<KeySym>(sym),KeyStates(mods)|KeyState::Virtual,code);
  if(release){auto it=held_.find(sym);if(it==held_.end()||it->second.sender!=sender)return false;auto held=it->second;auto context=focus()->watch();held_.erase(it);if(held.engine&&context.get()){KeyEvent event(context.get(),held.key,true);context.get()->keyEvent(event);}if(held.forwarded&&context.get())context.get()->forwardKey(held.key,true);return true;}
  if(held_.count(sym)||held_.size()>=16)return false;
  held_.emplace(sym,HeldKey{key,false,!sensitive(),sender});
  // Feed the engine first. An unconsumed printable character is text, while
  // shortcuts/control keys retain their forwarded press/release pair.
  bool consumed=false;if(!sensitive()){KeyEvent event(focus(),key,false);consumed=focus()->keyEvent(event);}
  auto it=held_.find(sym);if(it==held_.end()||epoch!=epoch_||!editable())return false;
  if(!consumed){auto unicode=Key::keySymToUnicode(key.sym());if(unicode>=32&&unicode!=127&&!key.states().testAny(KeyStates{KeyState::Ctrl,KeyState::Alt,KeyState::Super})){focus()->commitString(Key::keySymToUTF8(key.sym()));}else{it->second.forwarded=true;focus()->forwardKey(key,false);}}
  return true;
 }
 bool selectCandidate(int32_t index,uint64_t epoch,uint64_t candidate){
  if(!trusted()||epoch!=epoch_||candidate!=candidate_||!editable()||sensitive()||index<0||index>=limit_)return false;
  if(sogou_->ready()){const auto context=focus()->watch();return sogou_->select(index,[this,context,epoch]{if(epoch!=epoch_||!editable()||sensitive()||context.get()!=focus())return;Key key(FcitxKey_space,KeyState::Virtual);KeyEvent press(focus(),key,false);focus()->keyEvent(press);if(context.get()){KeyEvent release(context.get(),key,true);context.get()->keyEvent(release);}});}
  auto list=focus()->inputPanel().candidateList();if(!list)return false;
  try{const auto &word=list->toBulk()?list->toBulk()->candidateFromAll(index):list->candidate(index);if(word.isPlaceHolder())return false;word.select(focus());return true;}catch(...){return false;}
 }
 bool requestPage(bool next,uint64_t epoch,uint64_t candidate){
  if(!trusted()||epoch!=epoch_||candidate!=candidate_||!editable()||sensitive())return false;
  if(sogou_->ready()){if(!sogou_->selectable())return false;if(next?!sogou_->data().next:!sogou_->data().prev)return false;Key key(next?FcitxKey_Next:FcitxKey_Prior,KeyState::Virtual);auto context=focus()->watch();KeyEvent press(focus(),key,false);focus()->keyEvent(press);if(context.get()){KeyEvent release(context.get(),key,true);context.get()->keyEvent(release);}return true;}
  auto list=focus()->inputPanel().candidateList();if(!list)return false;
  if(list->toBulk()){if(!next||limit_>=1024)return false;limit_+=128;notify();return true;}
  auto pages=list->toPageable();if(!pages||(next?!pages->hasNext():!pages->hasPrev()))return false;
  next?pages->next():pages->prev();focus()->updateUserInterface(UserInterfaceComponent::InputPanel);return true;
 }
 std::vector<std::string> inputMethods(){std::vector<std::string> methods;for(const auto &im:instance_->inputMethodManager().currentGroup().inputMethodList())methods.push_back(im.name());return methods;}
 bool setInputMethod(const std::string &name,uint64_t epoch){if(!trusted()||!editable()||epoch!=epoch_||sensitive())return false;auto methods=inputMethods();if(std::find(methods.begin(),methods.end(),name)==methods.end())return false;releaseAll();instance_->setCurrentInputMethod(name);notify();return true;}
 void setSafeInput(bool value){if(!trusted()||safe_==value)return;releaseAll();safe_=value;restorePanel(false);if(focus())focus()->reset();++epoch_;++candidate_;notify();}
 void suspend(bool v){if(!trusted()||suspended_==v)return;releaseAll();if(focus())focus()->reset();restorePanel(false);suspended_=v;focusRef_.unwatch();if(!v){auto *current=instance_->lastFocusedInputContext();if(current&&current->hasFocus())focusRef_=current->watch();}if(!panelSender_.empty())capturePanel(true);lastList_.reset();lastPreedit_.clear();lastWords_.clear();lastTexts_.clear();++epoch_;++candidate_;notify();}
 void setPanelVisible(bool visible){
  const auto sender=currentMessage()?currentMessage()->sender():std::string();
  if(sender.empty()||!trusted())return;
  if(visible){if(!panelSender_.empty()&&panelSender_!=sender)return;panelSender_=sender;capturePanel(true);}
  else if(panelSender_==sender){panelSender_.clear();restorePanel(true);}
 }
 void releaseSender(const std::string &sender){
  trusted_.erase(sender);denied_.erase(sender);
  if(panelSender_==sender){panelSender_.clear();restorePanel(true);}
  decltype(held_) lost;
  for(auto it=held_.begin();it!=held_.end();){if(it->second.sender==sender){lost.emplace(it->first,it->second);it=held_.erase(it);}else ++it;}
  if(!focus())return;auto context=focus()->watch();
  for(const auto &[sym,pair]:lost){if(pair.engine&&context.get()){KeyEvent event(context.get(),pair.key,true);context.get()->keyEvent(event);}if(pair.forwarded&&context.get())context.get()->forwardKey(pair.key,true);}
 }
 void releaseAll(){auto held=std::move(held_);held_.clear();if(!focus())return;auto context=focus()->watch();for(const auto &[sym,pair]:held){if(pair.engine&&context.get()){KeyEvent event(context.get(),pair.key,true);context.get()->keyEvent(event);}if(pair.forwarded&&context.get())context.get()->forwardKey(pair.key,true);}}
private:
 void sogouChanged(){
  ++candidate_;
  if(sogou_->ready()&&editable()&&!sensitive()&&!panelSender_.empty()&&focus()->capabilityFlags().test(CapabilityFlag::Preedit)){
   capturePanel(false);auto &panel=focus()->inputPanel();if(!preeditOverridden_||panel.clientPreedit().toString()!=overriddenPreedit_.toString())previousClientPreedit_=panel.clientPreedit();
   overriddenPreedit_=Text(sogou_->data().composition);overriddenPreedit_.setCursor(sogou_->data().composition.size());preeditOverridden_=true;panel.setClientPreedit(overriddenPreedit_);focus()->updatePreedit();
  }else if(preeditOverridden_)restorePanel(false);
  notify();
 }
 struct PanelCallback {void operator()(InputContext *)const{}};
 void capturePanel(bool clearPopup){
  auto *ic=focus();if(!ic||!ic->hasFocus())return;
  if(panelContext_.get()!=ic){restorePanel(false);panelContext_=ic->watch();}
  auto &panel=ic->inputPanel();
  if(!panel.customVirtualKeyboardCallback().target<PanelCallback>()){
   previousPanelCallback_=panel.customVirtualKeyboardCallback();
   panel.setCustomVirtualKeyboardCallback(PanelCallback{});
  }
  // Some engines put the composing alphabet only in the input-panel preedit
  // (for example Pinyin's "Do not show" or "Commit preview" setting).
  // During the panel lease route that text through the normal client preedit,
  // without committing it or changing the engine/user configuration.
  if(!sensitive()&&ic->capabilityFlags().test(CapabilityFlag::Preedit)&&!panel.preedit().empty()){
   if(!preeditOverridden_||panel.clientPreedit().toString()!=overriddenPreedit_.toString())previousClientPreedit_=panel.clientPreedit();
   overriddenPreedit_=panel.preedit();preeditOverridden_=true;
   panel.setClientPreedit(overriddenPreedit_);ic->updatePreedit();
  }else if(preeditOverridden_&&panel.clientPreedit().toString()!=overriddenPreedit_.toString())preeditOverridden_=false;
  if(clearPopup){
   auto &manager=instance_->userInterfaceManager();
   auto *ui=dynamic_cast<UserInterface *>(instance_->addonManager().addon(manager.currentUI(),false));
   if(ui){PopupClearContext empty(instance_,ic);ui->update(UserInterfaceComponent::InputPanel,&empty);}
  }
 }
 void restorePanel(bool update){
  auto *ic=panelContext_.get();
  if(ic&&preeditOverridden_&&ic->inputPanel().clientPreedit().toString()==overriddenPreedit_.toString()){
   ic->inputPanel().setClientPreedit(previousClientPreedit_);ic->updatePreedit();
  }
  preeditOverridden_=false;previousClientPreedit_.clear();overriddenPreedit_.clear();
  if(ic&&ic->inputPanel().customVirtualKeyboardCallback().target<PanelCallback>())ic->inputPanel().setCustomVirtualKeyboardCallback(std::move(previousPanelCallback_));
  previousPanelCallback_={};panelContext_.unwatch();
  if(update&&ic&&ic->hasFocus())ic->updateUserInterface(UserInterfaceComponent::InputPanel,true);
 }
 bool preeditOverridden_=false;Text previousClientPreedit_,overriddenPreedit_;
 std::string panelSender_;TrackableObjectReference<InputContext> panelContext_;CustomInputPanelCallback previousPanelCallback_;
 FCITX_OBJECT_VTABLE_METHOD(setPanelVisible,"SetPanelVisible","b","");
 FCITX_OBJECT_VTABLE_METHOD(suspend,"Suspend","b","");
 FCITX_OBJECT_VTABLE_METHOD(getSnapshot,"GetSnapshot","","ttbbssasbbsii");
 FCITX_OBJECT_VTABLE_METHOD(sendKey,"SendKey","uuubt","b");
 FCITX_OBJECT_VTABLE_METHOD(selectCandidate,"SelectCandidate","itt","b");
 FCITX_OBJECT_VTABLE_METHOD(requestPage,"RequestPage","btt","b");
 FCITX_OBJECT_VTABLE_METHOD(inputMethods,"InputMethods","","as");
 FCITX_OBJECT_VTABLE_METHOD(setInputMethod,"SetInputMethod","st","b");
 FCITX_OBJECT_VTABLE_METHOD(setSafeInput,"SetSafeInput","b","");
 FCITX_OBJECT_VTABLE_SIGNAL(changed,"SnapshotChanged","ttbbssasbbsii");
 FCITX_OBJECT_VTABLE_SIGNAL(physical,"PhysicalKey","");
 std::shared_ptr<CandidateList> lastList_;std::string lastPreedit_;std::vector<const CandidateWord*> lastWords_;std::vector<std::string> lastTexts_;
 Instance *instance_;dbus::Bus bus_;TrackableObjectReference<InputContext> focusRef_;uint64_t epoch_=1,candidate_=1;bool safe_=false,suspended_=false,lastEditable_=false,lastSensitive_=false;int limit_=128;struct HeldKey {Key key;bool forwarded;bool engine;std::string sender;};std::map<uint32_t,HeldKey> held_;
 std::unique_ptr<dbus::Slot> ownerWatch_;
 std::unordered_set<std::string> trusted_,denied_;std::vector<std::string> allowed_;bool testHooks_=false;
 std::unique_ptr<SogouPanel> sogou_;
 std::vector<std::unique_ptr<HandlerTableEntry<EventHandler>>> handlers_;
};
class Factory:public AddonFactory {AddonInstance *create(AddonManager *manager)override{return new FocusBridge(manager->instance());}};
FCITX_ADDON_FACTORY(Factory)
