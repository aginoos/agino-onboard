#pragma once
#include <QObject>
#include <QSettings>
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
 explicit Preferences(QObject *p=nullptr):QObject(p){normalizeOpacity();}
 bool functionRow()const{return settings_.value("functionRow",false).toBool();}
 int mode()const{return qBound(0,settings_.value("mode",0).toInt(),2);}
 bool compact()const{return settings_.value("compact",false).toBool();}
 int opacityPercent()const{return qBound(50,settings_.value("opacityPercent",100).toInt(),100);}
 bool keySound()const{return settings_.value("keySound",false).toBool();}
 int screenIndex()const{return settings_.value("screenIndex",-1).toInt();}
 int theme()const{return qBound(0,settings_.value("theme",0).toInt(),2);}
 bool safeInput()const{return settings_.value("safeInput",false).toBool();}
 QString shortcut()const{return settings_.value("shortcut","Ctrl+Alt+K").toString();}
 QString preferredIm()const{return settings_.value("preferredIm").toString();}
 void setFunctionRow(bool v){set("functionRow",v);}void setMode(int v){set("mode",qBound(0,v,2));}
 void setCompact(bool v){set("compact",v);}
 void setOpacityPercent(int v){set("opacityPercent",qBound(50,v,100));}
 void setKeySound(bool v){set("keySound",v);}void setScreenIndex(int v){set("screenIndex",v);}
 void setTheme(int v){set("theme",qBound(0,v,2));}void setSafeInput(bool v){set("safeInput",v);}
 void setShortcut(const QString &v){set("shortcut",v);}void setPreferredIm(const QString &v){set("preferredIm",v);}
 Q_INVOKABLE void reload(){settings_.sync();normalizeOpacity();emit changed();}
signals:void changed();
private:
 void normalizeOpacity(){if(settings_.contains("opacityPercent")){const int saved=settings_.value("opacityPercent").toInt(),clamped=qBound(50,saved,100);if(saved!=clamped){settings_.setValue("opacityPercent",clamped);settings_.sync();}}}
 void set(const QString &k,const QVariant &v){if(settings_.value(k)!=v){settings_.setValue(k,v);settings_.sync();emit changed();}}QSettings settings_;
};
