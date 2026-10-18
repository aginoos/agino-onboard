// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nativecontrol.h"
#include <QWindow>
class NativeAccessibleHost {
public:virtual ~NativeAccessibleHost()=default;
 virtual QWindow *accessibleWindow()const=0;
 virtual QVector<NativeControl> accessibleControls()const=0;
 virtual bool invokeAccessible(const QString &id,const QString &action)=0;
};
void installNativeAccessibility();
// Diff the last published tree; ObjectReorder (the default) is emitted only
// when identities/order changed. Explicit show/hide lifecycle events pass through.
void notifyNativeAccessibility(QWindow*,QAccessible::Event event=QAccessible::ObjectReorder);
