// SPDX-License-Identifier: GPL-3.0-or-later
#include "nativesettings.h"
#include "nativepainting.h"
#include "../preferences.h"
#include "../fcitxclient.h"
#include <QSet>
#include <QPainterPath>
#include <QImage>
#include <cmath>

namespace {
void paintChoiceChevron(QPainter &p,const QRectF &control,QColor ink)
{
 // QML Canvas paints into a DPR-sized texture using an 8-bit stroke color.
 ink=QColor::fromRgba(ink.rgba());
 struct Raster {QColor ink;qreal dpr;QImage image;};
 static thread_local QVector<Raster> cache;
 const qreal dpr=p.device()->devicePixelRatioF();
 const QImage *image=nullptr;
 for(const auto &entry:cache)if(entry.ink==ink&&entry.dpr==dpr){image=&entry.image;break;}
 if(!image){
  if(cache.size()>=8)cache.clear();
  QImage raster(qRound(10*dpr),qRound(10*dpr),QImage::Format_ARGB32_Premultiplied);
  raster.setDevicePixelRatio(dpr);raster.fill(Qt::transparent);
  QPainter painter(&raster);painter.setRenderHint(QPainter::Antialiasing);
  QPainterPath path;path.moveTo(1,3);path.lineTo(5,7);path.lineTo(9,3);
  painter.strokePath(path,QPen(ink,1.4,Qt::SolidLine,Qt::FlatCap,Qt::MiterJoin));painter.end();
  cache.append({ink,dpr,std::move(raster)});image=&cache.last().image;
 }
 // Scene-graph image blits do not antialias the destination rectangle.
 p.save();p.setRenderHint(QPainter::Antialiasing,false);p.setRenderHint(QPainter::SmoothPixmapTransform);
 p.drawImage(QRectF(control.right()-20,control.center().y()-5,10,10),*image);p.restore();
}
}

class NativeSettings::Impl {
public:
 NativeSettings *q; Preferences &prefs; FcitxClient &input;
 QString picker,trigger; QSet<QString> modifiers;
 QStringList screens; QVector<NativeControl> controls;
 qreal offset=0,maximum=0; quint64 generation=1; bool transparency=true;
 QRectF slider;
 Impl(NativeSettings *owner,Preferences &p,FcitxClient &i):q(owner),prefs(p),input(i){}
 QString imName(const QString &id)const {auto name=input.inputMethodNames().value(id).toString();return name.isEmpty()?id:name;}
 void close(){picker.clear();modifiers.clear();trigger.clear();offset=maximum=0;controls.clear();++generation;emit q->changed();}
 void open(const QString &kind){picker=kind;offset=maximum=0;controls.clear();++generation;
  if(kind=="shortcut"){auto parts=prefs.shortcut().split('+');trigger=parts.isEmpty()?QStringLiteral("K"):parts.takeLast();if(trigger.isEmpty())trigger="K";modifiers.clear();for(auto part:parts)modifiers.insert(part=="Meta"?"Super":part);}
  emit q->changed();
 }
 bool valid(const NativeControl &c)const {if(!c.enabled||c.generation!=generation)return false;for(const auto &now:controls)if(now.id==c.id&&now.enabled&&(!c.action.startsWith("choice/")||now.value==c.value))return true;return false;}
 void opacity(qreal x){if(transparency&&slider.width()>24)prefs.setOpacityPercent(qRound(50+50*qBound(qreal(0),(x-slider.x()-12)/(slider.width()-24),qreal(1))));}
};
NativeSettings::NativeSettings(Preferences &p,FcitxClient &i,QObject *parent):QObject(parent),d(std::make_unique<Impl>(this,p,i)){
 connect(&p,&Preferences::changed,this,&NativeSettings::changed);
 connect(&i,&FcitxClient::changed,this,&NativeSettings::changed);
}
NativeSettings::~NativeSettings()=default;
bool NativeSettings::pickerOpen()const{return !d->picker.isEmpty();}
void NativeSettings::reset(){d->close();}
void NativeSettings::updateScreens(const QStringList &names){
 if(names==d->screens)return;
 d->screens=names;d->controls.clear();++d->generation;emit changed();
}
void NativeSettings::scroll(qreal dy){if(!pickerOpen())return; // Positive moves content upwards, increasing the content offset.
 auto next=qBound(qreal(0),d->offset+dy,d->maximum);if(next!=d->offset){d->offset=next;d->controls.clear();emit changed();}}

QVector<NativeControl> NativeSettings::paint(QPainter &p,QSizeF size,const NativeStyle &style,const QStringList &screens,const QString &pressed){
 if(d->screens!=screens){d->screens=screens;++d->generation;}d->transparency=style.transparencySupported;d->controls.clear();
 // The QML SettingsPanel Loader is inset by one logical pixel.
 p.save();p.translate(1,1);size-=QSizeF(2,2);
 const bool compact=style.compact;const auto ink=style.palette.color(QPalette::WindowText),highlight=style.palette.color(QPalette::Highlight);
 const auto accent=style.palette.color(QPalette::Window).lightnessF()<.5?highlight.lighter(160):highlight;
 auto alpha=[](QColor c,qreal a){c.setAlphaF(a);return c;};
 auto faded=[](QColor c,qreal a){c.setAlpha(int(c.alpha()*a));return c;};
 auto rounded=[&](QRectF r,qreal radius,QColor color,QColor border=QColor()){NativePainting::rectangle(p,r,color,radius,border,border.isValid()?1:0);};
 auto shade=[&](QRectF r,qreal radius,QColor color,qreal opacity){p.save();p.setOpacity(p.opacity()*opacity);rounded(r,radius,color);p.restore();};
 auto label=[&](QRectF r,const QString &s,int pixels,QColor color,Qt::Alignment alignment=Qt::AlignVCenter|Qt::AlignLeft,bool elide=false,bool bold=false,bool anchored=false,qreal opacity=1){p.save();p.setOpacity(p.opacity()*opacity);auto f=style.font;f.setPixelSize(pixels);f.setWeight(bold?QFont::DemiBold:QFont::Normal);NativePainting::text(p,r,s,f,QColor::fromRgba(color.rgba()),alignment,false,elide,!anchored);p.restore();};
 QRectF viewport(0,0,size.width(),size.height());
 auto control=[&](const QString &id,const QString &name,QRectF rect,QAccessible::Role role=QAccessible::Button,QVariant value=QVariant(),bool checked=false,bool enabled=true,bool checkable=false){NativeControl c;c.id="settings/"+id;c.action=id;c.name=name;c.rect=rect.intersected(viewport);c.role=role;c.value=value;c.checked=checked;c.enabled=enabled;c.checkable=checkable;c.generation=d->generation;c.pressed=pressed==c.id;if(!c.rect.isEmpty())d->controls.append(c);return c;};
 auto button=[&](const QString &id,const QString &name,QRectF r,int pixels,bool checked=false,bool enabled=true,bool emphasized=false){auto c=control(id,name,r,QAccessible::Button,{},checked,enabled,checked||id.startsWith("modifier/")||id.startsWith("trigger/"));p.save();if(!enabled)p.setOpacity(p.opacity()*.45);QRectF bg=r;if(emphasized)bg.setTop(r.center().y()-16),bg.setHeight(32);rounded(bg,6,emphasized?QColor(Qt::transparent):style.palette.color(QPalette::Button),emphasized?QColor():alpha(ink,.08));if(c.pressed||checked||emphasized)shade(bg,6,accent,c.pressed?.2:.12);label(r,name,pixels,(checked||emphasized)?accent:ink,Qt::AlignCenter);p.restore();};
 p.save();rounded(QRectF(QPointF(0,0),size),compact?8:12,style.palette.color(QPalette::Window));
 if(!d->picker.isEmpty()){
  // The QML picker is a second rounded Rectangle over the settings panel.
  rounded(QRectF(QPointF(0,0),size),compact?8:12,style.palette.color(QPalette::Window));
  const bool shortcut=d->picker=="shortcut";
  label({16,0,size.width()-160,48},shortcut?QStringLiteral("呼出快捷键"):d->picker=="input"?QStringLiteral("默认输入法"):QStringLiteral("显示屏幕"),compact?13:16,ink,Qt::AlignVCenter|Qt::AlignLeft,false,true,true);
  button("cancelPicker","‹  返回设置",{size.width()-128,0,112,48},13,false,true,true);
  viewport=shortcut?QRectF(16,52,size.width()-32,qMax(qreal(0),size.height()-64)):QRectF(12,56,size.width()-24,qMax(qreal(0),size.height()-68));
  const int columns=compact?12:16;const qreal contentHeight=shortcut?48+std::ceil(48.0/columns)*46+48:(d->picker=="input"?d->input.inputMethods().size():screens.size()+1)*48;
  d->maximum=qMax(qreal(0),contentHeight-viewport.height());d->offset=qBound(qreal(0),d->offset,d->maximum);
  p.setClipRect(viewport);const qreal top=viewport.y()-d->offset;
  if(shortcut){
   const QStringList mods={"Ctrl","Alt","Shift","Super"};for(int n=0;n<mods.size();++n)button("modifier/"+mods[n],mods[n],{viewport.x()+96*n,top,88,44},13,d->modifiers.contains(mods[n]));
   QStringList triggers;for(QChar ch:QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"))triggers<<QString(ch);for(int n=1;n<=12;++n)triggers<<"F"+QString::number(n);
   const qreal w=(viewport.width()-(columns-1)*2)/columns;for(int n=0;n<triggers.size();++n)button("trigger/"+triggers[n],triggers[n],{viewport.x()+(n%columns)*(w+2),top+48+(n/columns)*46,w,44},11,d->trigger==triggers[n]);
   QStringList chosen;for(const auto &m:mods)if(d->modifiers.contains(m))chosen<<m;chosen<<d->trigger;
   label({viewport.x()+4,top+contentHeight-44,viewport.width()-120,40},chosen.join('+'),13,ink);
   button("saveShortcut","保存快捷键",{viewport.right()-112,top+contentHeight-44,112,44},13,false,!d->modifiers.isEmpty());
  }else{
   const bool input=d->picker=="input";const auto ids=d->input.inputMethods();const int count=input?ids.size():screens.size()+1;
   for(int n=0;n<count;++n){const QString identity=input?"input:"+ids[n]:n==0?QStringLiteral("screen:follow"):"screen:"+QString::number(n-1)+":"+screens[n-1];
    const QVariant value=input?QVariant(ids[n]):QVariant(n-1);const QString name=input?d->imName(ids[n]):n==0?QStringLiteral("跟随输入窗口"):screens[n-1];
    const bool selected=input?ids[n]==(d->prefs.preferredIm().isEmpty()?d->input.inputMethod():d->prefs.preferredIm()):n-1==d->prefs.screenIndex();
    QRectF r(viewport.x(),top+n*48,viewport.width(),48);auto c=control("choice/"+identity,name,r,QAccessible::RadioButton,value,selected,true,true);
    if(selected){shade(r.adjusted(2,2,-2,-2),7,highlight,.12);}
    label(r.adjusted(16,0,-16,0),name,13,selected?accent:ink,Qt::AlignVCenter|Qt::AlignLeft,false,false,true);
   }
  }
  control("picker-scroll",QStringLiteral("当前picker列表"),viewport,QAccessible::List);
  p.restore();p.restore();for(auto &c:d->controls)c.rect.translate(1,1);d->slider.translate(1,1);return d->controls;
 }
 const qreal rh=compact?28:48,hh=compact?32:48,section=compact?20:28,margin=compact?8:12,gap=margin;
 label({16,0,26,hh},QStringLiteral("⚙"),20,ink,Qt::AlignVCenter|Qt::AlignLeft,false,false,true,.6);label({46,0,size.width()-182,hh},QStringLiteral("键盘设置"),compact?13:16,ink,Qt::AlignVCenter|Qt::AlignLeft,false,true,true);
 button("returnKeyboard","‹  返回键盘",{size.width()-124,0,112,hh},compact?11:13,false,true,true);
 const qreal cw=(size.width()-2*margin-gap)/2,y=hh+(compact?4:8);
 auto row=[&](int column,int n,const QString &title,const QString &subtitle,qreal controlWidth){const qreal x=margin+column*(cw+gap),ry=y+section+n*rh;QRectF r(x,ry,cw,rh);label({x+(compact?8:12),ry+(!compact&&!subtitle.isEmpty()?7:0),cw-controlWidth-(compact?20:30),!compact&&!subtitle.isEmpty()?20:rh},title,compact?11:13,ink,Qt::AlignVCenter|Qt::AlignLeft,true);if(!compact&&!subtitle.isEmpty())label({x+12,ry+27,cw-controlWidth-30,16},subtitle,10,ink,Qt::AlignTop|Qt::AlignLeft,true,false,false,.55);if(n<4){rounded(QRectF(x+12,ry+rh-1,cw-24,1),0,faded(ink,.07));}return QRectF(r.right()-(compact?8:12)-controlWidth,ry,controlWidth,rh);};
 for(int col=0;col<2;++col){QRectF r(margin+col*(cw+gap),y,cw,section+5*rh);rounded(r,9,style.palette.color(QPalette::Base),alpha(ink,.08));label({r.x()+(compact?8:12),y,cw-24,section},col==0?QStringLiteral("输入与呼出"):QStringLiteral("外观与显示"),11,ink,Qt::AlignVCenter|Qt::AlignLeft,false,false,false,.6);}
 auto segments=[&](QRectF r,const QString &id,const QStringList &items,int selected){rounded({r.x(),r.center().y()-qMin(qreal(32),rh-4)/2,r.width(),qMin(qreal(32),rh-4)},6,alpha(ink,.04));const qreal w=r.width()/items.size();for(int n=0;n<items.size();++n){QRectF s(r.x()+n*w,r.y(),w,r.height());auto c=control(id+"/"+QString::number(n),items[n],s,QAccessible::RadioButton,n,n==selected,true,true);if(n==selected)shade({s.x()+2,s.center().y()-qMin(qreal(28),rh-6)/2,w-4,qMin(qreal(28),rh-6)},5,highlight,.12);label(s,items[n],compact?11:12,n==selected?accent:ink,Qt::AlignCenter,false,false,true);}};
 auto choice=[&](QRectF r,const QString &id,const QString &name){auto c=control(id,name,r);rounded({r.x(),r.center().y()-qMin(qreal(32),rh-4)/2,r.width(),qMin(qreal(32),rh-4)},6,QColor(Qt::transparent),alpha(ink,.12));label(r.adjusted(10,0,-35,0),name,compact?11:12,ink,Qt::AlignVCenter|Qt::AlignLeft,true);paintChoiceChevron(p,r,ink);};
 auto toggle=[&](QRectF r,const QString &id,const QString &name,bool checked){auto c=control(id,name,r,QAccessible::CheckBox,checked,checked,true,true);QRectF track(r.center().x()-18,r.center().y()-10,36,20);rounded(track,10,checked?highlight:alpha(ink,.22));rounded({track.x()+(checked?18:2),track.y()+2,16,16},8,Qt::white);};
 const qreal width=compact?130:180;
 segments(row(0,0,"呼出方式",{},width),"modeSetting",{"自动","手动","关闭"},d->prefs.mode());
 auto im=d->imName(d->prefs.preferredIm().isEmpty()?d->input.inputMethod():d->prefs.preferredIm());choice(row(0,1,"默认输入法",{},width),"inputSetting",im.isEmpty()?QStringLiteral("请选择"):im);
 toggle(row(0,2,"安全输入","关闭候选词与词库学习",52),"safeSetting","安全输入",d->prefs.safeInput());
 choice(row(0,3,"呼出快捷键",{},width),"shortcutSetting",d->prefs.shortcut());
 toggle(row(0,4,"按键声音",{},52),"soundSetting","按键声音",d->prefs.keySound());
 segments(row(1,0,"布局模式",{},width),"layoutSetting",{"标准","紧凑"},d->prefs.compact()?1:0);
 toggle(row(1,1,"显示功能键行","F1–F12",52),"functionSetting","显示功能键行",d->prefs.functionRow());
 const int screen=d->prefs.screenIndex();choice(row(1,2,"显示屏幕",{},width),"screenSetting",screen>=0&&screen<screens.size()?screens[screen]:QStringLiteral("跟随输入窗口"));
 segments(row(1,3,"主题",{},width),"themeSetting",{"系统","浅色","深色"},d->prefs.theme());
 auto opacity=row(1,4,"不透明度",{},width);d->slider={opacity.x(),opacity.y(),opacity.width()-36,opacity.height()};
 if(d->transparency){control("opacitySetting","屏幕键盘不透明度",d->slider,QAccessible::Slider,QVariantMap{{"current",d->prefs.opacityPercent()},{"minimum",50},{"maximum",100},{"step",1}});if(!d->controls.isEmpty())d->controls.last().action="settings/opacity";const qreal fraction=(d->prefs.opacityPercent()-50)/50.0;QRectF track(d->slider.x()+6,d->slider.center().y()-1.5,d->slider.width()-12,3);rounded(track,2,alpha(ink,.15));rounded({track.x(),track.y(),track.width()*fraction,3},2,highlight);rounded({d->slider.x()+6+fraction*(d->slider.width()-24),d->slider.center().y()-6,12,12},6,highlight);}
 label({opacity.x(),opacity.y(),opacity.width(),rh},d->transparency?QString::number(d->prefs.opacityPercent())+"%":QStringLiteral("需要桌面合成"),compact?10:12,ink,Qt::AlignVCenter|Qt::AlignRight,false,false,false,.7);
 p.restore();p.restore();for(auto &c:d->controls)c.rect.translate(1,1);d->slider.translate(1,1);return d->controls;
}

bool NativeSettings::currentControl(NativeControl &c)const{
 if(!accepts(c))return false;
 const auto &id=c.action;
 if(id=="safeSetting"){c.checked=d->prefs.safeInput();c.value=c.checked;}
 else if(id=="soundSetting"){c.checked=d->prefs.keySound();c.value=c.checked;}
 else if(id=="functionSetting"){c.checked=d->prefs.functionRow();c.value=c.checked;}
 else if(id.startsWith("modeSetting/"))c.checked=c.value.toInt()==d->prefs.mode();
 else if(id.startsWith("layoutSetting/"))c.checked=c.value.toInt()==int(d->prefs.compact());
 else if(id.startsWith("themeSetting/"))c.checked=c.value.toInt()==d->prefs.theme();
 else if(id=="settings/opacity"){auto value=c.value.toMap();value["current"]=d->prefs.opacityPercent();c.value=value;}
 else if(id=="inputSetting"){c.name=d->imName(d->prefs.preferredIm().isEmpty()?d->input.inputMethod():d->prefs.preferredIm());if(c.name.isEmpty())c.name=QStringLiteral("请选择");}
 else if(id=="shortcutSetting")c.name=d->prefs.shortcut();
 else if(id=="screenSetting"){const int screen=d->prefs.screenIndex();c.name=screen>=0&&screen<d->screens.size()?d->screens[screen]:QStringLiteral("跟随输入窗口");}
 else if(id.startsWith("choice/input:")){c.name=d->imName(c.value.toString());c.checked=c.value.toString()==(d->prefs.preferredIm().isEmpty()?d->input.inputMethod():d->prefs.preferredIm());}
 else if(id.startsWith("choice/screen:"))c.checked=c.value.toInt()==d->prefs.screenIndex();
 else if(id.startsWith("modifier/"))c.checked=d->modifiers.contains(id.mid(9));
 else if(id.startsWith("trigger/"))c.checked=d->trigger==id.mid(8);
 return true;
}
bool NativeSettings::accepts(const NativeControl &c)const{
 if(!d->valid(c))return false;
 if(c.action=="picker-scroll")return !d->picker.isEmpty();
 if(c.action=="settings/opacity")return d->picker.isEmpty()&&d->transparency;
 if(c.action=="saveShortcut")return d->picker=="shortcut"&&!d->modifiers.isEmpty();
 if(c.action.startsWith("choice/input:"))return d->picker=="input"&&d->input.inputMethods().contains(c.value.toString())&&c.action=="choice/input:"+c.value.toString();
 if(c.action.startsWith("choice/screen:")){const int n=c.value.toInt();return d->picker=="screen"&&n>=-1&&n<d->screens.size()&&c.action==(n<0?QStringLiteral("choice/screen:follow"):"choice/screen:"+QString::number(n)+":"+d->screens[n]);}
 return true;
}
bool NativeSettings::activate(const NativeControl &c,QPointF position){
 if(!accepts(c))return false;
 const auto id=c.action;
 if(id=="picker-scroll")return true;
 if(id=="returnKeyboard"){d->close();emit finished();}
 else if(id=="cancelPicker")d->close();
 else if(id=="inputSetting")d->open("input");else if(id=="screenSetting")d->open("screen");else if(id=="shortcutSetting")d->open("shortcut");
 else if(id=="safeSetting")d->prefs.setSafeInput(!d->prefs.safeInput());else if(id=="soundSetting")d->prefs.setKeySound(!d->prefs.keySound());else if(id=="functionSetting")d->prefs.setFunctionRow(!d->prefs.functionRow());
 else if(id.startsWith("modeSetting/"))d->prefs.setMode(c.value.toInt());else if(id.startsWith("layoutSetting/"))d->prefs.setCompact(c.value.toInt()==1);else if(id.startsWith("themeSetting/"))d->prefs.setTheme(c.value.toInt());
 else if(id=="settings/opacity")d->opacity(position.x());
 else if(id.startsWith("choice/")){
  if(d->picker=="input"){const auto value=c.value.toString();if(!d->input.inputMethods().contains(value)||id!="choice/input:"+value)return false;d->prefs.setPreferredIm(value);}
  else if(d->picker=="screen"){int n=c.value.toInt();if(n< -1||n>=d->screens.size())return false;const auto identity=n<0?QStringLiteral("choice/screen:follow"):"choice/screen:"+QString::number(n)+":"+d->screens[n];if(id!=identity)return false;d->prefs.setScreenIndex(n);}else return false;
  d->close();
 }else if(id.startsWith("modifier/")&&d->picker=="shortcut"){auto m=id.mid(9);if(d->modifiers.contains(m))d->modifiers.remove(m);else d->modifiers.insert(m);emit changed();}
 else if(id.startsWith("trigger/")&&d->picker=="shortcut"){d->trigger=id.mid(8);emit changed();}
 else if(id=="saveShortcut"&&d->picker=="shortcut"&&!d->modifiers.isEmpty()){QStringList parts;for(const auto &m:QStringList{"Ctrl","Alt","Shift","Super"})if(d->modifiers.contains(m))parts<<(m=="Super"?"Meta":m);parts<<d->trigger;d->prefs.setShortcut(parts.join('+'));d->close();}else return false;
 return true;
}
bool NativeSettings::drag(const NativeControl &c,QPointF position){if(c.action!="settings/opacity"||!accepts(c)||!d->picker.isEmpty()||!d->transparency)return false;d->opacity(position.x());return true;}
