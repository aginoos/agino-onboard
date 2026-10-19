// SPDX-License-Identifier: GPL-3.0-or-later
#include "nativepanel.h"
#include <QGuiApplication>
#include <QPainterPath>
#include <QImageReader>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QScreen>
#include <QSurfaceFormat>
#include <DGuiApplicationHelper>
#include <DWindowManagerHelper>
#include <cmath>
#include <limits>
using Dtk::Gui::DGuiApplicationHelper;
namespace {constexpr int NoOwner=-999999;QFont pixels(QFont f,int size){f.setPixelSize(size);return f;}bool pointInside(const QRectF &r,QPointF p){return p.x()>=r.left()&&p.x()<r.right()&&p.y()>=r.top()&&p.y()<r.bottom();}}
NativePanel::NativePanel(Preferences &p,FcitxClient &i,KeyboardState &s):prefs_(p),input_(i),state_(s),store_(this),settings_(p,i,this),layoutCompact_(p.compact()),functionRow_(p.functionRow()){
 auto format=QSurfaceFormat::defaultFormat();format.setAlphaBufferSize(8);setFormat(format);
 installNativeAccessibility();
 connect(&input_,&FcitxClient::changed,this,&NativePanel::refresh);
 connect(&input_,&FcitxClient::candidateSelected,this,[this]{setExpanded(false);});
 connect(&input_,&FcitxClient::pageFinished,this,[this](bool accepted,quint64,quint64){if(!accepted)setExpanded(false);});
 connect(&prefs_,&Preferences::changed,this,[this]{if(layoutCompact_!=prefs_.compact()){layoutCompact_=prefs_.compact();cancelContacts();controls_.clear();}setFunctionRow(prefs_.functionRow());updateCorners();requestUpdate();});
 connect(&state_,&KeyboardState::modifiersChanged,this,[this]{requestUpdate();});
 connect(&state_,&KeyboardState::inputAllowedChanged,this,[this]{if(!state_.settingsOpen()&&!state_.inputAllowed())cancelContacts();requestUpdate();});
 connect(&state_,&KeyboardState::settingsOpenChanged,this,[this]{cancelContacts();setExpanded(false);alternatives_.clear();settings_.reset();requestUpdate();notifyNativeAccessibility(this);});
 connect(&state_,&KeyboardState::visibleChanged,this,[this](bool v){if(!v){cancelContacts();alternatives_.clear();setExpanded(false);}notifyNativeAccessibility(this,v?QAccessible::ObjectShow:QAccessible::ObjectHide);});
 connect(&settings_,&NativeSettings::changed,this,[this]{requestUpdate();});
 connect(&settings_,&NativeSettings::finished,this,[this]{state_.setSettingsOpen(false);});
 connect(DGuiApplicationHelper::instance(),&DGuiApplicationHelper::applicationPaletteChanged,this,[this]{requestUpdate();});
 connect(qGuiApp,&QGuiApplication::fontChanged,this,[this]{NativePainting::clearTextCache();requestUpdate();});
 pointerClock_.start();holdTimer_.setSingleShot(true);holdTimer_.setInterval(600);connect(&holdTimer_,&QTimer::timeout,this,&NativePanel::longPress);
 auto corners=[this]{updateCorners();};
 if(QGuiApplication::platformName()=="xcb"){
  compositor_=std::make_unique<X11CompositorWatcher>();
  connect(compositor_.get(),&X11CompositorWatcher::changed,compositor_.get(),corners);
  auto wm=Dtk::Gui::DWindowManagerHelper::instance();
  connect(wm,&Dtk::Gui::DWindowManagerHelper::hasCompositeChanged,compositor_.get(),corners);
  connect(wm,&Dtk::Gui::DWindowManagerHelper::hasNoTitlebarChanged,compositor_.get(),corners);
  connect(this,&QWindow::visibleChanged,compositor_.get(),corners);
  connect(this,&QWindow::screenChanged,compositor_.get(),corners);
 }
 refresh();
}
NativePanel::~NativePanel(){NativePainting::clearTextCache();cancelContacts();compositor_.reset();decoration_.reset();}
void NativePanel::setExpanded(bool value){value=value&&!words_.isEmpty()&&!input_.sensitive()&&!state_.settingsOpen();if(expanded_==value)return;cancelContacts();expanded_=value;expandedOffset_=0;emit expandedChanged();requestUpdate();notifyNativeAccessibility(this);}
void NativePanel::setFunctionRow(bool value){if(functionRow_==value)return;cancelContacts();controls_.clear();functionRow_=value;emit minimumHeightChanged();requestUpdate();}
void NativePanel::setTransparencySupported(bool value){if(transparency_==value)return;transparency_=value;requestUpdate();}
void NativePanel::refresh(){
 auto words=input_.sensitive()?QStringList():input_.candidates();
 if(input_.contextEpoch()!=candidateContext_||input_.candidateEpoch()!=candidateGeneration_||words!=words_){
  const bool expiredLease=input_.contextEpoch()!=candidateContext_||(input_.candidateEpoch()!=candidateGeneration_&&!input_.paging());
  const bool expired=expanded_&&expiredLease;
  if(expiredLease)alternatives_.clear();
  for(auto it=contacts_.begin();it!=contacts_.end();){
   const bool candidate=it->control.action=="candidate"||it->control.action.startsWith("scroll/");
   const bool stale=candidate&&(it->control.context!=input_.contextEpoch()||it->control.generation!=input_.candidateEpoch());
   const bool expiredKey=it->control.sym&&it->context!=input_.contextEpoch();
   if(!it->moving&&(stale||expiredKey))it=contacts_.erase(it);else ++it;
  }
  scheduleHold();candidateContext_=input_.contextEpoch();candidateGeneration_=input_.candidateEpoch();if(words_!=words){candidateWidths_.clear();measuredWidth_=0;}words_=words;
  if(expired||words_.isEmpty())setExpanded(false);
 }
 if(!input_.editable()||input_.sensitive())alternatives_.clear();
 requestUpdate();
}
NativeStyle NativePanel::style()const{NativeStyle s;s.palette=DGuiApplicationHelper::instance()->applicationPalette();s.font=QGuiApplication::font();s.compact=prefs_.compact();s.functionRow=functionRow_;s.transparencySupported=transparency_;return s;}
QRectF NativePanel::keyArea(QSizeF sz)const{const bool c=prefs_.compact();qreal margin=c?8:12,top=c?36:56,pad=c?4:12;return {margin,top,sz.width()-2*margin,sz.height()-top-pad};}
QRectF NativePanel::candidateViewport(QSizeF sz)const{const bool c=prefs_.compact();qreal x=c?4:8,h=c?32:48,slot=c?24:32;return {x+slot+8,0,sz.width()-2*x-h-slot-16,h};}
void NativePanel::addControl(NativeControl c){if(!c.rect.isEmpty())controls_.push_back(std::move(c));}
void NativePanel::updateIcon(){int size=qRound((prefs_.compact()?18:24)*devicePixelRatio());if(size==iconPixels_&&!icon_.isNull())return;iconPixels_=size;QImageReader reader(size>=1&&size<=128?QString(":/data/icons/keyboard-%1.png").arg(size):QString(":/data/agino-keyboard.svg"),size>=1&&size<=128?"PNG":"SVG");reader.setAutoDetectImageFormat(false);if(size>128)reader.setScaledSize(QSize(size,size));icon_=reader.read();}
void NativePanel::render(QPainter &p,QSizeF sz){
 controls_.clear();const auto st=style();const auto pal=st.palette;const bool compact=st.compact;const qreal w=sz.width(),h=sz.height(),ch=compact?32:48,bx=compact?4:8,bw=w-2*bx;
 const qreal opacity=transparency_?prefs_.opacityPercent()/100.:1.;p.setOpacity(opacity);p.setRenderHint(QPainter::Antialiasing,true);
 QColor border=pal.windowText().color();border.setAlphaF(.12);QColor fill=pal.window().color();const qreal alpha=border.alphaF();QColor finalBorder=QColor::fromRgbF(fill.redF()*(1-alpha)+border.redF()*alpha,fill.greenF()*(1-alpha)+border.greenF()*alpha,fill.blueF()*(1-alpha)+border.blueF()*alpha,1);
 p.setPen(QPen(finalBorder,1));p.setBrush(fill);NativePainting::rectangle(p,QRectF(0,0,w,h),fill,radius(),finalBorder,1);
 if(state_.settingsOpen()){
  QString pressed;for(const auto &contact:contacts_)if(!contact.moving&&!contact.scrolling){pressed=contact.control.id;break;}
  controls_=settings_.paint(p,sz,st,screens_,pressed);return;
 }
 updateIcon();qreal is=compact?18:24;p.save();p.setRenderHint(QPainter::Antialiasing,false);p.setRenderHint(QPainter::SmoothPixmapTransform,true);p.drawImage(QRectF(bx+4,(ch-is)/2,is,is),icon_);p.restore();
 const bool hasWords=!words_.isEmpty()&&!input_.sensitive();
 QRectF toolbarAction(bx+bw-ch,0,ch,ch);
 NativeControl action;action.id=hasWords?"expandButton":"settingsButton";action.name=hasWords?(expanded_?QCoreApplication::translate("Keyboard","Collapse candidates"):QCoreApplication::translate("Keyboard","Expand candidates")):QCoreApplication::translate("Keyboard","Settings");action.action=hasWords?"expand":"settings";action.rect=toolbarAction;action.checked=expanded_;action.enabled=dragOwner_==NoOwner;if(hasWords||!embedded_)addControl(action);
 if(hasWords){
  if(expanded_){bool held=false;for(const auto &contact:contacts_)held|=contact.control.id==action.id&&!contact.moving;const int lightness=held?-10:(hovered_==action.id?10:0);const QColor checked=DGuiApplicationHelper::adjustColor(pal.highlight().color(),0,0,lightness,0,0,0,0);NativePainting::rectangle(p,toolbarAction,checked,8);}
  qreal scale=(!expanded_&&hovered_==action.id)?1.1:1;
  if(!expanded_&&hovered_==action.id){p.save();p.setOpacity(opacity*.1);p.setPen(Qt::NoPen);p.setBrush(Qt::black);p.drawRoundedRect(toolbarAction,8,8);p.restore();}
  NativePainting::icon(p,toolbarAction,"Expand",pal.windowText().color(),scale,2);
  const QRectF clip=candidateViewport(sz);QFont font=pixels(st.font,compact?13:16);const qreal gap=compact?6:8;
  if(candidateFont_!=font){candidateFont_=font;candidateWidths_.clear();measuredWidth_=0;}
  // Measure only the visible prefix and its next word. Like a virtualized list,
  // offscreen candidate glyphs need not open fallback fonts until scrolled in.
  while(candidateWidths_.size()<words_.size()&&measuredWidth_<candidateOffset_+clip.width()+gap){
   const int index=candidateWidths_.size();const qreal width=qMax(ch,qreal(qCeil(NativePainting::textSize(words_[index],font).width())+(compact?20:28)));
   candidateWidths_.push_back(width);measuredWidth_+=width+gap;
  }
  candidateContent_=candidateWidths_.size()==words_.size()?qMax(0.,measuredWidth_-gap):measuredWidth_+clip.width();
  candidateOffset_=qBound(0.,candidateOffset_,qMax(0.,candidateContent_-clip.width()));qreal x=clip.x()-candidateOffset_;p.save();p.setClipRect(clip,Qt::IntersectClip);
  for(int index=0;index<candidateWidths_.size();++index){const qreal width=candidateWidths_[index];QRectF r(x,0,width,ch);
   if(r.intersects(clip)){NativePainting::text(p,r,words_[index],font,pal.windowText().color());NativeControl c;c.id=QString("candidate-%1-%2-%3").arg(candidateContext_).arg(candidateGeneration_).arg(index);c.name=words_[index];c.rect=r.intersected(clip);c.action="candidate";c.index=index;c.context=candidateContext_;c.generation=candidateGeneration_;c.enabled=dragOwner_==NoOwner;addControl(c);}x+=width+gap;
  }
  p.restore();
  NativeControl list;list.id="candidate-list";list.name=QCoreApplication::translate("Keyboard","Candidates");list.action="scroll/candidates";list.role=QAccessible::List;list.context=candidateContext_;list.generation=candidateGeneration_;list.rect=clip;list.enabled=dragOwner_==NoOwner;addControl(list);
 }else if(!embedded_){
  const QString label=compact?(input_.sensitive()?QCoreApplication::translate("Keyboard","Lock"):input_.inputLanguageCode().isEmpty()?QStringLiteral("—"):input_.inputLanguageCode()):input_.inputMethodStatus();QFont font=pixels(st.font,compact?13:14);qreal width=qMin(qreal(qCeil(NativePainting::textSize(label,font).width())),bw*.35);
  p.save();p.setOpacity(opacity*.75);NativePainting::text(p,QRectF(toolbarAction.x()-6-width,0,width,ch),label,font,pal.windowText().color(),Qt::AlignRight|Qt::AlignVCenter,false,true,true);p.restore();
  bool held=false;for(const auto &contact:contacts_)held|=contact.control.id==action.id&&!contact.moving;
  if(held){p.save();p.setOpacity(opacity*.12);p.setPen(Qt::NoPen);p.setBrush(pal.highlight());p.drawRoundedRect(toolbarAction,compact?3:7,compact?3:7);p.restore();}
  NativePainting::icon(p,toolbarAction,"Settings",held?pal.highlight().color():pal.windowText().color(),compact?.58:.75,1.2);
 }
 {
  const auto keys=NativePainting::keys(sz,compact,functionRow_);
  for(auto key:keys){if(embedded_&&!key.action.isEmpty())continue;bool held=false;for(const auto &contact:contacts_)held|=contact.control.id==key.control.id&&!contact.moving&&!contact.alternative;
   bool enabled=!expanded_&&(input_.editable()||!key.action.isEmpty()||key.control.id=="key-Space");NativePainting::key(p,key,st,state_.modifiers(),held,enabled,key.control.id=="key-Space"?input_.status():QString());
   key.control.enabled=enabled&&dragOwner_==NoOwner;key.control.pressed=held||((key.control.sym==0xffe1||key.control.sym==0xffe2)&&(state_.modifiers()&3));key.control.checked=(key.control.sym==0xffe1||key.control.sym==0xffe2)&&(state_.modifiers()&3);key.control.checkable=key.control.sym==0xffe1||key.control.sym==0xffe2;if(key.control.checked&&(state_.modifiers()&2))key.control.name=QCoreApplication::translate("Keyboard","Shift (Caps Lock)");if(!expanded_)addControl(key.control);
  }
 }
 if(expanded_){
  QRectF area=keyArea(sz);p.setPen(Qt::NoPen);p.setBrush(pal.window());NativePainting::rectangle(p,area,pal.window().color(),8);QRectF clip=area.adjusted(4,4,-4,-4);QFont font=pixels(st.font,18);
  candidateRows_.setFont(font);candidateRows_.setAvailableWidth(clip.width());candidateRows_.setCandidates(words_);candidateRows_.ensureLayout(expandedOffset_+clip.height());expandedContent_=candidateRows_.contentHeight();expandedOffset_=qBound(0.,expandedOffset_,qMax(0.,expandedContent_-clip.height()));qreal y=clip.y()-expandedOffset_;p.save();p.setClipRect(clip,Qt::IntersectClip);
  for(int row=0;row<candidateRows_.rowCount();++row){QModelIndex modelIndex=candidateRows_.index(row);const auto entries=candidateRows_.data(modelIndex,NativeCandidateRows::Entries).toList();qreal rowHeight=candidateRows_.data(modelIndex,NativeCandidateRows::RowHeight).toReal();
   for(const auto &entry:entries){auto value=entry.toMap();QRectF r(clip.x()+value["x"].toReal(),y,value["width"].toReal(),value["height"].toReal());if(!r.intersects(clip))continue;p.setPen(Qt::NoPen);p.setBrush(pal.button());NativePainting::rectangle(p,r,pal.button().color(),6);NativePainting::text(p,r.adjusted(12,12,-12,-12),value["text"].toString(),font,pal.buttonText().color(),Qt::AlignCenter,true,false,true);
    NativeControl c;c.id=QString("expandedCandidate-%1-%2-%3").arg(candidateContext_).arg(candidateGeneration_).arg(value["index"].toInt());c.name=value["text"].toString();c.action="candidate";c.index=value["index"].toInt();c.rect=r.intersected(clip);c.context=candidateContext_;c.generation=candidateGeneration_;addControl(c);
   }y+=rowHeight+8;
  }
  p.restore();expandedContent_=candidateRows_.contentHeight();expandedOffset_=qBound(0.,expandedOffset_,qMax(0.,expandedContent_-clip.height()));NativeControl list;list.id="candidate-flow";list.name=QCoreApplication::translate("Keyboard","More candidates");list.action="scroll/expanded";list.role=QAccessible::List;list.context=candidateContext_;list.generation=candidateGeneration_;list.rect=clip;addControl(list);
 }
 if(!alternatives_.isEmpty()){
  p.setPen(Qt::NoPen);p.setBrush(pal.highlight());NativePainting::rectangle(p,alternativeRect_,pal.highlight().color(),8);
  for(int n=0;n<alternatives_.size();++n){QRectF r(alternativeRect_.x()+n*56,alternativeRect_.y(),56,56);NativePainting::text(p,r,alternatives_[n],pixels(st.font,20),pal.highlightedText().color());NativeControl c;c.id=QString("alternative-%1-%2").arg(alternativeGeneration_).arg(n);c.index=n;c.generation=alternativeGeneration_;c.name=alternatives_[n];c.action="alternative";c.value=alternatives_[n];c.rect=r;c.context=alternativeContext_;addControl(c);}
 }
}
void NativePanel::paint(){
 if(!isExposed()||painting_)return;painting_=true;
 if(!x11Output_.paint(this,[this](QPainter &p,QSizeF size){render(p,size);})){
  store_.resize(size());const QRegion region(QRect(QPoint(),size()));store_.beginPaint(region);QPainter painter(store_.paintDevice());painter.setCompositionMode(QPainter::CompositionMode_Source);painter.fillRect(QRect(QPoint(),size()),Qt::transparent);painter.setCompositionMode(QPainter::CompositionMode_SourceOver);render(painter,size());painter.end();store_.endPaint();store_.flush(region,this);
 }
 if(frameMs_<0&&frameStart_.isValid())frameMs_=frameStart_.nsecsElapsed()/1000000.;painting_=false;notifyNativeAccessibility(this);
}
void NativePanel::exposeEvent(QExposeEvent*){updateCorners();paint();}
void NativePanel::resizeEvent(QResizeEvent*){cancelContacts();controls_.clear();updateCorners();requestUpdate();}
void NativePanel::measureNextFrame(const QElapsedTimer &time,quint64 serial){frameStart_=time;frameSerial_=serial;frameMs_=-1;requestUpdate();}
QImage NativePanel::grabWindow(){QImage image(qRound(width()*devicePixelRatio()),qRound(height()*devicePixelRatio()),QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(devicePixelRatio());image.fill(Qt::transparent);QPainter p(&image);render(p,size());return image;}
void NativePanel::updateCorners(bool force){if(QGuiApplication::platformName()!="xcb")return;auto wm=Dtk::Gui::DWindowManagerHelper::instance();bool composited=wm->hasComposite()||x11CompositeAvailable();setTransparencySupported(composited);
 if(wm->hasNoTitlebar()&&Dtk::Gui::DPlatformHandle::isDXcbPlatform()){if(!decoration_){decoration_=std::make_unique<Dtk::Gui::DPlatformHandle>(this);decoration_->setEnableSystemResize(false);decoration_->setEnableSystemMove(false);decoration_->setBorderWidth(0);}decoration_->setWindowRadius(qRound(radius()));}
 QRegion region;if(!composited){QPainterPath outline;outline.addRoundedRect(QRectF(0,0,width(),height()),radius(),radius());region=QRegion(outline.toFillPolygon().toPolygon());}if(force||region!=mask())setMask(region);
}
void NativePanel::cancelContacts(){holdTimer_.stop();state_.cancelAll();contacts_.clear();dragOwner_=NoOwner;alternatives_.clear();requestUpdate();}
void NativePanel::pointerBegin(int id,QPointF local,QPointF global){
 if(!state_.visible()||dragOwner_!=NoOwner||contacts_.contains(id))return;Contact contact;contact.originGlobal=contact.previousGlobal=global;contact.originLocal=local;contact.context=input_.contextEpoch();
 // A list is a scroll fallback; individual buttons painted before it win hits.
 const auto eligible=accessibleControls();
 for(const auto &control:eligible)if(control.enabled&&pointInside(control.rect,local)&&control.role!=QAccessible::List)contact.control=control;
 if(contact.control.id.isEmpty())for(const auto &control:eligible)if(control.enabled&&pointInside(control.rect,local)&&control.role==QAccessible::List)contact.control=control;
 for(const auto &existing:contacts_)if(!contact.control.id.isEmpty()&&contact.control.id==existing.control.id)return;
 if(contact.control.action=="candidate"||contact.control.action.startsWith("scroll/"))contact.scrollArea=contact.control.id.startsWith("expandedCandidate-")||contact.control.action=="scroll/expanded"?"expanded":"candidates";
 if(contact.control.sym&&!NativePainting::alternatives(contact.control.sym).isEmpty())contact.holdAt=pointerClock_.elapsed()+600;
 contacts_.insert(id,contact);scheduleHold();
 if(contact.control.sym)state_.press(id,contact.control.sym);
 if(contact.control.action=="settings/opacity")settings_.drag(contact.control,local);
 requestUpdate();
}
void NativePanel::pointerMove(int id,QPointF local,QPointF global){
 auto it=contacts_.find(id);if(it==contacts_.end()||(dragOwner_!=NoOwner&&dragOwner_!=id))return;
 Contact contact=it.value();const QPointF delta=global-contact.previousGlobal,travel=global-contact.originGlobal;
 if(contact.control.action=="settings/opacity"){settings_.drag(contact.control,local);if(contacts_.contains(id))contacts_[id].previousGlobal=global;return;}
 if(contact.alternative){const bool approach=!alternatives_.isEmpty()&&local.x()>=alternativeRect_.left()&&local.x()<alternativeRect_.right()&&local.y()>=alternativeRect_.top()&&local.y()<=contact.control.rect.bottom();if(approach||std::hypot(travel.x(),travel.y())<16)return;alternatives_.clear();contact.alternative=false;}
 const bool candidate=!contact.scrollArea.isEmpty();
 const bool picker=state_.settingsOpen()&&settings_.pickerOpen();
 if(!contact.moving&&!contact.scrolling&&std::hypot(travel.x(),travel.y())>=16){
  contact.holdAt=-1;
  if(candidate&&((contact.scrollArea=="expanded"&&std::abs(travel.y())>=std::abs(travel.x()))||(contact.scrollArea=="candidates"&&std::abs(travel.x())>=std::abs(travel.y()))))contact.scrolling=true;
  else if(picker)contact.scrolling=true;
  else if(!expanded_){
   dragOwner_=id;contact.moving=true;alternatives_.clear();
   for(auto peer=contacts_.begin();peer!=contacts_.end();)if(peer.key()!=id)peer=contacts_.erase(peer);else ++peer;
   contacts_[id]=contact;state_.cancelAll();if(!contacts_.contains(id))return;
  }else contact.suppressed=true;
 }
 contacts_[id]=contact;scheduleHold();
 if(contact.moving&&dragOwner_==id)emit moveRequested(delta.x(),delta.y());
 else if(contact.scrolling){if(picker)settings_.scroll(-delta.y());else scroll(contact.scrollArea,contact.scrollArea=="expanded"?-delta.y():-delta.x());}
 if(contacts_.contains(id))contacts_[id].previousGlobal=global;
 // A finger that only trembles on a key changes nothing on the screen.
 if(contact.moving||contact.scrolling||contact.suppressed)requestUpdate();
}
void NativePanel::pointerEnd(int id,QPointF local,bool canceled){auto it=contacts_.find(id);if(it==contacts_.end())return;const Contact contact=it.value();contacts_.erase(it);scheduleHold();if(dragOwner_!=NoOwner&&dragOwner_!=id){if(contact.control.sym)state_.release(id,false);return;}
 if(contact.moving){if(dragOwner_==id)dragOwner_=NoOwner;}
 else if(contact.suppressed){if(contact.control.sym)state_.release(id,false);}
 else if(contact.scrolling){if(!canceled&&!state_.settingsOpen()&&contact.control.context==input_.contextEpoch()&&contact.control.generation==input_.candidateEpoch()&&!contact.scrollArea.isEmpty())finishScroll(contact.scrollArea);}
 else if(contact.alternative){if(!canceled&&pointInside(alternativeRect_,local)&&contact.context==input_.contextEpoch()){int n=int((local.x()-alternativeRect_.x())/56);if(n>=0&&n<alternatives_.size()){auto cp=alternatives_[n].toUcs4();if(!cp.isEmpty()){state_.press(-500,0x1000000+cp[0]);state_.release(-500,true);}}}alternatives_.clear();}
 else if(contact.control.sym)state_.release(id,!canceled&&pointInside(contact.control.rect,local)&&dragOwner_==NoOwner);
 else if(!canceled&&pointInside(contact.control.rect,local)&&dragOwner_==NoOwner)activate(contact.control,local);
 requestUpdate();
}
void NativePanel::scheduleHold(){
 qint64 deadline=std::numeric_limits<qint64>::max();for(const auto &c:contacts_)if(c.holdAt>=0&&!c.moving&&!c.scrolling&&!c.alternative)deadline=qMin(deadline,c.holdAt);
 if(deadline==std::numeric_limits<qint64>::max())holdTimer_.stop();else holdTimer_.start(int(qMax(qint64(1),deadline-pointerClock_.elapsed())));
}
void NativePanel::longPress(){
 const auto ids=contacts_.keys();const auto now=pointerClock_.elapsed();
 for(int id:ids){auto it=contacts_.find(id);if(it==contacts_.end()||it->holdAt<0||it->holdAt>now||it->moving||it->scrolling||it->alternative)continue;
  Contact contact=it.value();contact.holdAt=-1;auto alternatives=NativePainting::alternatives(contact.control.sym);contacts_[id]=contact;if(alternatives.isEmpty())continue;
  state_.release(id,false);if(!contacts_.contains(id)||contacts_[id].context!=input_.contextEpoch())continue;
  contacts_[id].alternative=true;++alternativeGeneration_;alternativeContext_=contact.context;alternatives_=alternatives;alternativeRect_={qMax(8.,qMin(contact.control.rect.x(),width()-alternatives.size()*56.-8)),qMax(prefs_.compact()?32.:48.,contact.control.rect.y()-56),alternatives.size()*56.,56};
 }
 scheduleHold();requestUpdate();
}
void NativePanel::activate(const NativeControl &c,QPointF position){if(!state_.visible()||!isVisible()||!c.enabled)return;
 if(c.id.startsWith("settings/")){settings_.activate(c,position);return;}
 if(c.action=="candidate"){if(c.context==input_.contextEpoch()&&c.generation==input_.candidateEpoch()&&!input_.sensitive())input_.selectCandidate(c.index,c.context,c.generation);return;}
 if(c.action=="settings"){state_.cancelAll();alternatives_.clear();emit settingsRequested();return;}
 if(c.action=="expand"){setExpanded(!expanded_);return;}
 if(c.action=="Language"){if(!state_.inputAllowed()||!input_.editable()||input_.sensitive())return;state_.cancelAll();auto methods=input_.inputMethods();if(methods.size()>1)input_.setInputMethod(methods[(methods.indexOf(input_.inputMethod())+1)%methods.size()]);return;}
 if(c.action=="Hide"){if(embedded_)return;setExpanded(false);state_.closePanel();return;}
 if(c.action=="alternative"&&c.context==input_.contextEpoch()&&c.generation==alternativeGeneration_&&c.index>=0&&c.index<alternatives_.size()&&c.value.toString()==alternatives_[c.index]){auto cp=c.value.toString().toUcs4();if(!cp.isEmpty()){state_.press(-500,0x1000000+cp[0]);state_.release(-500,true);}alternatives_.clear();return;}
 if(c.sym&&state_.inputAllowed()){state_.press(-1000000,c.sym);state_.release(-1000000,true);}
}
void NativePanel::finishScroll(const QString &area,bool forward,bool backward){
 if(area=="expanded"){
  if(forward&&candidateRows_.complete()&&expandedOffset_>=qMax(0.,expandedContent_-keyArea(size()).height()+8)&&input_.hasNext())input_.requestPage(true);
 }else if(area=="candidates"){
  if(backward&&candidateOffset_<=0&&input_.hasPrev())input_.requestPage(false);
  else if(forward&&candidateWidths_.size()==words_.size()&&candidateOffset_>=qMax(0.,candidateContent_-candidateViewport(size()).width())&&input_.hasNext())input_.requestPage(true);
 }
}
void NativePanel::scroll(const QString &which,qreal amount){if(which=="expanded"){expandedOffset_=qBound(0.,expandedOffset_+amount,qMax(0.,expandedContent_-keyArea(size()).height()+8));}else candidateOffset_=qBound(0.,candidateOffset_+amount,qMax(0.,candidateContent_-candidateViewport(size()).width()));requestUpdate();}
QVector<NativeControl> NativePanel::accessibleControls()const{
 if(!isVisible()||!state_.visible())return {};
 QVector<NativeControl> result;
 for(auto c:controls_){
  const bool setting=c.id.startsWith("settings/");
  if(setting!=state_.settingsOpen())continue;
  if(c.id.startsWith("key-")&&expanded_)continue;
  if((c.id.startsWith("expandedCandidate-")||c.action=="scroll/expanded")&&!expanded_)continue;
  if(setting&&!settings_.currentControl(c))continue;
  if(c.action=="settings/opacity"&&!transparency_)continue;
  if(c.action=="settings/opacity"){auto value=c.value.toMap();value["current"]=prefs_.opacityPercent();c.value=value;}
  if(c.action=="alternative"&&(alternatives_.isEmpty()||c.generation!=alternativeGeneration_||c.context!=input_.contextEpoch()||c.index<0||c.index>=alternatives_.size()||c.value.toString()!=alternatives_[c.index]))continue;
  if(c.action=="expand"&&(words_.isEmpty()||input_.sensitive()))continue;
  if(c.action=="expand"){c.checked=expanded_;c.name=expanded_?QCoreApplication::translate("Keyboard","Collapse candidates"):QCoreApplication::translate("Keyboard","Expand candidates");}
  if(c.action=="settings"&&!words_.isEmpty()&&!input_.sensitive())continue;
  c.enabled=c.enabled&&dragOwner_==NoOwner;
  if(c.action=="candidate"||c.action.startsWith("scroll/"))c.enabled=c.enabled&&c.context==input_.contextEpoch()&&c.generation==input_.candidateEpoch()&&!input_.sensitive();
  if(c.sym)c.enabled=c.enabled&&(state_.inputAllowed()||c.id=="key-Space");
  result.push_back(c);
 }
 return result;
}
bool NativePanel::invokeAccessible(const QString &id,const QString &action){
 const auto items=accessibleControls();
 for(const auto &c:items)if(c.id==id&&c.enabled){
  const bool backwards=action==QAccessibleActionInterface::scrollLeftAction()||action==QAccessibleActionInterface::scrollUpAction();
  const bool forwards=action==QAccessibleActionInterface::scrollRightAction()||action==QAccessibleActionInterface::scrollDownAction();
  if(backwards||forwards){
   const qreal direction=backwards?-1:1;
   if(c.action.startsWith("scroll/")){
    const bool flow=c.action=="scroll/expanded";scroll(flow?"expanded":"candidates",direction*(flow?keyArea(size()).height():candidateViewport(size()).width())/2);
    finishScroll(flow?"expanded":"candidates",forwards,backwards);
   }else if(c.id.startsWith("settings/"))settings_.scroll(direction*height()/2);
   return true;
  }
  if(c.action=="settings/opacity"){
   if(!state_.settingsOpen()||!transparency_||!settings_.accepts(c))return false;
   const auto value=c.value.toMap();qreal n=prefs_.opacityPercent();
   if(action.startsWith("setValue:")){bool ok=false;n=action.mid(9).toDouble(&ok);if(!ok||!std::isfinite(n))return false;}
   else n+=(action==QAccessibleActionInterface::decreaseAction()?-1:1)*value.value("step",1).toDouble();
   n=qBound(value.value("minimum").toDouble(),n,value.value("maximum").toDouble());
   return settings_.drag(c,QPointF(c.rect.left()+12+(n-50)/50*(c.rect.width()-24),c.rect.center().y()));
  }
  activate(c,c.rect.center());return true;
 }
 return false;
}
bool NativePanel::event(QEvent *event){
 switch(event->type()){
 case QEvent::UpdateRequest:paint();return true;
 case QEvent::MouseButtonPress:{auto e=static_cast<QMouseEvent*>(event);if(e->button()==Qt::LeftButton&&e->source()==Qt::MouseEventNotSynthesized){pointerBegin(-1,e->position(),e->globalPosition());return true;}break;}
 case QEvent::MouseMove:{auto e=static_cast<QMouseEvent*>(event);if(e->source()!=Qt::MouseEventNotSynthesized)break;pointerMove(-1,e->position(),e->globalPosition());QString hover;for(const auto &c:controls_)if(c.enabled&&pointInside(c.rect,e->position())&&c.role!=QAccessible::List)hover=c.id;if(hover!=hovered_){hovered_=hover;requestUpdate();}return true;}
 case QEvent::MouseButtonRelease:{auto e=static_cast<QMouseEvent*>(event);if(e->button()==Qt::LeftButton&&e->source()==Qt::MouseEventNotSynthesized){pointerEnd(-1,e->position());return true;}break;}
 case QEvent::TouchBegin:case QEvent::TouchUpdate:case QEvent::TouchEnd:{auto e=static_cast<QTouchEvent*>(event);for(const auto &point:e->points()){switch(point.state()){case QEventPoint::State::Pressed:pointerBegin(point.id(),point.position(),point.globalPosition());break;case QEventPoint::State::Updated:pointerMove(point.id(),point.position(),point.globalPosition());break;case QEventPoint::State::Released:pointerEnd(point.id(),point.position());break;default:break;}}e->accept();return true;}
 case QEvent::TouchCancel:cancelContacts();return true;
 case QEvent::Wheel:{
  auto e=static_cast<QWheelEvent*>(event);QPointF delta=e->pixelDelta().isNull()?QPointF(e->angleDelta())/3:QPointF(e->pixelDelta());
  for(const auto &control:accessibleControls())if(control.enabled&&control.role==QAccessible::List&&pointInside(control.rect,e->position())){
   if(state_.settingsOpen())settings_.scroll(-delta.y());
   else{const bool flow=control.action=="scroll/expanded";const qreal amount=flow?-delta.y():-(delta.x()!=0?delta.x():delta.y());const QString area=flow?"expanded":"candidates";scroll(area,amount);finishScroll(area,amount>0,amount<0);}
   return true;
  }
  return true;
 }
 case QEvent::Leave:hovered_.clear();requestUpdate();break;
 case QEvent::UngrabMouse:cancelContacts();break;
 case QEvent::DevicePixelRatioChange:updateCorners(true);requestUpdate();break;
 default:break;
 }
 return QWindow::event(event);
}
