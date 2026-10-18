// SPDX-License-Identifier: GPL-3.0-or-later
#include "nativeoutput.h"
#include <QImage>
#include <QGuiApplication>
#include <QSysInfo>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <algorithm>
#include <numeric>
class NativeX11Output::Impl {
public:
 Display *display=nullptr;GC gc=nullptr;WId window=0;QImage band;
 Impl(){if(QGuiApplication::platformName()=="xcb")display=XOpenDisplay(nullptr);}
 ~Impl(){if(display){if(gc)XFreeGC(display,gc);XCloseDisplay(display);}}
};
NativeX11Output::NativeX11Output():d(std::make_unique<Impl>()){}
NativeX11Output::~NativeX11Output()=default;
qsizetype NativeX11Output::retainedBytes()const{return d->band.sizeInBytes();}
bool NativeX11Output::paint(QWindow *window,const std::function<void(QPainter&,QSizeF)> &scene){
 if(!d->display||!window||!window->isExposed())return false;
 const auto wid=window->winId();XWindowAttributes attr{};if(!XGetWindowAttributes(d->display,wid,&attr))return false;
 if((attr.depth!=24&&attr.depth!=32)||!attr.visual||attr.visual->red_mask!=0xff0000||attr.visual->green_mask!=0x00ff00||attr.visual->blue_mask!=0x0000ff)return false;
 if(d->window!=wid){if(d->gc)XFreeGC(d->display,d->gc);d->gc=XCreateGC(d->display,wid,0,nullptr);d->window=wid;if(!d->gc)return false;}
 const qreal scale=window->devicePixelRatio();const int width=qRound(window->width()*scale),height=qRound(window->height()*scale);if(width<=0||height<=0)return true;
 const qsizetype stride=qsizetype(width)*4;
 const qsizetype requestLimit=qsizetype(XMaxRequestSize(d->display))*4-256;
 if(stride>requestLimit)return false;
 const qsizetype budget=qMax(stride,qMin(qsizetype(64*1024),requestLimit));
 // Keep each band origin at an integer logical coordinate on the standard
 // quarter-step desktop scales. A fractional logical origin changes Qt's
 // glyph/source rounding even when it maps back to an integer physical row.
 const int maxRows=qMax(1,int(budget/stride));
 const int numerator=qRound(scale*4);
 const int quantum=qFuzzyCompare(scale,numerator/4.)?numerator/std::gcd(numerator,4):1;
 const int rows=quantum<=maxRows?qMax(quantum,(maxRows/quantum)*quantum):maxRows;
 if(d->band.size()!=QSize(width,rows)){d->band=QImage(width,rows,QImage::Format_ARGB32_Premultiplied);}
 if(d->band.isNull())return false;
 d->band.setDevicePixelRatio(scale);
 XImage *image=XCreateImage(d->display,attr.visual,unsigned(attr.depth),ZPixmap,0,reinterpret_cast<char*>(d->band.bits()),unsigned(width),unsigned(rows),32,d->band.bytesPerLine());
 if(!image)return false;
 image->byte_order=QSysInfo::ByteOrder==QSysInfo::LittleEndian?LSBFirst:MSBFirst;
 for(int top=0;top<height;top+=rows){
  d->band.fill(Qt::transparent);QPainter painter(&d->band);painter.translate(0,-top/scale);scene(painter,window->size());painter.end();
  XPutImage(d->display,wid,d->gc,image,0,0,0,top,unsigned(width),unsigned(qMin(rows,height-top)));
 }
 image->data=nullptr;XDestroyImage(image);XFlush(d->display);return true;
}
