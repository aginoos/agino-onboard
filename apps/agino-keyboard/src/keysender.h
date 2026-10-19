// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QHash>
#include "keyboardstate.h"

// What KeySender needs from the X server. The Xlib implementation is in embed-main.cpp, the unit test uses a fake.
class KeyBackend {
public:
 virtual ~KeyBackend()=default;
 virtual bool focusAccepts()=0;                  // a window other than the keyboard itself has the input focus
 virtual quint8 keycodeFor(quint32 sym)=0;       // 0: no key produces the symbol
 virtual quint32 keysymAt(quint8 code,int level)=0; // level 0..3 of the key in the current group
 virtual bool capsLockOn()=0;
 virtual void fake(quint8 code,bool press)=0;
 virtual void flush()=0;
};

// Sends the keys of the keyboard to the focused window as X11 key events (XTest), like a physical keyboard.
// The keyboard sends symbols (the latched Shift and the Caps Lock only change the symbol, they are not key
// events), so a key whose symbol is on the Shift or AltGr level of the layout gets those modifiers pressed
// around it. Only the current keyboard group is searched: a symbol that is not in it is not sent.
class KeySender {
public:
 static constexpr quint32 ShiftL=0xffe1,ShiftR=0xffe2,Level3Shift=0xfe03;
 explicit KeySender(KeyBackend &backend):backend_(backend){}
 ~KeySender(){releaseAll();}
 KeySender(const KeySender&)=delete;KeySender &operator=(const KeySender&)=delete;
 void send(KeySpec k,bool release){
  const quint8 code=k.code?quint8(k.code):backend_.keycodeFor(k.sym);
  if(!code)return;
  const bool shiftKey=k.sym==ShiftL||k.sym==ShiftR;
  if(shiftKey)shiftHeld_=!release;
  if(release){
   // The release of a key that was sent is always sent, wherever the focus is now: a key left down in the
   // X server would repeat and stay down in the session that starts next.
   const auto it=down_.find(code);
   if(it==down_.end())return;
   const Wrapped wrapped=it.value();down_.erase(it);
   backend_.fake(code,false);unwrap(wrapped);backend_.flush();
   return;
  }
  if(!backend_.focusAccepts())return;
  Wrapped wrapped;
  if(!shiftKey&&!plan(code,k.sym,wrapped))return;
  wrap(wrapped);backend_.fake(code,true);down_.insert(code,wrapped);backend_.flush();
 }
 // Releases every key that is still down (the program ends, the keyboard is closed).
 void releaseAll(){
  if(down_.isEmpty())return;
  const auto keys=down_;down_.clear();
  for(auto it=keys.begin();it!=keys.end();++it){backend_.fake(it.key(),false);unwrap(it.value());}
  backend_.flush();
 }
private:
 struct Wrapped {bool shift=false,level3=false;};
 // Which modifiers the key needs to produce the symbol; false when it is not on the key at all.
 bool plan(quint8 code,quint32 sym,Wrapped &wrapped){
  int level=-1;for(int n=0;n<4&&level<0;++n)if(backend_.keysymAt(code,n)==sym)level=n;
  if(level<0)return false;
  bool shift=level&1;const bool level3=level&2;
  // The Caps Lock of the X server turns the letters of the first level into capitals: Shift then cancels it.
  const quint32 low=backend_.keysymAt(code,0),high=backend_.keysymAt(code,1);
  if(backend_.capsLockOn()&&low!=high&&low>='a'&&low<='z'&&high>='A'&&high<='Z')shift=!shift;
  // The Shift key of the keyboard is held: it is already down in the X server.
  wrapped.shift=shift&&!shiftHeld_;
  wrapped.level3=level3;
  if(wrapped.level3&&!backend_.keycodeFor(Level3Shift))return false;
  if(wrapped.shift&&!backend_.keycodeFor(ShiftL))return false;
  return true;
 }
 void wrap(const Wrapped &w){if(w.shift)backend_.fake(backend_.keycodeFor(ShiftL),true);if(w.level3)backend_.fake(backend_.keycodeFor(Level3Shift),true);}
 void unwrap(const Wrapped &w){if(w.level3)backend_.fake(backend_.keycodeFor(Level3Shift),false);if(w.shift)backend_.fake(backend_.keycodeFor(ShiftL),false);}
 KeyBackend &backend_;bool shiftHeld_=false;QHash<quint8,Wrapped> down_;
};
