#pragma once
#include <QObject>
#include <QSettings>
#include <QVariantMap>
#include "log.h"
class Preferences:public QObject {
 Q_OBJECT
 Q_PROPERTY(bool functionRow READ functionRow WRITE setFunctionRow NOTIFY changed)
 Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY changed)
 Q_PROPERTY(bool compact READ compact WRITE setCompact NOTIFY changed)
 Q_PROPERTY(int opacityPercent READ opacityPercent WRITE setOpacityPercent NOTIFY changed)
 Q_PROPERTY(bool keySound READ keySound WRITE setKeySound NOTIFY changed)
 Q_PROPERTY(int screenIndex READ screenIndex WRITE setScreenIndex NOTIFY changed)
 Q_PROPERTY(int theme READ theme WRITE setTheme NOTIFY changed)
 Q_PROPERTY(bool safeInput READ safeInput WRITE setSafeInput NOTIFY changed)
 Q_PROPERTY(QString shortcut READ shortcut WRITE setShortcut NOTIFY changed)
 Q_PROPERTY(QString preferredIm READ preferredIm WRITE setPreferredIm NOTIFY changed)
public:
 // memoryOnly: nothing is read from or written to a settings file (the keyboard of the login and lock screens has a fixed look).
 explicit Preferences(QObject *p=nullptr,bool memoryOnly=false):QObject(p),memoryOnly_(memoryOnly){if(!memoryOnly_)normalizeOpacity();}
 bool functionRow()const{return get("functionRow",false).toBool();}
 // 0 automatic (touch input shows the keyboard), 1 manual, 2 off. The default is manual: nothing starts the controller at login, the user runs the keyboard.
 int mode()const{return qBound(0,get("mode",1).toInt(),2);}
 bool compact()const{return get("compact",false).toBool();}
 int opacityPercent()const{return qBound(50,get("opacityPercent",100).toInt(),100);}
 bool keySound()const{return get("keySound",false).toBool();}
 int screenIndex()const{return get("screenIndex",-1).toInt();}
 int theme()const{return qBound(0,get("theme",0).toInt(),2);}
 bool safeInput()const{return get("safeInput",false).toBool();}
 QString shortcut()const{return get("shortcut","Ctrl+Alt+K").toString();}
 QString preferredIm()const{return get("preferredIm").toString();}
 void setFunctionRow(bool v){set("functionRow",v);}void setMode(int v){set("mode",qBound(0,v,2));}
 void setCompact(bool v){set("compact",v);}
 void setOpacityPercent(int v){set("opacityPercent",qBound(50,v,100));}
 void setKeySound(bool v){set("keySound",v);}void setScreenIndex(int v){set("screenIndex",v);}
 void setTheme(int v){set("theme",qBound(0,v,2));}void setSafeInput(bool v){set("safeInput",v);}
 void setShortcut(const QString &v){set("shortcut",v);}void setPreferredIm(const QString &v){set("preferredIm",v);}
 Q_INVOKABLE void reload(){if(!memoryOnly_){synced();normalizeOpacity();}emit changed();}
signals:void changed();
private:
 // A settings file that cannot be read or written is reported once (the path, not the content).
 void synced(){settings_.sync();if(settings_.status()!=QSettings::NoError&&!failureLogged_){failureLogged_=true;qCWarning(lcController()).noquote()<<"The settings file"<<settings_.fileName()<<"cannot be used:"<<(settings_.status()==QSettings::AccessError?"access error (not writable?)":"format error");}}
 void normalizeOpacity(){if(settings_.contains("opacityPercent")){const int saved=settings_.value("opacityPercent").toInt(),clamped=qBound(50,saved,100);if(saved!=clamped){settings_.setValue("opacityPercent",clamped);synced();}}}
 QVariant get(const QString &k,const QVariant &fallback=QVariant())const{return memoryOnly_?memory_.value(k,fallback):settings_.value(k,fallback);}
 void set(const QString &k,const QVariant &v){if(get(k)==v)return;if(memoryOnly_)memory_.insert(k,v);else{settings_.setValue(k,v);synced();}emit changed();}
 bool memoryOnly_=false,failureLogged_=false;QVariantMap memory_;QSettings settings_;
};
