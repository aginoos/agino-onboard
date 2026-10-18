#include "keyboardstate.h"
#include <QSet>
static bool shiftKey(quint32 sym){return sym==0xffe1||sym==0xffe2;}
static quint32 mask(quint32 sym) {
 switch(sym){case 0xffe1:case 0xffe2:return 1;case 0xffe3:case 0xffe4:return 4;case 0xffe9:case 0xffea:return 8;case 0xffeb:case 0xffec:return 64;default:return 0;}
}
void KeyboardState::show(bool v){if(v&&!layoutFits_)return;if(visible_==v)return; if(!v)cancelAll(); visible_=v;emit visibleChanged(v);}
void KeyboardState::updateModifiers(){quint32 m=(caps_?2:0)|(shiftLatched_?1:0);for(const auto &t:touches_)m|=mask(t.key.sym);if(m!=modifiers_){modifiers_=m;emit modifiersChanged();}}
KeySpec KeyboardState::resolved(KeySpec k)const{
 k.modifiers|=modifiers_;
 if(k.sym>='a'&&k.sym<='z'){if(bool(k.modifiers&1)!=bool(k.modifiers&2))k.sym-=32;}
 else if(k.modifiers&3){const QByteArray plain="`1234567890-=[]\\;',./",shift="~!@#$%^&*()_+{}|:\"<>?";int n=plain.indexOf(char(k.sym));if(k.sym<128&&n>=0)k.sym=quint8(shift[n]);}
 return k;
}
void KeyboardState::beginTouch(int id,KeySpec k){
 if(!layoutFits_||!editable_||mode_==Off||settingsOpen_||touches_.contains(id))return;
 const bool shift=shiftKey(k.sym);
 if(!shift||!touches_.isEmpty()){lastShiftTap_=-1;for(auto &held:touches_)held.shiftTap=false;}
 TouchKey t{resolved(k),bool(mask(k.sym)),clock_+450,clock_,shift&&touches_.isEmpty()};
 bool duplicate=false;for(const auto &held:touches_)if(held.sent&&held.key.sym==t.key.sym)duplicate=true;
 touches_.insert(id,t); updateModifiers();
 if(t.sent&&!duplicate)emit keyRequested(t.key,false);
}
void KeyboardState::endTouch(int id,bool inside){
 auto it=touches_.find(id);if(it==touches_.end())return;auto t=it.value();touches_.erase(it);
 if(t.sent){bool duplicate=false;for(const auto &held:touches_)if(held.sent&&held.key.sym==t.key.sym)duplicate=true;if(!duplicate)emit keyRequested(t.key,true);}else if(inside&&editable_&&!t.repeated){shiftLatched_=false;emit keyRequested(t.key,false);emit keyRequested(t.key,true);}
 if(shiftKey(t.key.sym)){
  if(inside&&t.shiftTap&&clock_-t.started<=350){if(lastShiftTap_>=0&&clock_-lastShiftTap_<=400){caps_=!caps_;shiftLatched_=false;lastShiftTap_=-1;}else{shiftLatched_=!shiftLatched_;lastShiftTap_=clock_;}}
  else lastShiftTap_=-1;
 }
 if(inside&&t.key.sym==0xffe5)caps_=!caps_;updateModifiers();
}
void KeyboardState::cancelAll(){lastShiftTap_=-1;shiftLatched_=false;const auto copy=touches_;touches_.clear();QSet<quint32> released;for(const auto &t:copy)if(t.sent&&!released.contains(t.key.sym)){released.insert(t.key.sym);emit keyRequested(t.key,true);}updateModifiers();}
void KeyboardState::setContext(quint64 e,bool editable,bool sensitive){
 if(e!=epoch_||!editable||sensitive!=sensitive_)cancelAll();
 if(e!=epoch_)suppressed_=false;
 epoch_=e;editable_=editable;sensitive_=sensitive;emit inputAllowedChanged();
 // Keep the panel across keyboard focus traversal, including transient FocusOut.
 // editable_ still gates input; lock, disconnect and explicit dismissal hide it.
 if(mode_==Automatic)show(settingsOpen_||(!suppressed_&&(manualVisible_||visible_||(editable_&&(source_==Touch||source_==Pen)))));
}
void KeyboardState::setMode(Mode m){if(mode_==m)return;cancelAll();manualVisible_=false;mode_=m;emit inputAllowedChanged();if(!settingsOpen_)show(false);}
void KeyboardState::setSource(InputSource s){source_=s;if(!layoutFits_||settingsOpen_)return;if(s==VirtualKeyboard)return;if(mode_!=Automatic)return;if(s==Touch||s==Pen){suppressed_=false;show(editable_);}}
void KeyboardState::advanceClock(qint64 now){clock_=now;for(auto &t:touches_)if(t.key.sym==0xff08){while(t.nextRepeat<=now){emit keyRequested(t.key,false);emit keyRequested(t.key,true);t.repeated=true;t.nextRepeat+=60;}}}
void KeyboardState::manualShow(){if(layoutFits_&&mode_!=Off){suppressed_=false;manualVisible_=true;show(true);}}
void KeyboardState::setSettingsOpen(bool value){
 if(settingsOpen_==value||(!layoutFits_&&value))return;
 cancelAll();settingsOpen_=value;emit settingsOpenChanged();emit inputAllowedChanged();
 if(value)show(true);else if(mode_==Off)show(false);else manualShow();
}
void KeyboardState::dismiss(){suppressed_=true;manualVisible_=false;source_=Unknown;if(settingsOpen_){settingsOpen_=false;emit settingsOpenChanged();}show(false);}
