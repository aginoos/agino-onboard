// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVariantMap>
#include <cmath>
#include <limits>
#include <type_traits>
// What the keyboard window reports (its Status() and StateChanged) and what the controller hands to the next window
// (--resume-json). The D-Bus key names and value types are the protocol of tests/process-lifecycle.py: do not rename or retype.
namespace PanelStatusDetail {
 inline bool isNumber(const QVariant &v){
  switch(v.typeId()){
   case QMetaType::Int:case QMetaType::UInt:case QMetaType::LongLong:case QMetaType::ULongLong:case QMetaType::Long:case QMetaType::ULong:
   case QMetaType::Short:case QMetaType::UShort:case QMetaType::Char:case QMetaType::SChar:case QMetaType::UChar:case QMetaType::Double:case QMetaType::Float:return true;
   default:return false;
  }
 }
 // A value of the wrong type (or out of range) leaves the field as it is: the strings "false" or "x" are not booleans, numbers are not strings.
 inline void read(const QVariantMap &m,const char *key,bool &out){const auto v=m.value(key);if(v.typeId()==QMetaType::Bool)out=v.toBool();}
 inline void read(const QVariantMap &m,const char *key,QString &out){const auto v=m.value(key);if(v.typeId()==QMetaType::QString)out=v.toString();}
 inline void read(const QVariantMap &m,const char *key,double &out){const auto v=m.value(key);if(!isNumber(v))return;const double d=v.toDouble();if(std::isfinite(d))out=d;}
 template<class T,std::enable_if_t<std::is_integral_v<T>&&!std::is_same_v<T,bool>,int> =0>
 inline void read(const QVariantMap &m,const char *key,T &out){
  const auto v=m.value(key);if(!isNumber(v))return;
  if(v.typeId()==QMetaType::Double||v.typeId()==QMetaType::Float){   // JSON numbers; the upper bound is exclusive (2^64 is not representable below it)
   const double d=v.toDouble();if(std::isfinite(d)&&d>=double(std::numeric_limits<T>::min())&&d<double(std::numeric_limits<T>::max()))out=T(d);
   return;
  }
  if(v.typeId()==QMetaType::ULongLong||v.typeId()==QMetaType::ULong){const qulonglong u=v.toULongLong();if(u<=qulonglong(std::numeric_limits<T>::max()))out=T(u);return;}
  const qlonglong s=v.toLongLong();
  if(s<0?(std::is_signed_v<T>&&s>=qlonglong(std::numeric_limits<T>::min())):qulonglong(s)<=qulonglong(std::numeric_limits<T>::max()))out=T(s);
 }
}
struct PanelStatus {
 // The window state (Status()/StateChanged).
 bool capsLock=false;quint32 modifiers=0;QString shortcutError,inputMethodName,inputMethodStatus,inputLanguageCode;qint64 methodsCount=0;
 quint64 epoch=0,generation=0;
 bool connected=false,editable=false,sensitive=false,expanded=false,transparencySupported=false;
 int panelX=0,panelY=0,panelWidth=0,panelHeight=0;
 bool visible=false,settingsVisible=false,panelLoaded=false,viewCreated=false;
 quint64 showSerial=0,firstFrameSerial=0;double firstFrameMs=-1;
 int graphicsApi=0;QString renderer,platform;bool automatic=false;QString desktop;
 // Where the panel floats (DesktopAdapter::placement()).
 bool floating=false;double floatX=0,floatY=0;QString outputName;
 // The handoff to the next window only: not part of Status(), see toVariantMap(true).
 bool resuming=false,applyPreferred=false;int sourceKind=0;double sourceX=0,sourceY=0;

 // handoff=false: exactly the keys of the window Status(); true: plus the keys only the handoff carries.
 QVariantMap toVariantMap(bool handoff=false)const{
  QVariantMap map;
  visitStatus(*this,[&map](const char *key,const auto &value){map.insert(QLatin1String(key),QVariant::fromValue(value));});
  if(handoff)visitHandoff(*this,[&map](const char *key,const auto &value){map.insert(QLatin1String(key),QVariant::fromValue(value));});
  return map;
 }
 // Missing keys keep their defaults; a value of the wrong type is ignored.
 static PanelStatus fromVariantMap(const QVariantMap &map){
  PanelStatus status;
  const auto reader=[&map](const char *key,auto &value){PanelStatusDetail::read(map,key,value);};
  visitStatus(status,reader);visitHandoff(status,reader);
  return status;
 }
 QByteArray toJson()const{return QJsonDocument(QJsonObject::fromVariantMap(toVariantMap(true))).toJson(QJsonDocument::Compact);}
 // Anything that is not a JSON object gives the defaults.
 static PanelStatus fromJson(const QByteArray &json){
  const auto document=QJsonDocument::fromJson(json);
  return document.isObject()?fromVariantMap(document.object().toVariantMap()):PanelStatus();
 }
private:
 template<class S,class F> static void visitStatus(S &s,F &&f){
  f("capsLock",s.capsLock);f("modifiers",s.modifiers);f("shortcutError",s.shortcutError);f("inputMethodName",s.inputMethodName);f("inputMethodStatus",s.inputMethodStatus);
  f("inputLanguageCode",s.inputLanguageCode);f("methodsCount",s.methodsCount);f("epoch",s.epoch);f("generation",s.generation);
  f("connected",s.connected);f("editable",s.editable);f("sensitive",s.sensitive);f("expanded",s.expanded);f("transparencySupported",s.transparencySupported);
  f("panelX",s.panelX);f("panelY",s.panelY);f("panelWidth",s.panelWidth);f("panelHeight",s.panelHeight);
  f("visible",s.visible);f("settingsVisible",s.settingsVisible);f("panelLoaded",s.panelLoaded);f("viewCreated",s.viewCreated);
  f("showSerial",s.showSerial);f("firstFrameSerial",s.firstFrameSerial);f("firstFrameMs",s.firstFrameMs);
  f("graphicsApi",s.graphicsApi);f("renderer",s.renderer);f("platform",s.platform);f("automatic",s.automatic);f("desktop",s.desktop);
  f("floating",s.floating);f("floatX",s.floatX);f("floatY",s.floatY);f("outputName",s.outputName);
 }
 template<class S,class F> static void visitHandoff(S &s,F &&f){
  f("resuming",s.resuming);f("applyPreferred",s.applyPreferred);f("sourceKind",s.sourceKind);f("sourceX",s.sourceX);f("sourceY",s.sourceY);
 }
};
