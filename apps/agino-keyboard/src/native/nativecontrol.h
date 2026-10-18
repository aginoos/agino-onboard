// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QRectF>
#include <QString>
#include <QVariant>
#include <QPalette>
#include <QFont>
#include <QAccessible>
struct NativeControl {
 QString id,name,action;
 QRectF rect;
 QAccessible::Role role=QAccessible::Button;
 bool enabled=true,checked=false,pressed=false,checkable=false;
 quint32 sym=0;
 int index=-1;
 quint64 context=0,generation=0;
 QVariant value;
};
struct NativeStyle {
 QPalette palette;
 QFont font;
 bool compact=false,functionRow=false,transparencySupported=true;
};
