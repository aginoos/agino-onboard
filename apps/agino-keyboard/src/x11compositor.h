// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
bool x11CompositeAvailable();
class X11CompositorWatcher:public QObject,public QAbstractNativeEventFilter {
 Q_OBJECT
public:
 explicit X11CompositorWatcher(QObject *parent=nullptr);
 ~X11CompositorWatcher()override;
 bool nativeEventFilter(const QByteArray &,void *,qintptr *)override;
signals:
 void changed();
private:
 quint32 selection_=0;
 int eventType_=-1;
};
