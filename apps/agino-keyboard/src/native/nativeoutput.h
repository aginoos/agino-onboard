// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWindow>
#include <QPainter>
#include <functional>
#include <memory>
// X11 streams native premultiplied ARGB scanline bands to the real window.
// No client full-frame buffer or RGB/BGRA conversion image is retained.
class NativeX11Output final {
public:
 NativeX11Output();~NativeX11Output();
 bool paint(QWindow*,const std::function<void(QPainter&,QSizeF)> &);
 qsizetype retainedBytes()const;
private:class Impl;std::unique_ptr<Impl> d;
};
