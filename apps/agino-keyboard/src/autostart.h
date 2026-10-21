#pragma once
#include <QDir>
#include <QFile>
#include <QStandardPaths>
// The controller is not started at login (the launcher icon starts it, so does the shortcut once it runs). Only while the user has chosen
// the automatic mode, in which touch input shows the keyboard by itself and the controller must be there, it asks the session to start it
// at login: a per-user autostart entry copied from the template the package installs. Any other mode removes the entry again. An entry
// that the user wrote (no marker line) is never replaced or deleted.
namespace Autostart {
inline constexpr char marker[]="X-Agino-Autostart-Managed=true";
inline QString entryPath(const QString &configHome=QString()){
 const QString base=configHome.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::ConfigLocation):configHome;
 return QDir(base).filePath(QStringLiteral("autostart/agino-keyboard.desktop"));
}
inline bool isOurs(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)&&QString::fromUtf8(f.readAll()).contains(QLatin1String(marker));}
// wanted: create or refresh the entry from templatePath; not wanted: remove ours. Returns true when the state is as requested.
inline bool sync(bool wanted,const QString &templatePath,const QString &configHome=QString()){
 const QString path=entryPath(configHome);const bool exists=QFile::exists(path);
 if(!wanted)return !exists||!isOurs(path)||QFile::remove(path);
 if(exists&&!isOurs(path))return true;   // the user's own entry stands
 QFile source(templatePath);if(!source.open(QIODevice::ReadOnly))return false;
 const QByteArray content=source.readAll();
 if(exists){QFile current(path);if(current.open(QIODevice::ReadOnly)&&current.readAll()==content)return true;}
 if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;
 QFile target(path);if(!target.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;
 return target.write(content)==content.size();
}
}
