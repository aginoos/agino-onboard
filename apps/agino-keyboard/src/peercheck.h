// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

// Who is behind a bus name (SEC-10, SEC-20): the bus daemon vouches for the process id of a connection, /proc/<pid>/exe says
// which program it runs. This stops other programs from posing as the Fcitx5 bridge, the controller or the session manager;
// it does not stop code that runs inside those programs. Calls are synchronous (a round trip to the bus daemon): ask once
// per unique bus name and keep the answer until the name goes away.
namespace PeerCheck {
// The program of the process that owns the bus name (unique or well-known); empty when it cannot be found.
// A trailing " (deleted)" (the file was replaced by an upgrade while the program runs) is removed.
inline QString exeOf(const QDBusConnection &bus,const QString &name){
 if(!bus.interface())return {};
 const QDBusReply<uint> pid=bus.interface()->servicePid(name);
 if(!pid.isValid()||pid.value()==0)return {};
 QString path=QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid.value()));
 static const QString gone=QStringLiteral(" (deleted)");
 if(path.endsWith(gone))path.chop(gone.size());
 return QFileInfo(path).canonicalFilePath().isEmpty()?path:QFileInfo(path).canonicalFilePath();
}
// True when the program behind the name is one of the allowed programs. AGINO_TEST_HOOKS in the environment (the tests, which run their
// own programs) trusts every name.
inline bool peerIs(const QDBusConnection &bus,const QString &name,const QStringList &allowed){
 if(qEnvironmentVariableIsSet("AGINO_TEST_HOOKS"))return true;
 const QString exe=exeOf(bus,name);
 return !exe.isEmpty()&&allowed.contains(exe);
}
}
