#pragma once
#include <QObject>
#include <QDBusContext>
#include <QHash>
class VirtualKeyboardUi:public QObject,protected QDBusContext {
 Q_OBJECT
 Q_CLASSINFO("D-Bus Interface","org.fcitx.Fcitx5.VirtualKeyboard1")
public:
 explicit VirtualKeyboardUi(QObject *p=nullptr):QObject(p){}
 bool start();
 bool ownsInterface()const{return ownsInterface_;}
public slots:
 void ShowVirtualKeyboard(); void HideVirtualKeyboard();
 void UpdatePreeditArea(const QString &){} void UpdatePreeditCaret(int){}
 void UpdateCandidateArea(const QStringList &,bool,bool,int,int){}
 void NotifyIMActivated(const QString &){}void NotifyIMDeactivated(const QString &){}void NotifyIMListChanged();
signals:
 void showRequested();void hideRequested();void inputMethodsChanged();
private:bool trusted()const;bool ownsInterface_=false;mutable QHash<QString,bool> verdicts_;
};
