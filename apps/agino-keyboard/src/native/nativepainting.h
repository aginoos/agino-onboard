// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nativecontrol.h"
#include <QPainter>
#include <QVector>
struct NativeKey {NativeControl control;QString label,secondary,action;};
namespace NativePainting {
void clearTextCache();
void rectangle(QPainter&,QRectF,QColor fill,qreal radius,QColor border={},qreal borderWidth=0);
QVector<NativeKey> keys(QSizeF size,bool compact,bool functionRow);
QSizeF textSize(const QString&,const QFont&,qreal width=0,bool wrap=false);
void text(QPainter&,QRectF,const QString&,QFont,QColor,Qt::Alignment alignment=Qt::AlignCenter,bool wrap=false,bool elide=false,bool explicitAlignment=false);
void icon(QPainter&,QRectF,const QString &action,QColor,qreal scale=1,qreal stroke=1.6);
void key(QPainter&,const NativeKey&,const NativeStyle&,quint32 modifiers,bool held,bool enabled,const QString &status={});
QStringList alternatives(quint32 sym);
}
