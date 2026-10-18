// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QThread>
#include <QList>
class KeySound:public QObject {
 Q_OBJECT
public:
 explicit KeySound(QObject *parent=nullptr):QObject(parent){}
 ~KeySound()override;
 void setEnabled(bool);
 void play();
 bool active()const{return worker_!=nullptr;}
signals:
 void requested();
 void delivered(bool success);
private:
 QList<QThread *> threads_;
 quint64 generation_=0;
 QObject *worker_=nullptr;
};
