// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QChar>
#include <QHash>
#include <QSet>
#include "keyboardstate.h"

// What KeySender needs from the X server. The Xlib implementation is in embed-main.cpp, the unit test uses a fake.
class KeyBackend {
public:
 virtual ~KeyBackend()=default;
 virtual bool focusAccepts()=0;                  // a window other than the keyboard itself has the input focus
 // The key that produces the symbol in the current group of the keyboard (layout) and the level (0..3: plain, Shift,
 // AltGr, Shift+AltGr) it is on; 0 when no key does. Symbols are compared as characters (see KeySender::character).
 virtual quint8 findKey(quint32 sym,int *level)=0;
 virtual quint32 keysymAt(quint8 code,int level)=0; // level 0..3 of the key in the current group
 virtual bool capsLockOn()=0;
 virtual void fake(quint8 code,bool press)=0;
 virtual void flush()=0;
};

// Sends the keys of the keyboard to the focused window as X11 key events (XTest), like a physical keyboard.
// The keyboard sends symbols (the latched Shift and the Caps Lock only change the symbol, they are not key
// events), so a key whose symbol is on the Shift or AltGr level of the layout gets those modifiers pressed
// around it, and a Shift of the keyboard that is held is released around a key that must not have it. Only the
// current keyboard group is searched: a symbol that is not in it is not sent.
class KeySender {
public:
 static constexpr quint32 ShiftL=0xffe1,ShiftR=0xffe2,Level3Shift=0xfe03;
 explicit KeySender(KeyBackend &backend):backend_(backend){}
 ~KeySender(){releaseAll();}
 KeySender(const KeySender&)=delete;KeySender &operator=(const KeySender&)=delete;
 // The character of a keysym: Unicode keysyms (0x1000000 + code point) and the Latin-1 keysyms (equal to the code point) compare equal.
 static quint32 character(quint32 sym){return sym>=0x1000000&&sym<=0x110ffff+0x1000000?sym-0x1000000:sym;}
 void send(KeySpec k,bool release){
  int level=0;
  const bool shiftKey=k.sym==ShiftL||k.sym==ShiftR;
  quint8 code=k.code?quint8(k.code):0;
  // The release goes to the key that was pressed, also when the layout was switched in between.
  if(!code&&release)code=codeOf_.take(k.sym);
  if(!code)code=backend_.findKey(k.sym,&level);
  if(!code)return;
  if(release){
   // The release of a key that was sent is always sent, wherever the focus is now: a key left down in the
   // X server would repeat and stay down in the session that starts next.
   const auto it=down_.find(code);
   if(it==down_.end())return;
   const Wrapped wrapped=it.value();down_.erase(it);
   backend_.fake(code,false);unwrap(wrapped);
   if(shiftKey){heldShifts_.remove(code);}
   backend_.flush();
   return;
  }
  if(!backend_.focusAccepts())return;
  if(down_.contains(code)){backend_.fake(code,true);backend_.flush();return;}   // the repeat of a held key
  Wrapped wrapped;
  if(!shiftKey&&!plan(code,k.sym,level,wrapped))return;
  wrap(wrapped);backend_.fake(code,true);down_.insert(code,wrapped);codeOf_.insert(k.sym,code);
  if(shiftKey)heldShifts_.insert(code);
  backend_.flush();
 }
 // Releases every key that is still down (the program ends, the keyboard is closed).
 void releaseAll(){
  if(down_.isEmpty())return;
  const auto keys=down_;down_.clear();heldShifts_.clear();codeOf_.clear();
  for(auto it=keys.begin();it!=keys.end();++it){backend_.fake(it.key(),false);unwrap(it.value());}
  backend_.flush();
 }
private:
 // heldShift: the Shift key to release around the key (a Shift of the keyboard is held, the symbol must not have it);
 // shift, level3: modifier keys to press around the key.
 struct Wrapped {quint8 shift=0,level3=0,heldShift=0;};
 // Which modifiers the key needs to produce the symbol in the current state of the server; false when it cannot be sent.
 bool plan(quint8 code,quint32 sym,int level,Wrapped &wrapped){
  Q_UNUSED(sym)
  bool shift=level&1;const bool level3=level&2;
  // The Caps Lock of the server turns the letters of the first level into capitals: Shift then cancels it.
  const quint32 low=character(backend_.keysymAt(code,0)),high=character(backend_.keysymAt(code,1));
  if(backend_.capsLockOn()&&low!=high&&low<0x110000&&QChar::toUpper(low)==high)shift=!shift;
  // A Shift of the keyboard that is held is down in the server: the key gets it, or it is released around the key.
  const bool heldShift=!heldShifts_.isEmpty();
  if(heldShift&&!shift)wrapped.heldShift=*heldShifts_.begin();
  else if(!heldShift&&shift){int shiftLevel=0;wrapped.shift=backend_.findKey(ShiftL,&shiftLevel);if(!wrapped.shift)return false;}
  if(level3){int l=0;wrapped.level3=backend_.findKey(Level3Shift,&l);if(!wrapped.level3)return false;}
  return true;
 }
 void wrap(const Wrapped &w){
  if(w.heldShift)backend_.fake(w.heldShift,false);
  if(w.shift)backend_.fake(w.shift,true);
  if(w.level3)backend_.fake(w.level3,true);
 }
 void unwrap(const Wrapped &w){
  if(w.level3)backend_.fake(w.level3,false);
  if(w.shift)backend_.fake(w.shift,false);
  if(w.heldShift&&heldShifts_.contains(w.heldShift))backend_.fake(w.heldShift,true);   // still held: press it again
 }
 KeyBackend &backend_;QSet<quint8> heldShifts_;QHash<quint8,Wrapped> down_;QHash<quint32,quint8> codeOf_;
};
