// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QList>
#include <QtGlobal>

// Decides where the embedded keyboard may type (SEC-03): only into the window of the session program (lock screen, greeter).
// Pure logic without X: the Xlib side (embed-main.cpp) fetches the parent chains and the _NET_WM_PID values.
// A chain lists the window ids from the window up to the top-level window (the direct child of the root), inclusive;
// an empty chain means the X server could not answer, which never accepts.
namespace FocusGuard {
// The focus is the keyboard's own window or inside it: never typed into, whoever owns the ancestors.
inline bool isOwn(const QList<quint32> &own,const QList<quint32> &focus){return !own.isEmpty()&&focus.contains(own.first());}
// The focus is in the window tree of the keyboard's top-level: the session program embedded the keyboard there.
// Unparented (the own window is the top-level itself) nothing outside the own window matches.
inline bool sameToplevel(const QList<quint32> &own,const QList<quint32> &focus){return !own.isEmpty()&&!focus.isEmpty()&&own.last()==focus.last();}
// One of the pids (_NET_WM_PID of the focus window and its ancestors, 0 = none) is the session program.
inline bool pidMatches(const QList<quint32> &pids,qint64 session){
 if(session<=1)return false;   // 0 and init are no session program (the parent ended and we were reparented)
 for(const quint32 pid:pids)if(pid&&qint64(pid)==session)return true;
 return false;
}
}
