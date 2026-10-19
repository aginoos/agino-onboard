#include "desktopadapter.h"
#include <QGuiApplication>
#include <QDynamicPropertyChangeEvent>
#include <QTimer>
#include <cmath>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusConnectionInterface>
#include <QDBusPendingReply>
#include <QDebug>
#include <QSet>
#include "peercheck.h"
#include "waylandproperties.h"
#ifdef AGINO_WAYLAND_SHELL
#include <qpa/qplatformnativeinterface.h>
#endif
#include <wayland-client.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>
namespace {
// SEC-20: a lock / screensaver signal counts only from the owner of the well-known name AND only when the program behind it is an
// expected one (another program of the same user could otherwise claim the name and hide the keyboard / suspend Fcitx5).
// Programs seen behind the names on an installed agino desktop: dde-session (package dde-session) owns SessionManager1 and emits
// LockedChanged; dde-session-daemon (dde-daemon) owns org.freedesktop.ScreenSaver.
PeerCheck::Gate sessionGate{"org.deepin.dde.SessionManager1",{"/usr/bin/dde-session"}};
PeerCheck::Gate saverGate{"org.freedesktop.ScreenSaver",{"/usr/lib/deepin-daemon/dde-session-daemon"}};
}
DesktopAdapter::DesktopAdapter(QObject *p,bool trackInputSource):QObject(p){
 wayland_=QGuiApplication::platformName().startsWith("wayland");
 auto bus=QDBusConnection::sessionBus();
 bus.connect("org.deepin.dde.SessionManager1","/org/deepin/dde/SessionManager1","org.deepin.dde.SessionManager1","LockedChanged",this,SLOT(sessionLock(bool,QDBusMessage)));
 bus.connect("org.freedesktop.ScreenSaver","/ScreenSaver","org.freedesktop.ScreenSaver","ActiveChanged",this,SLOT(screensaverFrom(bool,QDBusMessage)));
 QDBusConnection::systemBus().connect("org.freedesktop.login1","/org/freedesktop/login1","org.freedesktop.login1.Manager","PrepareForSleep",this,SLOT(sleeping(bool)));
 // The start snapshot is asked of the verified owner by its unique name (the sender of a reply is not set in the message QtDBus hands over).
 const QString sessionOwner=bus.interface()?bus.interface()->serviceOwner("org.deepin.dde.SessionManager1").value():QString();
 auto m=QDBusMessage::createMethodCall(sessionOwner.isEmpty()?QStringLiteral("org.deepin.dde.SessionManager1"):sessionOwner,"/org/deepin/dde/SessionManager1","org.freedesktop.DBus.Properties","Get");m.setArguments({"org.deepin.dde.SessionManager1","Locked"});m.setAutoStartService(false);
 auto *ownerWatcher=new QDBusServiceWatcher("org.deepin.dde.SessionManager1",bus,QDBusServiceWatcher::WatchForOwnerChange,this);
 connect(ownerWatcher,&QDBusServiceWatcher::serviceOwnerChanged,this,[this]{++lockRevision_;});
 const auto revision=lockRevision_;
 if(!sessionOwner.isEmpty()&&sessionGate.accept(QDBusConnection::sessionBus(),sessionOwner)){
  auto w=new QDBusPendingCallWatcher(bus.asyncCall(m,1000),this);
  connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,revision]{
   QDBusPendingReply<QDBusVariant> r=*w;
   // Never undo a newer lock signal or trust a snapshot from a replaced owner.
   if(!r.isError()&&revision==lockRevision_)lock(r.value().variant().toBool());
   w->deleteLater();
  });
 }
 connect(qGuiApp,&QGuiApplication::screenRemoved,this,[this](QScreen *screen){if(screen==output_){emit outputChanged();setOutput(qGuiApp->primaryScreen());}});
 if(wayland_){detectWayland();return;}
 auto d=XOpenDisplay(nullptr);display_=d;if(!d)return;
 caps_.layer=caps_.text=true;int event,error,major=2,minor=2;
 if(XQueryExtension(d,"XInputExtension",&xiOpcode_,&event,&error)&&XIQueryVersion(d,&major,&minor)==Success){if(trackInputSource){unsigned char mask[XIMaskLen(XI_LASTEVENT)]={};XISetMask(mask,XI_RawTouchBegin);XISetMask(mask,XI_RawKeyPress);XISetMask(mask,XI_RawButtonPress);XIEventMask events{XIAllMasterDevices,sizeof(mask),mask};XISelectEvents(d,DefaultRootWindow(d),&events,1);}caps_.source=true;}
 if(trackInputSource){notifier_=new QSocketNotifier(ConnectionNumber(d),QSocketNotifier::Read,this);connect(notifier_,&QSocketNotifier::activated,this,[this]{readXEvents();});}
 XFlush(d);
}
DesktopAdapter::~DesktopAdapter(){detach();delete notifier_;if(display_)XCloseDisplay(static_cast<Display*>(display_));}
void DesktopAdapter::detectWayland(){
 auto display=wl_display_connect(nullptr);if(!display)return;auto registry=wl_display_get_registry(display);
 static const wl_registry_listener listener={[](void *data,wl_registry *,uint32_t,const char *name,uint32_t){auto c=static_cast<DesktopCapabilities*>(data);QByteArray n(name);if(n=="zwlr_layer_shell_v1")c->layer=true;if(n=="zwp_text_input_manager_v1"||n=="zwp_text_input_manager_v2"||n=="zwp_text_input_manager_v3")c->text=true;},[](void *,wl_registry *,uint32_t){}};
 wl_registry_add_listener(registry,&listener,&caps_);wl_display_roundtrip(display);wl_registry_destroy(registry);wl_display_disconnect(display);
 // Check the actual selected integration before mapping any panel; never
 // substitute an ordinary shell window when our bundled plugin is unavailable.
#ifdef AGINO_WAYLAND_SHELL
 QWindow probe;probe.create();auto native=QGuiApplication::platformNativeInterface();
 caps_.layer=caps_.layer&&native&&native->nativeResourceForWindow(KeyboardWayland::IntegrationResource,&probe);
#else
 caps_.layer=false;   // built without the shell plugin: no Wayland panel
#endif
 auto bus=QDBusConnection::sessionBus();
 caps_.source=bus.interface()->isServiceRegistered("org.agino.KeyboardDevice1").value();
 bus.connect("org.agino.KeyboardDevice1","/org/agino/KeyboardDevice1","org.agino.KeyboardDevice1","SourceChanged",this,SLOT(deviceSource(uint,double,double)));
 auto watcher=new QDBusServiceWatcher("org.agino.KeyboardDevice1",bus,QDBusServiceWatcher::WatchForOwnerChange,this);
 connect(watcher,&QDBusServiceWatcher::serviceOwnerChanged,this,[this](const QString &,const QString &,const QString &owner){caps_.source=!owner.isEmpty();emit capabilitiesChanged();});
}
bool DesktopAdapter::attach(QWindow *w){
 if(!w)return false;
 detach();
 strutCleared_=false;window_=w;requestedSize_={};requestedOutput_=nullptr;window_->installEventFilter(this);window_->setFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::WindowDoesNotAcceptFocus);
 if(wayland_){if(!caps_.waylandReady())return false;w->setProperty(KeyboardWayland::ExclusiveZone,0);}
 else{if(!display_)return false;auto d=static_cast<Display*>(display_);auto type=XInternAtom(d,"_NET_WM_WINDOW_TYPE_DOCK",False);XChangeProperty(d,w->winId(),XInternAtom(d,"_NET_WM_WINDOW_TYPE",False),XA_ATOM,32,PropModeReplace,reinterpret_cast<unsigned char*>(&type),1);XWMHints hints{};hints.flags=InputHint;hints.input=False;XSetWMHints(d,w->winId(),&hints);}
 setOutput(w->screen());auto resized=[this]{if(wayland_&&floating_&&negotiatedSize_.isValid()&&window_->size()!=negotiatedSize_)return;updateGeometry();};connect(w,&QWindow::heightChanged,this,resized);connect(w,&QWindow::widthChanged,this,resized);return true;
}
void DesktopAdapter::detach(){
 if(window_){retainedSize_=window_->size();retainedOutput_=output_;restoringGeometry_=true;setPanelVisible(false);disconnect(window_,nullptr,this,nullptr);window_->removeEventFilter(this);}
 if(output_)disconnect(output_,nullptr,this,nullptr);
 window_.clear();output_.clear();
}
void DesktopAdapter::setOutput(QScreen *screen){
 if(!screen||!window_)return;
 if(screen!=output_){
  emit outputChanged();
  const bool remap=wayland_&&window_->isVisible();
  // QWindow::setScreen does not recreate a surface between virtual siblings.
  // A layer role's output is immutable, so remap it on the selected output.
  if(remap)window_->hide();
  if(output_)disconnect(output_,nullptr,this,nullptr);
  output_=screen;window_->setScreen(screen);
  connect(screen,&QScreen::geometryChanged,this,[this]{emit outputChanged();updateGeometry();});
  connect(screen,&QScreen::logicalDotsPerInchChanged,this,[this]{emit outputChanged();updateGeometry();});
  if(remap)window_->show();
 }
 updateGeometry();
}
void DesktopAdapter::updateGeometry(){
 if(configuring_||!window_||!output_)return;
 const auto g=output_->geometry();
 if(floating_){position_.setX(qBound(0.0,position_.x(),double(qMax(0,g.width()-window_->width()))));position_.setY(qBound(0.0,position_.y(),double(qMax(0,g.height()-window_->height()))));}
 if(wayland_){
  window_->setProperty(KeyboardWayland::Position,position_.toPoint());window_->setProperty(KeyboardWayland::Floating,floating_);
  if(window_->isVisible())window_->setProperty(KeyboardWayland::ExclusiveZone,floating_?-1:window_->height());
 }else{
  window_->setPosition(g.topLeft()+(floating_?position_.toPoint():QPoint((g.width()-window_->width())/2,g.height()-window_->height())));
  if(window_->isVisible())strut(!floating_);
 }
}
bool DesktopAdapter::eventFilter(QObject *object,QEvent *event){
 if(wayland_&&object==window_&&event->type()==QEvent::DynamicPropertyChange&&static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName()==KeyboardWayland::NegotiatedSize){
  const QSize actual=window_->property(KeyboardWayland::NegotiatedSize).toSize();if(actual.isValid()){
   if(floating_&&negotiatedOutput_==output_&&negotiatedSize_.width()==actual.width())position_.ry()+=negotiatedSize_.height()-actual.height();
   negotiatedSize_=actual;negotiatedOutput_=output_;QTimer::singleShot(0,this,[this]{updateGeometry();});
  }
 }
 return QObject::eventFilter(object,event);
}
void DesktopAdapter::configurePanel(QScreen *screen,QSize size){
 if(!window_||!screen)return;
 if(wayland_&&requestedSize_==size&&requestedOutput_==screen){if(!floating_||window_->size()==negotiatedSize_)updateGeometry();return;}
 requestedSize_=size;requestedOutput_=screen;
 // Grow/shrink the candidate strip above a floating keyboard, keeping its
 // bottom edge (and therefore its keys) stationary during composition.
 const QSize previousSize=restoringGeometry_?retainedSize_:window_->size();
 const QScreen *previousOutput=restoringGeometry_?retainedOutput_.data():output_.data();
 if(floating_&&(!wayland_||!negotiatedSize_.isValid())&&screen==previousOutput&&size.width()==previousSize.width())position_.ry()+=previousSize.height()-size.height();
 restoringGeometry_=false;
 configuring_=true;setOutput(screen);if(window_->size()!=size)window_->resize(size);configuring_=false;if(!wayland_||!floating_||!negotiatedSize_.isValid()||window_->size()==negotiatedSize_)updateGeometry();
}
void DesktopAdapter::moveBy(double dx,double dy){
 if(!window_||!output_||!window_->isVisible()||!std::isfinite(dx)||!std::isfinite(dy))return;
 const auto g=output_->geometry();
 if(!floating_)position_=wayland_?QPoint((g.width()-window_->width())/2,g.height()-window_->height()):window_->position()-g.topLeft();
 floating_=true;position_+=QPointF(qBound(-double(g.width()),dx,double(g.width())),qBound(-double(g.height()),dy,double(g.height())));updateGeometry();
}
void DesktopAdapter::strut(bool visible){if(!display_||!window_)return;
 // A floating panel reserves no space: once cleared, moving it needs no round trip to the X server.
 if(!visible&&strutCleared_)return;strutCleared_=!visible;
auto d=static_cast<Display*>(display_);unsigned long values[12]={};Window ignored;int x=0,y=0;XTranslateCoordinates(d,window_->winId(),DefaultRootWindow(d),0,0,&x,&y,&ignored);XWindowAttributes a{};XGetWindowAttributes(d,window_->winId(),&a);
 if(visible&&y+a.height>=DisplayHeight(d,DefaultScreen(d))){values[3]=DisplayHeight(d,DefaultScreen(d))-y;values[10]=qMax(0,x);values[11]=qMax(0,x+a.width-1);}XChangeProperty(d,window_->winId(),XInternAtom(d,"_NET_WM_STRUT_PARTIAL",False),XA_CARDINAL,32,PropModeReplace,reinterpret_cast<unsigned char*>(values),12);XChangeProperty(d,window_->winId(),XInternAtom(d,"_NET_WM_STRUT",False),XA_CARDINAL,32,PropModeReplace,reinterpret_cast<unsigned char*>(values),4);XFlush(d);
}
void DesktopAdapter::setPanelVisible(bool v){if(!window_)return;if(v){updateGeometry();if(wayland_)window_->setProperty(KeyboardWayland::ExclusiveZone,floating_?-1:window_->height());window_->show();if(!wayland_)strut(!floating_);}else{if(wayland_&&caps_.layer)window_->setProperty(KeyboardWayland::ExclusiveZone,0);else strut(false);window_->hide();}}
QString DesktopAdapter::status()const{if(wayland_)return !caps_.waylandReady()?tr("The Wayland docking plugin or the layer-shell / text-input protocol is unavailable"):caps_.source?tr("Wayland: touch detection and manual activation are supported"):tr("Wayland: the input device cannot be identified, manual activation only");return caps_.source?tr("X11: touch detection and manual activation are supported"):tr("X11: the input device cannot be identified, manual activation only");}
void DesktopAdapter::readXEvents(){auto d=static_cast<Display*>(display_);while(XPending(d)){XEvent event;XNextEvent(d,&event);if(event.type!=GenericEvent||event.xcookie.extension!=xiOpcode_||!XGetEventData(d,&event.xcookie))continue;auto raw=static_cast<XIRawEvent*>(event.xcookie.data);int count;auto info=XIQueryDevice(d,raw->sourceid,&count);bool synthetic=info&&count&&QByteArray(info[0].name).contains("XTEST");if(info)XIFreeDeviceInfo(info);if(!synthetic){if(event.xcookie.evtype==XI_RawTouchBegin)emit inputSourceChanged(KeyboardState::Touch);else if(event.xcookie.evtype==XI_RawKeyPress)emit inputSourceChanged(KeyboardState::PhysicalKeyboard);else if(event.xcookie.evtype==XI_RawButtonPress&&!(raw->flags&XIPointerEmulated))emit inputSourceChanged(KeyboardState::Mouse);}XFreeEventData(d,&event.xcookie);}}
void DesktopAdapter::sessionLock(bool value,const QDBusMessage &message){if(sessionGate.accept(QDBusConnection::sessionBus(),message.service()))lock(value);}
void DesktopAdapter::screensaverFrom(bool value,const QDBusMessage &message){if(saverGate.accept(QDBusConnection::sessionBus(),message.service()))screensaver(value);}
void DesktopAdapter::lock(bool v){++lockRevision_;locked_=v;emit lockedChanged(locked_||screensaver_||sleeping_);}
void DesktopAdapter::sleeping(bool v){sleeping_=v;emit lockedChanged(locked_||screensaver_||sleeping_);}

void DesktopAdapter::deviceSource(uint kind,double x,double y){
 if(kind<KeyboardState::Mouse||kind>KeyboardState::PhysicalKeyboard)return;
 if(kind==KeyboardState::Touch||kind==KeyboardState::Pen||kind==KeyboardState::Mouse){auto screen=qGuiApp->screenAt(QPoint(qRound(x),qRound(y)));if(screen&&screen!=targetOutput_){targetOutput_=screen;emit outputChanged();}}
 emit inputSourceChanged(KeyboardState::InputSource(kind));
}
QScreen *DesktopAdapter::targetOutput()const{
 if(wayland_)return qGuiApp->screens().contains(targetOutput_.data())?targetOutput_.data():output_.data();
 if(!display_)return output_;auto d=static_cast<Display*>(display_);Window focus;int revert;XGetInputFocus(d,&focus,&revert);if(focus==None||focus==PointerRoot)return output_;XWindowAttributes a{};Window ignored;int x,y;if(!XGetWindowAttributes(d,focus,&a)||!XTranslateCoordinates(d,focus,DefaultRootWindow(d),a.width/2,a.height/2,&x,&y,&ignored))return output_;auto screen=qGuiApp->screenAt(QPoint(x,y));return screen?screen:output_.data();
}

void DesktopAdapter::screensaver(bool v){screensaver_=v;emit lockedChanged(locked_||screensaver_||sleeping_);}

QVariantMap DesktopAdapter::placement()const{return {{"floating",floating_},{"floatX",position_.x()},{"floatY",position_.y()},{"outputName",output_?output_->name():retainedOutput_?retainedOutput_->name():QString()}};}
void DesktopAdapter::restorePlacement(const QVariantMap &value){
 floating_=value.value("floating").toBool();position_=QPointF(value.value("floatX").toDouble(),value.value("floatY").toDouble());
 // The controlling process can observe touch focus before this process exists.
 if(value.value("sourceKind").toInt()>=KeyboardState::Mouse&&value.value("sourceKind").toInt()<=KeyboardState::Pen){auto screen=qGuiApp->screenAt(QPoint(qRound(value.value("sourceX").toDouble()),qRound(value.value("sourceY").toDouble())));if(screen)targetOutput_=screen;}
}
