// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDir>
#include <QByteArrayList>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
inline bool enableVirtualKeyboardConfig(const QString &path){
 QByteArray old;QFile input(path);
 if(input.exists()){if(!input.open(QIODevice::ReadOnly))return false;old=input.readAll();if(input.error()!=QFileDevice::NoError)return false;input.close();}
 auto lines=old.split('\n');if(!lines.isEmpty()&&lines.last().isEmpty())lines.removeLast();bool found=false;
 for(auto &line:lines){if(line.endsWith('\r'))line.chop(1);if(line.startsWith("EnableVirtualKeyboard=")){line="EnableVirtualKeyboard=True";found=true;}}
 if(!found)lines.append("EnableVirtualKeyboard=True");const QByteArray contents=lines.join('\n')+'\n';if(contents==old)return true;
 if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;
 QSaveFile output(path);if(!output.open(QIODevice::WriteOnly))return false;output.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
 if(output.write(contents)!=contents.size()){output.cancelWriting();return false;}return output.commit();
}
