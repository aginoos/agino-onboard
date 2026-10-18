// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nativecontrol.h"
#include <QObject>
#include <QPainter>
#include <memory>
class Preferences;class FcitxClient;
class NativeSettings final:public QObject {
 Q_OBJECT
public:
 NativeSettings(Preferences&,FcitxClient&,QObject *parent=nullptr);
 ~NativeSettings()override;
 QVector<NativeControl> paint(QPainter&,QSizeF,const NativeStyle&,const QStringList &screens,const QString &pressedId={});
 bool accepts(const NativeControl&)const;
 // Refresh value/state without painting; false means the control lease expired.
 bool currentControl(NativeControl&)const;
 bool activate(const NativeControl&,QPointF localPosition={});
 bool drag(const NativeControl&,QPointF localPosition);
 void scroll(qreal dy);
 void updateScreens(const QStringList&);
 void reset();
 bool pickerOpen()const;
signals:void changed();void finished();
private:
 class Impl;std::unique_ptr<Impl> d;
};
