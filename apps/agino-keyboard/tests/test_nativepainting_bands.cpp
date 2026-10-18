// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/nativepainting.h"
#include <QGuiApplication>
#include <QImage>
#include <QDebug>
#include <cstring>
#include <numeric>

// Compare the exact physical pixels of a complete backing image with a stream
// whose transient scanline image never exceeds 64KiB. Integer logical origins
// prevent fractional glyph/corner rounding from changing between bands.
int main(int argc,char **argv)
{
    QGuiApplication app(argc,argv);
    for(qreal dpr:{1.,1.25,1.5,1.75,2.})for(bool compact:{false,true})for(bool functionRow:{false,true}) {
        const QSizeF size(934,compact?(functionRow?246:212):(functionRow?400:344));
        const int width=qRound(size.width()*dpr),height=qRound(size.height()*dpr);
        const auto keys=NativePainting::keys(size,compact,functionRow);
        NativeStyle style;style.font=app.font();style.palette=app.palette();style.compact=compact;style.functionRow=functionRow;
        auto render=[&](QPainter &p){
            NativePainting::rectangle(p,QRectF(QPointF(),size),style.palette.color(QPalette::Window),compact?8:12,QColor(218,218,218),1);
            for(int i=0;i<keys.size();++i)NativePainting::key(p,keys[i],style,2,i%13==0,i%17!=0);
            QFont font=style.font;font.setPixelSize(compact?13:14);
            NativePainting::text(p,QRectF(35,0,500,compact?32:48),QStringLiteral("候选词测试 English"),font,Qt::black);
            NativePainting::icon(p,QRectF(860,0,48,48),QStringLiteral("Settings"),Qt::black,.75);
            NativePainting::icon(p,QRectF(904,0,24,48),QStringLiteral("Expand"),Qt::black);
        };
        QImage full(width,height,QImage::Format_ARGB32_Premultiplied);full.setDevicePixelRatio(dpr);full.fill(Qt::transparent);
        {QPainter p(&full);render(p);}
        QImage stream(width,height,full.format());stream.fill(Qt::transparent);
        const int scaleNumerator=qRound(dpr*4),quantum=scaleNumerator/std::gcd(scaleNumerator,4);
        const int rows=(65536/(width*4)/quantum)*quantum;
        if(rows<1){qCritical()<<"No aligned scanline band fits"<<dpr;return 1;}
        for(int top=0;top<height;top+=rows){
            QImage band(width,qMin(rows,height-top),full.format());band.setDevicePixelRatio(dpr);band.fill(Qt::transparent);
            if(band.sizeInBytes()>65536){qCritical()<<"Scanline budget exceeded";return 1;}
            {QPainter p(&band);p.translate(0,-top/dpr);render(p);}
            for(int row=0;row<band.height();++row)std::memcpy(stream.scanLine(top+row),band.constScanLine(row),width*4);
        }
        if(stream!=full){qCritical()<<"Scanline pixels differ"<<dpr<<compact<<functionRow;return 1;}
    }
    // Regression for an aliased dark card's half-pixel bottom at 1.5x. The
    // global endpoint rounds to row294; subtracting a band offset first used
    // to change it to 293 through floating-point cancellation.
    for(qreal dpr:{1.,1.25,1.5,1.75,2.}){
        const QSizeF size(524,242);const int width=qRound(size.width()*dpr),height=qRound(size.height()*dpr);
        auto render=[&](QPainter &p){
            for(qreal x:{9.,266.})NativePainting::rectangle(p,QRectF(x,37,249,160),QColor(40,40,40),9,QColor(255,255,255,20),1);
            // Odd physical diameter at 1.25x/1.75x uses one whole circle atlas.
            NativePainting::rectangle(p,QRectF(453,177,12,12),QColor(0,129,255),6);
            p.save();p.translate(1,1);
            NativePainting::rectangle(p,QRectF(268,140,180,32),Qt::transparent,6,QColor(0,0,0,31),1);
            p.restore();
            p.save();p.translate(1,1);p.setOpacity(.12);
            NativePainting::rectangle(p,QRectF(119+130./3+2,59,130./3-4,22),QColor(0,129,255),5);
            p.restore();
            QFont font=app.font();font.setPixelSize(18);
            NativePainting::text(p,QRectF(100,100,48,24),QStringLiteral("longUnbrokenURL0123456789中文🙂 abc مرحبا 123 אבגדה"),font,Qt::white,Qt::AlignCenter,true,false,true);
        };
        QImage full(width,height,QImage::Format_ARGB32_Premultiplied);full.setDevicePixelRatio(dpr);full.fill(QColor(37,37,37));
        {QPainter p(&full);render(p);}
        QImage stream(width,height,full.format());stream.fill(QColor(37,37,37));
        const int numerator=qRound(dpr*4),quantum=numerator/std::gcd(numerator,4),rows=(65536/(width*4)/quantum)*quantum;
        for(int top=0;top<height;top+=rows){
            QImage band(width,qMin(rows,height-top),full.format());band.setDevicePixelRatio(dpr);band.fill(QColor(37,37,37));
            {QPainter p(&band);p.translate(0,-top/dpr);render(p);}
            for(int row=0;row<band.height();++row)std::memcpy(stream.scanLine(top+row),band.constScanLine(row),width*4);
        }
        if(stream!=full){qCritical()<<"Dark card/overflowing wrapped text differs"<<dpr;return 1;}
    }
    auto dynamicFrame=[&](int variant){
        QImage image(450,150,QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(1.5);image.fill(Qt::white);
        NativeStyle style;style.font=app.font();style.font.setWeight(variant%2?QFont::Bold:QFont::Normal);style.palette=app.palette();
        QPainter p(&image);NativeKey key;key.label=QStringLiteral("Shift");key.control.sym=variant%2?0xffe1:32;key.control.rect=QRectF(23.4,5.5,140.6,48);
        NativePainting::key(p,key,style,quint32(variant%3),variant%4==0,variant%5!=0,QStringLiteral("输入状态 %1").arg(variant));
        QFont font=style.font;font.setPixelSize(11+variant%3);
        NativePainting::text(p,QRectF(0,55,200+variant%7,40),QStringLiteral("候选词字体变化 cache wrap %1").arg(variant),font,Qt::black,Qt::AlignCenter,true);
        p.end();return image;
    };
    // Verify that a cache hit after different held/lock/status/font/wrap states
    // equals a fresh shape, and that eviction/clearing cannot leave stale runs.
    for(int variant=0;variant<24;++variant){
        NativePainting::clearTextCache();const QImage fresh=dynamicFrame(variant);
        dynamicFrame(variant+1);
        if(dynamicFrame(variant)!=fresh){qCritical()<<"Stale shaped state"<<variant;return 1;}
    }
    NativePainting::clearTextCache();const QImage beforeEviction=dynamicFrame(7);
    for(int variant=1000;variant<1200;++variant)dynamicFrame(variant);
    if(dynamicFrame(7)!=beforeEviction){qCritical()<<"Evicted layout changed pixels";return 1;}
    NativePainting::clearTextCache();
    if(dynamicFrame(7)!=beforeEviction){qCritical()<<"Cache clear changed pixels";return 1;}
    auto changed=NativePainting::keys(QSizeF(934,344),false,false);changed[0].label=QStringLiteral("mutated consumer copy");
    if(NativePainting::keys(QSizeF(934,344),false,false)[0].label!=QStringLiteral("Esc")){qCritical()<<"Geometry cache was mutated by consumer";return 1;}
    NativePainting::clearTextCache();
    qInfo()<<"All 25 full-frame/64KiB-band comparisons and dynamic cache checks match exactly";
}
