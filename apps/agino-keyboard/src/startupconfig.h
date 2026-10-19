// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDir>
#include <QByteArrayList>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include "log.h"
// changed (optional): set to true only when the file was rewritten.
inline bool enableVirtualKeyboardConfig(const QString &path,bool *changed=nullptr){
 if(changed)*changed=false;
 QByteArray old;QFile input(path);
 if(input.exists()){if(!input.open(QIODevice::ReadOnly)){qCWarning(lcController()).noquote()<<"Cannot read"<<path<<":"<<input.errorString();return false;}old=input.readAll();if(input.error()!=QFileDevice::NoError){qCWarning(lcController()).noquote()<<"Cannot read"<<path<<":"<<input.errorString();return false;}input.close();}
 auto lines=old.split('\n');if(!lines.isEmpty()&&lines.last().isEmpty())lines.removeLast();bool found=false;
 for(auto &line:lines){if(line.endsWith('\r'))line.chop(1);if(line.startsWith("EnableVirtualKeyboard=")){line="EnableVirtualKeyboard=True";found=true;}}
 if(!found)lines.append("EnableVirtualKeyboard=True");const QByteArray contents=lines.join('\n')+'\n';if(contents==old)return true;
 if(!QDir().mkpath(QFileInfo(path).absolutePath())){qCWarning(lcController()).noquote()<<"Cannot create the directory of"<<path;return false;}
 QSaveFile output(path);if(!output.open(QIODevice::WriteOnly)){qCWarning(lcController()).noquote()<<"Cannot write"<<path<<":"<<output.errorString();return false;}output.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
 if(output.write(contents)!=contents.size()){qCWarning(lcController()).noquote()<<"Cannot write"<<path<<":"<<output.errorString();output.cancelWriting();return false;}
 if(!output.commit()){qCWarning(lcController()).noquote()<<"Cannot save"<<path<<":"<<output.errorString();return false;}
 if(changed)*changed=true;
 qCInfo(lcController()).noquote()<<"Enabled the Fcitx5 virtual keyboard in"<<path;
 return true;
}
