// SPDX-License-Identifier: GPL-3.0-or-later
#include "nativepainting.h"
#include <memory>
#include <vector>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QMatrix4x4>
#include <QPainterPath>
#include <QTextLayout>
#include <cmath>

namespace {
// Check in physical device coordinates: a scanline band's painter already
// carries both the output DPR and the negative global band offset. Keep a
// small guard for glyph bearings, antialiasing and aligned rectangle edges.
bool touchesDevice(const QPainter &p,QRectF rect)
{
    if(!p.isActive()||rect.isEmpty())return false;
    const qreal guard=2*p.device()->devicePixelRatioF();
    return p.deviceTransform().mapRect(rect).adjusted(-guard,-guard,guard,guard)
        .intersects(QRectF(0,0,p.device()->width(),p.device()->height()));
}
// Independent QtGui implementation of Qt 6.8's software Rectangle raster
// contract (QSGSoftwareInternalRectangleNode): aligned local sizes, a small
// antialiased corner atlas, nearest-neighbour corner blits and unaliased fills.
// Reference: qtdeclarative/src/quick/scenegraph/adaptations/software/
// qsgsoftwareinternalrectanglenode.cpp (The Qt Company, GPL-3.0-only).
void softwareRectangle(QPainter &p,const QRectF &rect,QColor color,qreal requestedRadius,QColor border,qreal borderWidth)
{
    if(rect.isEmpty()||!touchesDevice(p,rect))return;
    const qreal w=std::ceil(rect.width()),h=std::ceil(rect.height());
    const int radius=int(std::floor(qMin(qMin(w,h)/2,requestedRadius)));
    const qreal bw=qMax(qreal(0),qreal(qRound(borderWidth*p.device()->devicePixelRatioF()))/p.device()->devicePixelRatioF());
    struct Corner { QColor color,border; int radius; qreal dpr,bw; QImage image; };
    static thread_local QVector<Corner> corners;
    const qreal dpr=p.device()->devicePixelRatioF();
    const QImage *image=nullptr;
    if(radius>0) {
        for(const Corner &c:corners) if(c.color==color&&c.border==border&&c.radius==radius&&c.bw==bw&&qFuzzyCompare(c.dpr,dpr)) {image=&c.image;break;}
        if(!image) {
            if(corners.size()>=32) corners.clear();
            QImage atlas(QSize(qRound(radius*2*dpr),qRound(radius*2*dpr)),QImage::Format_ARGB32_Premultiplied);
            atlas.setDevicePixelRatio(dpr);atlas.fill(Qt::transparent);
            QPainter cp(&atlas);cp.setRenderHint(QPainter::Antialiasing);cp.setCompositionMode(QPainter::CompositionMode_Source);
            cp.setPen(Qt::NoPen);
            if(bw>0){cp.setBrush(border);cp.drawRoundedRect(QRectF(0,0,radius*2,radius*2),radius,radius);}
            if(radius>bw){cp.setBrush(color);cp.drawRoundedRect(QRectF(bw,bw,radius*2-2*bw,radius*2-2*bw),radius,radius);}
            cp.end();corners.append({color,border,radius,dpr,bw,atlas});image=&corners.last().image;
        }
    }
    const QTransform baseDevice=p.deviceTransform();
    const bool stableBandFills=p.worldTransform().dy()<0&&!qFuzzyCompare(dpr,qreal(qRound(dpr)));
    // QSG item translations pass through QMatrix4x4 float storage. This
    // matters when a 130/3 segment origin lands just below a physical pixel.
    const QPointF origin(float(rect.x()),float(rect.y()));
    p.save();p.translate(origin);
    p.setRenderHint(QPainter::Antialiasing,false);p.setRenderHint(QPainter::SmoothPixmapTransform,false);
    auto fill=[&](QRectF box,QColor ink){
        if(box.width()<=0||box.height()<=0)return;
        if(!stableBandFills){p.fillRect(box,ink);return;}
        // Round global physical endpoints before subtracting an integer band
        // offset. Cancellation after a negative band translation otherwise
        // changes half-pixel fills (e.g. 293.5 becomes 5.49999999999997).
        const qreal ix=std::floor(baseDevice.dx()),iy=std::floor(baseDevice.dy());
        const QTransform stable(baseDevice.m11(),baseDevice.m12(),baseDevice.m21(),baseDevice.m22(),baseDevice.dx()-ix,baseDevice.dy()-iy);
        const QRectF global=box.translated(origin);
        const QPointF a=stable.map(global.topLeft()),b=stable.map(global.bottomRight());
        const QRect pixels(QPoint(qRound(a.x())+int(ix),qRound(a.y())+int(iy)),QSize(qRound(b.x())-qRound(a.x()),qRound(b.y())-qRound(a.y())));
        p.save();p.resetTransform();p.scale(1/dpr,1/dpr);p.fillRect(pixels,ink);p.restore();
    };
    if(bw>0){
        const qreal bx=qMin(bw,w/2),by=qMin(bw,h/2);
        if(bx>radius){
            fill(QRectF(radius,0,w-2*radius,radius),border);
            fill(QRectF(bx,radius,w-2*bx,by-radius),border);
            fill(QRectF(radius,h-radius,w-2*radius,radius),border);
            fill(QRectF(bx,h-by,w-2*bx,by-radius),border);
        }else{
            fill(QRectF(radius,0,w-2*radius,by),border);
            fill(QRectF(radius,h-by,w-2*radius,by),border);
        }
        fill(QRectF(0,radius,bx,h-2*radius),border);
        fill(QRectF(QPointF(w-bx,radius),QPointF(w,h-radius)),border);
    }
    if(image){
        // Fractional source halves can extend one pixel beyond an odd-sized
        // atlas. QPainter's device-edge clipping then changes nearest-neighbour
        // source rounding. Rasterize just that corner with a two-pixel guard,
        // on the global physical pixel grid, before clipping its integer blit.
        auto corner=[&](QRectF target,const QImage &atlas,QRectF source){
            if(!touchesDevice(p,target))return;
            const QTransform t=p.deviceTransform();
            if(qFuzzyCompare(dpr,qreal(qRound(dpr)))||QRectF(0,0,p.device()->width(),p.device()->height()).contains(t.mapRect(target))){
                p.drawImage(target,atlas,source);return;
            }
            const QRect bounds=t.mapRect(target).toAlignedRect().adjusted(-2,-2,2,2);
            QImage patch(bounds.size(),QImage::Format_ARGB32_Premultiplied);patch.fill(Qt::transparent);
            QPainter cp(&patch);
            cp.setWorldTransform(QTransform(t.m11(),t.m12(),t.m21(),t.m22(),t.dx()-bounds.left(),t.dy()-bounds.top()));
            cp.drawImage(target,atlas,source);cp.end();
            p.save();p.resetTransform();p.scale(1/dpr,1/dpr);p.drawImage(bounds.topLeft(),patch);p.restore();
        };
        const qreal sr=qRound(radius*dpr);
        if(radius*2>=w&&radius*2>=h) {
            corner(QRectF(0,0,w,h),*image,QRectF(QPointF(0,0),QSizeF(image->size())));
        } else {
        corner(QRectF(0,0,radius,radius),*image,QRectF(0,0,sr,sr));
        corner(QRectF(w-radius,0,radius,radius),*image,QRectF(sr,0,sr,sr));
        corner(QRectF(0,h-radius,radius,radius),*image,QRectF(0,sr,sr,sr));
        corner(QRectF(w-radius,h-radius,radius,radius),*image,QRectF(sr,sr,sr,sr));
        }
    }
    QRectF interior=QRectF(0,0,w,h).marginsRemoved(QMarginsF(bw,bw,bw,bw));
    interior.setWidth(qMax(qreal(0),interior.width()));interior.setHeight(qMax(qreal(0),interior.height()));
    const qreal r=qMax(qreal(0),radius-bw);
    if(r>0){
        fill(QRectF(QPointF(interior.x()+r,interior.y()),QPointF(interior.x()+interior.width()-r,interior.y()+interior.height())),color);
        fill(QRectF(QPointF(interior.x(),interior.y()+r),QPointF(interior.x()+r,interior.y()+interior.height()-r)),color);
        fill(QRectF(QPointF(interior.x()+interior.width()-r,interior.y()+r),QPointF(interior.x()+interior.width(),interior.y()+interior.height()-r)),color);
    }else fill(interior,color);
    p.restore();
}
struct LayoutKey { QString label, name; quint32 sym; qreal units; };
LayoutKey entry(const QString &label, quint32 sym, qreal units=1, const QString &name={})
{ return {label,name.isEmpty()?label:name,sym,units}; }
void letters(QVector<LayoutKey> &row,const char *s)
{ for (;*s;++s) row.append(entry(QString(QChar(*s)).toUpper(),quint32(*s))); }
QString shifted(quint32 sym)
{
    const QString plain=QStringLiteral("`1234567890-=[]\\;',./");
    const QString upper=QStringLiteral("~!@#$%^&*()_+{}|:\"<>?");
    const int i=plain.indexOf(QChar(ushort(sym)));
    return sym<128&&i>=0?QString(upper.at(i)):QString();
}
// QTextLayout is also the shaping engine used by Qt Quick Text. Keep fractional
// advances; QFontMetrics::boundingRect introduces ink-bearing offsets here.
QSizeF layoutText(QTextLayout &layout,qreal width,bool wrap,Qt::Alignment horizontal=Qt::AlignLeft)
{
    QTextOption option;
    option.setWrapMode(wrap?QTextOption::WrapAtWordBoundaryOrAnywhere:QTextOption::NoWrap);
    option.setAlignment(horizontal);
    option.setUseDesignMetrics(true);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal height=0, naturalWidth=0;
    while (true) {
        QTextLine line=layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(width>0?width:1e9);
        line.setPosition(QPointF(0,height));
        height+=line.height();
        naturalWidth=qMax(naturalWidth,line.naturalTextWidth());
    }
    layout.endLayout();
    return QSizeF(naturalWidth,std::ceil(height));
}
// Bound shaped runs by both entry count and a conservative UTF-16/glyph/line
// storage estimate; font engines remain shared with Qt. Cleared on panel/font
// lifecycle changes through clearTextCache(). Geometry uses implicit sharing.
struct ShapedText {QString value;QFont font;qreal width;bool wrap;Qt::Alignment horizontal;int dpiX,dpiY;QSizeF extent;std::unique_ptr<QTextLayout> layout;QVector<QPointF> glyphLinePositions;QList<QGlyphRun> glyphRuns;QVector<QRectF> glyphBounds;qsizetype cost;};
struct ShapeCache {std::vector<std::shared_ptr<ShapedText>> entries;qsizetype bytes=0;};
ShapeCache &shapeCache(){static thread_local ShapeCache c;return c;}
std::shared_ptr<ShapedText> shapedText(const QString &value,const QFont &font,qreal width,bool wrap,QPaintDevice *device=nullptr,Qt::Alignment horizontal=Qt::AlignLeft,bool explicitAlignment=false)
{
 auto &cache=shapeCache();const int dx=device?device->logicalDpiX():0,dy=device?device->logicalDpiY():0;const qreal effectiveWidth=(wrap||explicitAlignment)?width:0;
 for(const auto &entry:cache.entries)if(entry->value==value&&entry->font==font&&entry->width==effectiveWidth&&entry->wrap==wrap&&entry->horizontal==horizontal&&entry->dpiX==dx&&entry->dpiY==dy)return entry;
 auto entry=std::make_shared<ShapedText>();entry->value=value;entry->font=font;entry->width=effectiveWidth;entry->wrap=wrap;entry->horizontal=horizontal;entry->dpiX=dx;entry->dpiY=dy;
 entry->layout=std::make_unique<QTextLayout>(value,device?QFont(font,device):font);entry->layout->setCacheEnabled(true);entry->extent=layoutText(*entry->layout,effectiveWidth,wrap,horizontal);entry->cost=512+value.size()*48+entry->layout->lineCount()*128;
 if(entry->cost<=131072){while(!cache.entries.empty()&&(cache.bytes+entry->cost>131072||cache.entries.size()>=128)){cache.bytes-=cache.entries.front()->cost;cache.entries.erase(cache.entries.begin());}cache.bytes+=entry->cost;cache.entries.push_back(entry);}return entry;
}

}

void NativePainting::clearTextCache()
{
    auto &cache=shapeCache();cache.entries.clear();cache.bytes=0;
}

void NativePainting::rectangle(QPainter &p,QRectF rect,QColor fill,qreal radius,QColor border,qreal borderWidth)
{
    softwareRectangle(p,rect,fill,radius,border,borderWidth);
}

QVector<NativeKey> NativePainting::keys(QSizeF size,bool compact,bool functionRow)
{
    static thread_local QSizeF cachedSize;static thread_local bool cachedCompact=false,cachedFunction=false;static thread_local QVector<NativeKey> cached;
    if(cachedSize==size&&cachedCompact==compact&&cachedFunction==functionRow&&!cached.isEmpty())return cached;
    QVector<QVector<LayoutKey>> rows;
    if (functionRow) {
        QVector<LayoutKey> row{entry(QStringLiteral("Esc"),0xff1b)};
        for(int i=1;i<=12;++i) row.append(entry(QStringLiteral("F%1").arg(i),0xffbd+i));
        row.append(entry(QStringLiteral("Del"),0xffff,2)); rows.append(row);
    }
    QVector<LayoutKey> row;
    if(!functionRow) row.append(entry(QStringLiteral("Esc"),0xff1b));
    row.append(entry(QStringLiteral("`"),96));
    for(const QChar c:QStringLiteral("1234567890-=")) row.append(entry(QString(c),c.unicode()));
    row.append(entry(QStringLiteral("⌫"),0xff08,functionRow?2:1,QStringLiteral("Backspace")));
    rows.append(row);
    row={entry(QStringLiteral("Tab"),0xff09)}; letters(row,"qwertyuiop");
    row.append(entry(QStringLiteral("["),91)); row.append(entry(QStringLiteral("]"),93));
    row.append(entry(QStringLiteral("\\"),92,functionRow?2:1));
    if(!functionRow) row.append(entry(QStringLiteral("Del"),0xffff));
    rows.append(row);
    row={entry(QStringLiteral("🌐"),0,1.75,QStringLiteral("Language"))}; letters(row,"asdfghjkl");
    row.append(entry(QStringLiteral(";"),59)); row.append(entry(QStringLiteral("'"),39));
    row.append(entry(QStringLiteral("Enter"),0xff0d,2.25)); rows.append(row);
    row={entry(QStringLiteral("Shift"),0xffe1,2.5,QStringLiteral("Shift-L"))}; letters(row,"zxcvbnm");
    row.append(entry(QStringLiteral(","),44)); row.append(entry(QStringLiteral("."),46)); row.append(entry(QStringLiteral("/"),47));
    row.append(entry(QStringLiteral("Shift"),0xffe2,2.5,QStringLiteral("Shift-R"))); rows.append(row);
    rows.append({entry(QStringLiteral("Ctrl"),0xffe3,1.5,QStringLiteral("Ctrl-L")),entry(QStringLiteral("Super"),0xffeb),
                 entry(QStringLiteral("Alt"),0xffe9,1,QStringLiteral("Alt-L")),entry(QCoreApplication::translate("Keyboard","Space"),32,7.5,QStringLiteral("Space")),
                 entry(QStringLiteral("Alt"),0xffea,1,QStringLiteral("Alt-R")),entry(QStringLiteral("Ctrl"),0xffe4,2,QStringLiteral("Ctrl-R")),
                 entry(QStringLiteral("⌄"),0,1,QStringLiteral("Hide"))});
    const qreal margin=compact?8:12,gap=compact?2:6,rowGap=compact?2:8,height=compact?32:48;
    const qreal pitch=(size.width()-2*margin+gap)/15;
    const qreal top=compact?36:56;
    QVector<NativeKey> result;
    for(int r=0;r<rows.size();++r) {
        qreal units=0;
        for(const LayoutKey &k:rows[r]) {
            NativeKey key;
            key.label=k.label; key.secondary=shifted(k.sym);
            if(k.name==QStringLiteral("Language")||k.name==QStringLiteral("Hide")) key.action=k.name;
            key.control.id=QStringLiteral("key-")+k.name;
            key.control.name=k.name==QStringLiteral("Language")?QCoreApplication::translate("Keyboard","Switch input method"):
                k.name==QStringLiteral("Hide")?QCoreApplication::translate("Keyboard","Hide screen keyboard"):
                k.sym==0xff08?QCoreApplication::translate("Keyboard","Backspace"):k.label;
            key.control.action=key.action;
            key.control.sym=k.sym;
            key.control.checkable=k.sym==0xffe1||k.sym==0xffe2;
            key.control.rect=QRectF(margin+units*pitch,top+r*(height+rowGap),k.units*pitch-gap,height);
            result.append(key); units+=k.units;
        }
    }
    cachedSize=size;cachedCompact=compact;cachedFunction=functionRow;cached=result;return result;
}

QSizeF NativePainting::textSize(const QString &value,const QFont &font,qreal width,bool wrap)
{
    return shapedText(value,font,width,wrap)->extent;
}

void NativePainting::text(QPainter &p,QRectF rect,const QString &value,QFont font,QColor color,Qt::Alignment alignment,bool wrap,bool elide,bool explicitAlignment)
{
    if(value.isEmpty()||rect.isEmpty()||(!wrap&&!touchesDevice(p,rect))) return;
    const QString content=elide?QFontMetricsF(font).elidedText(value,Qt::ElideRight,rect.width()):value;
    const auto shaped=shapedText(content,font,rect.width(),wrap,p.device(),explicitAlignment?(alignment&Qt::AlignHorizontal_Mask):Qt::AlignLeft,explicitAlignment);
    QTextLayout &layout=*shaped->layout;
    const QSizeF extent=shaped->extent;
    qreal y=rect.top();
    // Match QQuickAnchors::centerIn alignWhenCentered: each half extent is
    // rounded independently, before the fractional parent position is added.
    if(alignment&Qt::AlignVCenter) y+=explicitAlignment?(rect.height()-extent.height())/2:std::round(rect.height()/2)-std::round(extent.height()/2);
    else if(alignment&Qt::AlignBottom) y+=rect.height()-extent.height();
    // Individual lines are positioned instead of aligning the layout to an
    // enormous unconstrained line width (which loses subpixel precision).
    for(int i=0;i<layout.lineCount();++i) {
        QTextLine line=layout.lineAt(i);
        qreal x=0;
        if(!explicitAlignment&&(alignment&Qt::AlignHCenter)) x=std::round(rect.width()/2)-std::round(line.naturalTextWidth()/2);
        else if(!explicitAlignment&&(alignment&Qt::AlignRight)) x=rect.width()-line.naturalTextWidth();
        line.setPosition(QPointF(x,line.y()));
    }
    QVector<QPointF> positions;
    positions.reserve(layout.lineCount());
    for(int i=0;i<layout.lineCount();++i)positions.append(layout.lineAt(i).position());
    if(shaped->glyphLinePositions!=positions){
        shaped->glyphLinePositions=positions;shaped->glyphRuns.clear();shaped->glyphBounds.clear();
        // QQuickTextNodeEngine merges all runs of the same font before its
        // glyph node computes ink clipping, including separate wrapped lines
        // and bidi fragments. Clipping each fragment separately is too tight.
        for(const auto &run:layout.glyphRuns()){
            int index=-1;
            for(int i=0;i<shaped->glyphRuns.size();++i)if(shaped->glyphRuns[i].rawFont()==run.rawFont()){index=i;break;}
            if(index<0){shaped->glyphRuns.append(run);continue;}
            auto &merged=shaped->glyphRuns[index];auto glyphs=merged.glyphIndexes();glyphs+=run.glyphIndexes();
            auto points=merged.positions();points+=run.positions();merged.setGlyphIndexes(glyphs);merged.setPositions(points);
        }
        for(const auto &run:shaped->glyphRuns){
            QRectF bounds;const auto glyphs=run.glyphIndexes();const auto points=run.positions();
            for(int i=0;i<qMin(glyphs.size(),points.size());++i){
                const QRectF glyph=run.rawFont().boundingRect(glyphs[i]).translated(points[i]);
                bounds=i?bounds.united(glyph):glyph;
            }
            shaped->glyphBounds.append(bounds);
        }
    }
    p.save();p.setPen(QColor::fromRgba(color.rgba()));
    for(int i=0;i<shaped->glyphRuns.size();++i){
        // The software scene graph clips a glyph node to its unscaled ink
        // bounds, aligned in logical coordinates BEFORE applying output DPR.
        // Omitting this admits extra hinted AA columns at fractional scales.
        p.save();const QTransform transform=p.worldTransform();p.resetTransform();
        p.setClipRect(transform.mapRect(shaped->glyphBounds[i].translated(rect.left(),y)).toAlignedRect(),Qt::IntersectClip);
        p.setWorldTransform(transform);p.drawGlyphRun(QPointF(rect.left(),y),shaped->glyphRuns[i]);p.restore();
    }
    p.restore();
}

static void vectorIcon(QPainter &p,QRectF rect,const QString &action,QColor color,qreal scale,qreal stroke)
{
    p.save();p.setRenderHint(QPainter::Antialiasing);
    p.translate(rect.center());p.scale(scale,scale);p.translate(-12,-12);
    QPen pen(color,stroke,Qt::SolidLine,Qt::RoundCap,Qt::MiterJoin);
    QPainterPath path;
    if(action==QStringLiteral("Language")) {
        path.addEllipse(QRectF(2,2,20,20));
        path.moveTo(2,12);path.lineTo(22,12);path.moveTo(4,7);path.lineTo(20,7);path.moveTo(4,17);path.lineTo(20,17);
        path.moveTo(12,2);path.cubicTo(5,7,5,17,12,22);path.moveTo(12,2);path.cubicTo(19,7,19,17,12,22);
    } else if(action==QStringLiteral("Settings")) {
        constexpr qreal pi=3.14159265358979323846;
        for(int n=0;n<32;++n) {
            const qreal angle=(n-0.5)*pi/16,r=(n%4==1||n%4==2)?10:8;
            const QPointF point(12+r*std::cos(angle),12+r*std::sin(angle));
            if(n==0)path.moveTo(point);else path.lineTo(point);
        }
        path.closeSubpath();path.moveTo(16,12);path.arcTo(QRectF(8,8,8,8),0,-360);
    } else if(action==QStringLiteral("Backspace")) {
        pen.setJoinStyle(Qt::RoundJoin);
        path.moveTo(8,5);path.lineTo(22,5);path.lineTo(22,19);path.lineTo(8,19);path.lineTo(2,12);path.closeSubpath();
        path.moveTo(11,9);path.lineTo(17,15);path.moveTo(17,9);path.lineTo(11,15);
    } else if(action==QStringLiteral("Expand")||action==QStringLiteral("Collapse")) {
        // CandidateBar's 14px Canvas is centred inside the icon slot.
        path.moveTo(8,10);path.lineTo(12,14);path.lineTo(16,10);
    } else {
        path.addRect(QRectF(2,2,20,12));
        for(int y=5;y<=11;y+=3)for(int x=5;x<=19;x+=3){path.moveTo(x,y);path.lineTo(x+0.5,y);}
        path.moveTo(8,19);path.lineTo(12,22);path.lineTo(16,19);
    }
    p.setPen(pen);p.setBrush(Qt::NoBrush);p.drawPath(path);p.restore();
}

void NativePainting::icon(QPainter &p,QRectF rect,const QString &action,QColor color,qreal scale,qreal stroke)
{
    if(!touchesDevice(p,QRectF(rect.center().x()-12*scale,rect.center().y()-12*scale,24*scale,24*scale)))return;
    color=QColor::fromRgba(color.rgba());
    // QML Canvas first paints a 24px backing image at the output DPR; Loader
    // scaling then filters that image. Scaling the vector path directly gives
    // different strokes in compact mode and for the settings toolbar icon.
    struct Raster {QString action;QColor color;qreal dpr,stroke;QImage image;};
    static thread_local QVector<Raster> cache;
    const qreal dpr=p.device()->devicePixelRatioF();
    const qreal extent=(action==QStringLiteral("Expand")||action==QStringLiteral("Collapse"))?14:24;
    const QImage *image=nullptr;
    for(const Raster &r:cache) if(r.action==action&&r.color==color&&r.dpr==dpr&&r.stroke==stroke){image=&r.image;break;}
    if(!image){
        if(cache.size()>=16)cache.clear();
        QImage raster(qRound(extent*dpr),qRound(extent*dpr),QImage::Format_ARGB32_Premultiplied);
        raster.setDevicePixelRatio(dpr);raster.fill(Qt::transparent);
        QPainter ip(&raster);vectorIcon(ip,QRectF((extent-24)/2,(extent-24)/2,24,24),action,color,1,stroke);ip.end();
        cache.append({action,color,dpr,stroke,raster});image=&cache.last().image;
    }
    p.save();p.setRenderHint(QPainter::Antialiasing,false);p.setRenderHint(QPainter::SmoothPixmapTransform,true);
    // QSG composes item scale about its centre in float precision. An exact
    // qreal target rectangle changes a few bilinear weights for scale 0.58.
    QMatrix4x4 item;item.translate(float(rect.center().x()),float(rect.center().y()));
    item.scale(float(scale));item.translate(float(-extent/2),float(-extent/2));
    p.setWorldTransform(item.toTransform(),true);
    // A 14px Canvas at 1.25x/1.75x has a rounded 18px/25px backing image;
    // keep its logical target 14px instead of using image.width()/DPR.
    p.drawImage(QRectF(0,0,extent,extent),*image,QRectF(0,0,image->width(),image->height()));p.restore();
}

void NativePainting::key(QPainter &p,const NativeKey &k,const NativeStyle &style,quint32 modifiers,bool held,bool enabled,const QString &status)
{
    if(!touchesDevice(p,k.control.rect))return;
    const bool shift=k.control.sym==0xffe1||k.control.sym==0xffe2;
    const bool active=held||(shift&&(modifiers&3));
    const bool locked=shift&&(modifiers&2);
    QRectF rect=k.control.rect;
    // QML composes the key-area and key-item translations in QMatrix4x4's
    // float precision. Preserve that rounding at physical half-pixel edges.
    const qreal areaMargin=style.compact?8:12;
    rect.moveLeft(qreal(float(float(areaMargin)+float(rect.left()-areaMargin))));
    const QColor ink=style.palette.color(active?QPalette::HighlightedText:QPalette::ButtonText);
    p.save();p.setOpacity(p.opacity()*(enabled?1:0.7));p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);p.setBrush(style.palette.color(active?QPalette::Highlight:QPalette::Button));
    rectangle(p,rect,style.palette.color(active?QPalette::Highlight:QPalette::Button),style.compact?3:7);
    if(k.action==QStringLiteral("Language")||k.action==QStringLiteral("Hide")||k.action==QStringLiteral("Settings")||k.control.sym==0xff08) {
        // QML icons use held alone, unlike the label's shift-active colour.
        const QColor iconInk=style.palette.color(held?QPalette::HighlightedText:QPalette::ButtonText);
        const QRectF iconRect(rect.left()+std::round(rect.width()/2)-12,rect.top()+std::round(rect.height()/2)-12,24,24);
        icon(p,iconRect,k.control.sym==0xff08?QStringLiteral("Backspace"):k.action,iconInk,style.compact?0.75:1);
    } else {
        const QString label=locked?QStringLiteral("↑ Shift"):!status.isEmpty()?status:(!k.secondary.isEmpty()&&(modifiers&3))?k.secondary:k.label;
        QFont font=style.font;font.setPixelSize(k.label.size()>3?(style.compact?12:14):(style.compact?14:18));
        p.save();p.setOpacity(p.opacity()*(enabled?1:0.4));text(p,rect,label,font,ink);p.restore();
    }
    if(!k.secondary.isEmpty()) {
        const qreal margin=style.compact?3:5;
        QFont font=style.font;font.setPixelSize(style.compact?10:12);
        p.save();p.setOpacity(p.opacity()*(enabled?0.9:0.4));
        // A right-anchored QML Text moves its item origin. Placing this offset
        // in QTextLine instead rounds it to QFixed's 1/64px and changes the
        // hinted glyph phase (notably the dollar symbol at 2x).
        const QSizeF extent=textSize(k.secondary,font);
        const qreal x=qreal(float(float(rect.left())+float(rect.width()-margin-extent.width())));
        text(p,QRectF(x,rect.top()+margin,extent.width(),extent.height()),k.secondary,font,ink,Qt::AlignTop|Qt::AlignLeft);p.restore();
    }
    p.restore();
}

QStringList NativePainting::alternatives(quint32 sym)
{
    switch(sym) {
    case 97:return {QStringLiteral("á"),QStringLiteral("à"),QStringLiteral("â"),QStringLiteral("ä")};
    case 101:return {QStringLiteral("é"),QStringLiteral("è"),QStringLiteral("ê"),QStringLiteral("ë")};
    case 105:return {QStringLiteral("í"),QStringLiteral("ì"),QStringLiteral("î"),QStringLiteral("ï")};
    case 111:return {QStringLiteral("ó"),QStringLiteral("ò"),QStringLiteral("ô"),QStringLiteral("ö")};
    case 117:return {QStringLiteral("ú"),QStringLiteral("ù"),QStringLiteral("û"),QStringLiteral("ü")};
    case 110:return {QStringLiteral("ñ")};case 99:return {QStringLiteral("ç")};
    case 46:return {QStringLiteral("…")};case 45:return {QStringLiteral("—"),QStringLiteral("–")};default:return {};
    }
}
