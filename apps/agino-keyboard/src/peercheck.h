// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QStringList>
#include "log.h"

// Who is behind a bus name (SEC-10, SEC-20): the bus daemon vouches for the process id of a connection, /proc/<pid>/exe says
// which program it runs. This stops other programs from posing as the Fcitx5 bridge, the controller or the session manager;
// it does not stop code that runs inside those programs. Calls are synchronous (a round trip to the bus daemon): ask once
// per unique bus name and keep the answer until the name goes away, but only a decided answer: when the program cannot be
// found (check() < 0) ask again next time.
namespace PeerCheck {
// The program of the process that owns the bus name (unique or well-known); empty when it cannot be found.
// A trailing " (deleted)" (the file was replaced by an upgrade while the program runs) is removed.
inline QString exeOf(const QDBusConnection &bus,const QString &name){
 if(name.isEmpty()||!bus.interface())return {};
 const QDBusReply<uint> pid=bus.interface()->servicePid(name);
 if(!pid.isValid()||pid.value()==0)return {};
 QString path=QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid.value()));
 static const QString gone=QStringLiteral(" (deleted)");
 if(path.endsWith(gone))path.chop(gone.size());
 const QString real=QFileInfo(path).canonicalFilePath();
 return real.isEmpty()?path:real;
}
// 1: the program behind the name is one of the allowed programs; 0: it is another program; -1: it could not be found.
// AGINO_TEST_HOOKS in the environment (the tests, which run their own programs) trusts every name.
inline int check(const QDBusConnection &bus,const QString &name,const QStringList &allowed){
 if(qEnvironmentVariableIsSet("AGINO_TEST_HOOKS")){
  static bool said=false;if(!said){said=true;qCWarning(lcSession(),"AGINO_TEST_HOOKS is set: the programs behind the bus names are not checked");}
  return 1;
 }
 const QString exe=exeOf(bus,name);
 if(exe.isEmpty()){qCDebug(lcSession())<<"The program behind"<<name<<"cannot be found (no pid or no /proc entry)";return -1;}
 for(const QString &path:allowed){const QString real=QFileInfo(path).canonicalFilePath();if(exe==path||(!real.isEmpty()&&exe==real))return 1;}
 qCDebug(lcSession())<<"The program behind"<<name<<"is"<<exe<<", not an allowed one";
 return 0;
}
inline bool peerIs(const QDBusConnection &bus,const QString &name,const QStringList &allowed){return check(bus,name,allowed)>0;}

// A signal source on the session bus: the sender must own the well-known name now and run an expected program. The verdict of the latest
// owner is kept (a unique name is never reused); one warning per refused name.
struct Gate{
 QString name;QStringList allowed;QString lastPeer;bool lastOk=false;QSet<QString> warned;
 bool accept(const QDBusConnection &bus,const QString &sender){
  auto *iface=bus.interface();if(sender.isEmpty()||!iface)return false;
  const auto current=iface->serviceOwner(name);if(!current.isValid()||current.value()!=sender)return false;
  if(sender!=lastPeer){const int verdict=check(bus,sender,allowed);if(verdict<0)return false;lastPeer=sender;lastOk=verdict>0;}
  if(!lastOk&&!warned.contains(sender)){warned.insert(sender);qCWarning(lcSession()).noquote()<<"Ignoring"<<name<<"signals from"<<sender<<"("<<exeOf(bus,sender)<<"): not an expected program";}
  return lastOk;
 }
};
}
