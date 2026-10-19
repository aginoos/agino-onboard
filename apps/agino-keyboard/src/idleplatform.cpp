// SPDX-License-Identifier: GPL-3.0-or-later
#include "idleplatform.h"
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QDebug>
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>
#include "peercheck.h"
#include "log.h"
#include <QVariantMap>
#include <cmath>
#include <unistd.h>
#include <wayland-client.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
struct TrayPixmap{int width=0,height=0;QByteArray pixels;};
Q_DECLARE_METATYPE(TrayPixmap)
QDBusArgument &operator<<(QDBusArgument &a,const TrayPixmap &p){a.beginStructure();a<<p.width<<p.height<<p.pixels;a.endStructure();return a;}
const QDBusArgument &operator>>(const QDBusArgument &a,TrayPixmap &p){a.beginStructure();a>>p.width>>p.height>>p.pixels;a.endStructure();return a;}
struct MenuGroup{int id=0;QVariantMap properties;};
Q_DECLARE_METATYPE(MenuGroup)
QDBusArgument &operator<<(QDBusArgument &a,const MenuGroup &p){a.beginStructure();a<<p.id<<p.properties;a.endStructure();return a;}
const QDBusArgument &operator>>(const QDBusArgument &a,MenuGroup &p){a.beginStructure();a>>p.id>>p.properties;a.endStructure();return a;}
struct TrayTooltip{QString icon;QList<TrayPixmap> pixmaps;QString title,description;};
Q_DECLARE_METATYPE(TrayTooltip)
QDBusArgument &operator<<(QDBusArgument &a,const TrayTooltip &p){a.beginStructure();a<<p.icon<<p.pixmaps<<p.title<<p.description;a.endStructure();return a;}
const QDBusArgument &operator>>(const QDBusArgument &a,TrayTooltip &p){a.beginStructure();a>>p.icon>>p.pixmaps>>p.title>>p.description;a.endStructure();return a;}
struct MenuLayout{int id=0;QVariantMap properties;QList<QDBusVariant> children;};
Q_DECLARE_METATYPE(MenuLayout)
QDBusArgument &operator<<(QDBusArgument &a,const MenuLayout &p){a.beginStructure();a<<p.id<<p.properties<<p.children;a.endStructure();return a;}
const QDBusArgument &operator>>(const QDBusArgument &a,MenuLayout &p){a.beginStructure();a>>p.id>>p.properties>>p.children;a.endStructure();return a;}
namespace {
const QString accel=QStringLiteral("org.kde.kglobalaccel");
const QString watcher=QStringLiteral("org.kde.StatusNotifierWatcher");
const QString device=QStringLiteral("org.agino.KeyboardDevice1");
const QString session=QStringLiteral("org.deepin.dde.SessionManager1");
const QString component=QStringLiteral("agino-keyboard");
const QString action=QStringLiteral("show-keyboard");
// Built when used: the friendly names are translated and the translator is installed after static initialization.
QStringList actionId(){return {component,action,QCoreApplication::translate("Keyboard","Screen Keyboard"),QCoreApplication::translate("Keyboard","Show Screen Keyboard")};}
bool owner(const QDBusConnection &bus,const QString &name,const QDBusMessage &m){
 const auto reply=bus.interface()->serviceOwner(name);
 return reply.isValid()&&!reply.value().isEmpty()&&reply.value()==m.service();
}
QDBusMessage method(const QString &service,const QString &path,const QString &interface,const QString &name,const QVariantList &args={}){
 auto m=QDBusMessage::createMethodCall(service,path,interface,name);m.setArguments(args);m.setAutoStartService(false);return m;
}
QVariantMap menuProperties(int id){
 if(id==0)return {{"children-display","submenu"}};
 if(id==3)return {{"type","separator"}};
 return {{"label",id==1?QCoreApplication::translate("Keyboard","Show Screen Keyboard"):id==2?QCoreApplication::translate("Keyboard","Settings"):QCoreApplication::translate("Keyboard","Quit")},{"enabled",true},{"visible",true}};
}
MenuLayout layout(int id,int depth){
 MenuLayout result{id,menuProperties(id),{}};
 if(id==0&&depth!=0)for(int i=1;i<=4;++i)result.children<<QDBusVariant(QVariant::fromValue(layout(i,0)));
 return result;
}
}
struct IdlePlatform::Private: QDBusVirtualObject {
 IdlePlatform *q;
 QDBusConnection bus=QDBusConnection::sessionBus();
 bool wl=false,layer=false,text=false,source=false,locked=false,saver=false,sleep=false;
 // Programs seen behind the names on an installed agino desktop: dde-session (package dde-session) owns SessionManager1 and emits
 // LockedChanged; dde-session-daemon (dde-daemon) owns org.freedesktop.ScreenSaver.
 PeerCheck::Gate sessionGate{session,{"/usr/bin/dde-session"}},saverGate{"org.freedesktop.ScreenSaver",{"/usr/lib/deepin-daemon/dde-session-daemon"}};
 Display *display=nullptr;int opcode=0;QSocketNotifier *notifier=nullptr;
 QString shortcut,shortcutFailure,shortcutPath;quint64 generation=0;bool shortcutConfigured=false;quint64 lockRevision=0;
 explicit Private(IdlePlatform *p):QDBusVirtualObject(p),q(p){}
 ~Private() override {unregisterShortcut();bus.unregisterObject("/StatusNotifierItem");bus.unregisterObject("/MenuBar");delete notifier;if(display)XCloseDisplay(display);}
 void publishLock(){emit q->lockedChanged(locked||saver||sleep);}
 // One log line per change of a lock source (the sources are signalled again with the same value now and then).
 void noteChange(const char *what,bool before,bool now,const char *on,const char *off){if(before!=now)qCInfo(lcSession())<<what<<(now?on:off);}
 void unregisterShortcut(){
  ++generation;
  if(!shortcutPath.isEmpty())bus.disconnect(accel,shortcutPath,"org.kde.kglobalaccel.Component","globalShortcutPressed",q,SLOT(shortcutPressed(QString,QString,qlonglong,QDBusMessage)));
  shortcutPath.clear();
  if(bus.isConnected())bus.asyncCall(method(accel,"/kglobalaccel","org.kde.KGlobalAccel","unRegister",{actionId()}));
 }
 void bindShortcut(){
  if(!shortcutConfigured)return;
  unregisterShortcut();const auto serial=generation;int key=IdlePlatform::parseShortcut(shortcut);
  if(!key){shortcutFailure=QCoreApplication::translate("Keyboard","A shortcut needs a modifier key and a valid key");qCWarning(lcSession()).noquote()<<"Global shortcut not registered:"<<shortcutFailure<<"(configured:"<<shortcut<<")";emit q->capabilitiesChanged();return;}
  shortcutFailure=QCoreApplication::translate("Keyboard","Shortcut registration failed");
  auto registration=method(accel,"/kglobalaccel","org.kde.KGlobalAccel","doRegister",{actionId()});registration.setAutoStartService(true);
  auto *w=new QDBusPendingCallWatcher(bus.asyncCall(registration,1000),q);
  QObject::connect(w,&QDBusPendingCallWatcher::finished,q,[this,w,serial,key]{
   QDBusPendingReply<> reply=*w;w->deleteLater();if(serial!=generation)return;
   if(reply.isError()){qCWarning(lcSession()).noquote()<<"Global shortcut registration failed:"<<shortcutFailure<<"(doRegister:"<<reply.error().name()<<")";emit q->capabilitiesChanged();return;}
   auto *s=new QDBusPendingCallWatcher(bus.asyncCall(method(accel,"/kglobalaccel","org.kde.KGlobalAccel","setShortcut",{actionId(),QVariant::fromValue(QList<int>{key}),uint(6)}),1000),q);
   QObject::connect(s,&QDBusPendingCallWatcher::finished,q,[this,s,serial,key]{
    QDBusPendingReply<QList<int>> assigned=*s;s->deleteLater();if(serial!=generation)return;
    if(assigned.isError()||!assigned.value().contains(key)){qCWarning(lcSession()).noquote()<<"Global shortcut registration failed:"<<shortcutFailure<<(assigned.isError()?QStringLiteral("(setShortcut: %1)").arg(assigned.error().name()):QStringLiteral("(not assigned: another action may hold it)"));emit q->capabilitiesChanged();return;}
    auto *c=new QDBusPendingCallWatcher(bus.asyncCall(method(accel,"/kglobalaccel","org.kde.KGlobalAccel","getComponent",{component}),1000),q);
    QObject::connect(c,&QDBusPendingCallWatcher::finished,q,[this,c,serial]{
     QDBusPendingReply<QDBusObjectPath> path=*c;c->deleteLater();if(serial!=generation)return;
     if(!path.isError()){
      shortcutPath=path.value().path();
      if(bus.connect(accel,shortcutPath,"org.kde.kglobalaccel.Component","globalShortcutPressed",q,SLOT(shortcutPressed(QString,QString,qlonglong,QDBusMessage)))){shortcutFailure.clear();qCInfo(lcSession())<<"Global shortcut registered";}
      else qCWarning(lcSession()).noquote()<<"Global shortcut registration failed:"<<shortcutFailure<<"(cannot connect to the signal of"<<shortcutPath<<")";
     }else qCWarning(lcSession()).noquote()<<"Global shortcut registration failed:"<<shortcutFailure<<"(getComponent:"<<path.error().name()<<")";
     emit q->capabilitiesChanged();
    });
   });
  });
 }
 void registerTray(){
  auto *w=new QDBusPendingCallWatcher(bus.asyncCall(method(watcher,"/StatusNotifierWatcher",watcher,"RegisterStatusNotifierItem",{QStringLiteral("/StatusNotifierItem")})),q);
  QObject::connect(w,&QDBusPendingCallWatcher::finished,q,[w]{QDBusPendingReply<> reply=*w;w->deleteLater();if(reply.isError())qCWarning(lcSession()).noquote()<<"Tray registration failed:"<<reply.error().name();else qCInfo(lcSession())<<"Tray item registered";});
 }
 QVariant trayProperty(const QString &name)const {
  if(name=="Category")return "ApplicationStatus";
  if(name=="Id")return component;
  if(name=="Title")return QCoreApplication::translate("Keyboard","Screen Keyboard");
  if(name=="Status")return "Active";
  if(name=="IconName")return component;
  if(name=="IconThemePath"||name=="AttentionIconName"||name=="OverlayIconName")return QString();
  if(name=="WindowId")return uint(0);
  if(name=="ItemIsMenu")return false;
  if(name=="Menu")return QVariant::fromValue(QDBusObjectPath("/MenuBar"));
  if(name=="ToolTip")return QVariant::fromValue(TrayTooltip{component,{},QCoreApplication::translate("Keyboard","Screen Keyboard"),{}});
  if(name.endsWith("Pixmap"))return QVariant::fromValue(QList<TrayPixmap>());
  return {};
 }
 void activateMenu(int id){if(id==1)emit q->showRequested();else if(id==2)emit q->settingsRequested();else if(id==4)emit q->quitRequested();}
 QString introspect(const QString &path)const override;
 bool handleMessage(const QDBusMessage &m,const QDBusConnection &connection)override;
 void readX(){while(display&&XPending(display)){
  XEvent event;XNextEvent(display,&event);
  if(event.type!=GenericEvent||event.xcookie.extension!=opcode||!XGetEventData(display,&event.xcookie))continue;
  auto *raw=static_cast<XIRawEvent*>(event.xcookie.data);int count=0;auto *info=XIQueryDevice(display,raw->sourceid,&count);
  const bool synthetic=info&&count&&QByteArray(info[0].name).contains("XTEST");if(info)XIFreeDeviceInfo(info);
  if(!synthetic){int kind=0;if(event.xcookie.evtype==XI_RawTouchBegin)kind=2;else if(event.xcookie.evtype==XI_RawKeyPress)kind=4;else if(event.xcookie.evtype==XI_RawButtonPress&&!(raw->flags&XIPointerEmulated))kind=1;if(kind)emit q->sourceChanged(kind,0,0);}
  XFreeEventData(display,&event.xcookie);
 }}
};
QString IdlePlatform::Private::introspect(const QString &path)const {
 if(path=="/MenuBar")return QStringLiteral(R"xml(<interface name="com.canonical.dbusmenu">
 <property name="Version" type="u" access="read"/><property name="TextDirection" type="s" access="read"/><property name="Status" type="s" access="read"/><property name="IconThemePath" type="as" access="read"/>
 <method name="GetLayout"><arg direction="in" type="i"/><arg direction="in" type="i"/><arg direction="in" type="as"/><arg direction="out" type="u"/><arg direction="out" type="(ia{sv}av)"/></method>
 <method name="GetGroupProperties"><arg direction="in" type="ai"/><arg direction="in" type="as"/><arg direction="out" type="a(ia{sv})"/></method>
 <method name="GetProperty"><arg direction="in" type="i"/><arg direction="in" type="s"/><arg direction="out" type="v"/></method>
 <method name="Event"><arg direction="in" type="i"/><arg direction="in" type="s"/><arg direction="in" type="v"/><arg direction="in" type="u"/></method>
 <method name="EventGroup"><arg direction="in" type="a(isvu)"/><arg direction="out" type="ai"/></method>
 <method name="AboutToShow"><arg direction="in" type="i"/><arg direction="out" type="b"/></method>
 <method name="AboutToShowGroup"><arg direction="in" type="ai"/><arg direction="out" type="ai"/><arg direction="out" type="ai"/></method>
 </interface>)xml");
 return QStringLiteral(R"xml(<interface name="org.kde.StatusNotifierItem">
 <property name="Category" type="s" access="read"/><property name="Id" type="s" access="read"/><property name="Title" type="s" access="read"/><property name="Status" type="s" access="read"/>
 <property name="WindowId" type="u" access="read"/><property name="IconName" type="s" access="read"/><property name="IconThemePath" type="s" access="read"/><property name="IconPixmap" type="a(iiay)" access="read"/>
 <property name="OverlayIconName" type="s" access="read"/><property name="OverlayIconPixmap" type="a(iiay)" access="read"/><property name="AttentionIconName" type="s" access="read"/><property name="AttentionIconPixmap" type="a(iiay)" access="read"/>
 <property name="ToolTip" type="(sa(iiay)ss)" access="read"/><property name="ItemIsMenu" type="b" access="read"/><property name="Menu" type="o" access="read"/>
 <method name="Activate"><arg direction="in" type="i"/><arg direction="in" type="i"/></method><method name="SecondaryActivate"><arg direction="in" type="i"/><arg direction="in" type="i"/></method>
 <method name="ContextMenu"><arg direction="in" type="i"/><arg direction="in" type="i"/></method><method name="Scroll"><arg direction="in" type="i"/><arg direction="in" type="s"/></method>
 </interface>)xml");
}
bool IdlePlatform::Private::handleMessage(const QDBusMessage &m,const QDBusConnection &connection){
 const auto args=m.arguments();const auto name=m.member();QVariantList result;
 const bool menu=m.path()=="/MenuBar";
 auto property=[&](const QString &key)->QVariant{
  if(!menu)return trayProperty(key);
  if(key=="Version")return uint(3);
  if(key=="TextDirection")return "ltr";
  if(key=="Status")return "normal";
  if(key=="IconThemePath")return QStringList();
  return {};
 };
 if(m.interface()=="org.freedesktop.DBus.Properties"){
  const QString expected=menu?QStringLiteral("com.canonical.dbusmenu"):QStringLiteral("org.kde.StatusNotifierItem");
  if(args.isEmpty()||args[0].toString()!=expected)return false;
  if(name=="Get"&&args.size()==2){auto value=property(args[1].toString());if(!value.isValid()){connection.send(m.createErrorReply("org.freedesktop.DBus.Error.UnknownProperty","Unknown property"));return true;}result<<QVariant::fromValue(QDBusVariant(value));}
  else if(name=="GetAll"&&args.size()==1){
   QVariantMap map;const auto keys=menu?QStringList{"Version","TextDirection","Status","IconThemePath"}:QStringList{"Category","Id","Title","Status","WindowId","IconName","IconThemePath","IconPixmap","OverlayIconName","OverlayIconPixmap","AttentionIconName","AttentionIconPixmap","ToolTip","ItemIsMenu","Menu"};
   for(const auto &key:keys)map.insert(key,property(key));result<<map;
  }else return false;
 }else if(menu&&m.interface()=="com.canonical.dbusmenu"){
  if(name=="GetLayout"&&args.size()==3){const int id=args[0].toInt();if(id<0||id>4)return false;result<<uint(1)<<QVariant::fromValue(layout(id,args[1].toInt()));}
  else if(name=="GetProperty"&&args.size()==2){auto value=menuProperties(args[0].toInt()).value(args[1].toString());if(!value.isValid())return false;result<<QVariant::fromValue(QDBusVariant(value));}
  else if(name=="GetGroupProperties"&&args.size()==2){
   const auto ids=qdbus_cast<QList<int>>(args[0]);QList<MenuGroup> groups;
   for(int id:ids)if(id>=0&&id<=4)groups<<MenuGroup{id,menuProperties(id)};
   result<<QVariant::fromValue(groups);
  }else if(name=="AboutToShow"&&args.size()==1)result<<false;
  else if(name=="AboutToShowGroup"&&args.size()==1)result<<QVariant::fromValue(QList<int>())<<QVariant::fromValue(QList<int>());
  else if(name=="Event"&&args.size()==4){if(args[1].toString()=="clicked")activateMenu(args[0].toInt());}
  else if(name=="EventGroup"&&args.size()==1){
   const auto events=qvariant_cast<QDBusArgument>(args[0]);events.beginArray();while(!events.atEnd()){int id;QString event;QDBusVariant data;uint time;events.beginStructure();events>>id>>event>>data>>time;events.endStructure();if(event=="clicked")activateMenu(id);}events.endArray();result<<QVariant::fromValue(QList<int>());
  }else return false;
 }else if(!menu&&m.interface()=="org.kde.StatusNotifierItem"){
  if(name=="Activate"&&args.size()==2)emit q->toggleRequested();
  else if((name=="SecondaryActivate"||name=="ContextMenu"||name=="Scroll")&&args.size()==2){}
  else return false;
 }else return false;
 connection.send(m.createReply(result));return true;
}
static bool waylandSession(){const auto platform=qgetenv("QT_QPA_PLATFORM");return platform.startsWith("wayland")||(platform.isEmpty()&&(qgetenv("XDG_SESSION_TYPE")=="wayland"||!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")));}
bool IdlePlatform::displayAvailable(){if(waylandSession())return true;auto display=XOpenDisplay(nullptr);if(display)XCloseDisplay(display);return display;}
static int displayExitStatus=1;
void IdlePlatform::exitWithDisplay(int status){displayExitStatus=status;XSetIOErrorHandler([](Display *)->int{qCInfo(lcSession())<<"The X11 display is gone, the session has ended";_exit(displayExitStatus);});}
IdlePlatform::IdlePlatform(QObject *parent):QObject(parent),d(std::make_unique<Private>(this)){
 qDBusRegisterMetaType<TrayPixmap>();qDBusRegisterMetaType<QList<TrayPixmap>>();qDBusRegisterMetaType<TrayTooltip>();qDBusRegisterMetaType<MenuGroup>();qDBusRegisterMetaType<QList<MenuGroup>>();qDBusRegisterMetaType<MenuLayout>();
 d->wl=waylandSession();
 auto bus=d->bus;
 bus.connect(session,"/org/deepin/dde/SessionManager1",session,"LockedChanged",this,SLOT(lock(bool,QDBusMessage)));
 bus.connect("org.freedesktop.ScreenSaver","/ScreenSaver","org.freedesktop.ScreenSaver","ActiveChanged",this,SLOT(screensaver(bool,QDBusMessage)));
 QDBusConnection::systemBus().connect("org.freedesktop.login1","/org/freedesktop/login1","org.freedesktop.login1.Manager","PrepareForSleep",this,SLOT(sleeping(bool,QDBusMessage)));
 // The start snapshot is asked of the verified owner by its unique name (the sender of a reply is not set in the message QtDBus hands over).
 const QString sessionOwner=bus.interface()?bus.interface()->serviceOwner(session).value():QString();
 if(!sessionOwner.isEmpty()&&d->sessionGate.accept(bus,sessionOwner)){
  auto *w=new QDBusPendingCallWatcher(bus.asyncCall(method(sessionOwner,"/org/deepin/dde/SessionManager1","org.freedesktop.DBus.Properties","Get",{session,QStringLiteral("Locked")}),1000),this);
  const auto lockRevision=d->lockRevision;
  connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,lockRevision]{
   QDBusPendingReply<QDBusVariant> reply=*w;
   // A startup snapshot may arrive after a newer signal or owner replacement.
   if(!reply.isError()&&lockRevision==d->lockRevision){const bool value=reply.value().variant().toBool();d->noteChange("Session lock (start state):",d->locked,value,"locked","unlocked");d->locked=value;d->publishLock();}
   else if(reply.isError())qCWarning(lcSession()).noquote()<<"Cannot read the lock state of the session:"<<reply.error().name();
   w->deleteLater();
  });
 }
 if(d->wl){
  auto *display=wl_display_connect(nullptr);
  if(display){auto *registry=wl_display_get_registry(display);static const wl_registry_listener listener={[](void *data,wl_registry*,uint32_t,const char *name,uint32_t){auto *p=static_cast<Private*>(data);QByteArray n(name);if(n=="zwlr_layer_shell_v1")p->layer=true;if(n=="zwp_text_input_manager_v1"||n=="zwp_text_input_manager_v2"||n=="zwp_text_input_manager_v3")p->text=true;},[](void*,wl_registry*,uint32_t){}};wl_registry_add_listener(registry,&listener,d.get());wl_display_roundtrip(display);wl_registry_destroy(registry);wl_display_disconnect(display);}else qCWarning(lcSession())<<"Cannot connect to the Wayland display";
  d->source=bus.interface()->isServiceRegistered(device).value();
  bus.connect(device,"/org/agino/KeyboardDevice1",device,"SourceChanged",this,SLOT(deviceSource(uint,double,double,QDBusMessage)));
 }else{
  d->display=XOpenDisplay(nullptr);
  if(d->display){int event,error,major=2,minor=2;d->layer=d->text=true;
   if(XQueryExtension(d->display,"XInputExtension",&d->opcode,&event,&error)&&XIQueryVersion(d->display,&major,&minor)==Success){unsigned char mask[XIMaskLen(XI_LASTEVENT)]={};XISetMask(mask,XI_RawTouchBegin);XISetMask(mask,XI_RawKeyPress);XISetMask(mask,XI_RawButtonPress);XIEventMask events{XIAllMasterDevices,sizeof(mask),mask};d->source=XISelectEvents(d->display,DefaultRootWindow(d->display),&events,1)==Success;}
   d->notifier=new QSocketNotifier(ConnectionNumber(d->display),QSocketNotifier::Read,this);connect(d->notifier,&QSocketNotifier::activated,this,[this]{d->readX();});XFlush(d->display);
  }else qCWarning(lcSession())<<"Cannot open the X11 display";
 }
 {QStringList missing;if(!d->layer)missing<<"layer-shell";if(!d->text)missing<<"text-input";if(!d->source)missing<<"input device detection";qCInfo(lcSession()).noquote()<<"Platform:"<<(d->wl?"Wayland":"X11")<<(missing.isEmpty()?QStringLiteral("nothing missing"):QStringLiteral("missing: %1").arg(missing.join(", ")));}
 auto *services=new QDBusServiceWatcher(this);services->setConnection(bus);services->setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);services->addWatchedService(watcher);services->addWatchedService(accel);services->addWatchedService(session);if(d->wl)services->addWatchedService(device);
 connect(services,&QDBusServiceWatcher::serviceOwnerChanged,this,[this](const QString &name,const QString &,const QString &newOwner){if(name==watcher&&!newOwner.isEmpty())d->registerTray();else if(name==accel){if(newOwner.isEmpty()){qCInfo(lcSession())<<"kglobalaccel went away, the global shortcut is gone";d->unregisterShortcut();d->shortcutFailure=QCoreApplication::translate("Keyboard","Shortcut registration failed");emit capabilitiesChanged();}else d->bindShortcut();}else if(name==session){++d->lockRevision;}else if(name==device){d->source=!newOwner.isEmpty();qCInfo(lcSession())<<"Input device bridge"<<(d->source?"available":"gone");emit capabilitiesChanged();}});
 bus.registerVirtualObject("/StatusNotifierItem",d.get());bus.registerVirtualObject("/MenuBar",d.get());d->registerTray();
}
IdlePlatform::~IdlePlatform()=default;
bool IdlePlatform::wayland()const{return d->wl;}
bool IdlePlatform::automaticReady()const{return d->layer&&d->text&&d->source;}
QString IdlePlatform::status()const{
 if(d->wl)return !d->layer||!d->text?QCoreApplication::translate("DesktopAdapter","The Wayland docking plugin or the layer-shell / text-input protocol is unavailable"):d->source?QCoreApplication::translate("DesktopAdapter","Wayland: touch detection and manual activation are supported"):QCoreApplication::translate("DesktopAdapter","Wayland: the input device cannot be identified, manual activation only");
 return d->source?QCoreApplication::translate("DesktopAdapter","X11: touch detection and manual activation are supported"):QCoreApplication::translate("DesktopAdapter","X11: the input device cannot be identified, manual activation only");
}
QString IdlePlatform::shortcutError()const{return d->shortcutFailure;}
void IdlePlatform::setShortcut(QString text){if(d->shortcutConfigured&&d->shortcut==text)return;d->shortcutConfigured=true;d->shortcut=text;d->bindShortcut();}
void IdlePlatform::lock(bool value,const QDBusMessage &m){if(d->sessionGate.accept(d->bus,m.service())){++d->lockRevision;d->noteChange("Session lock:",d->locked,value,"locked","unlocked");d->locked=value;d->publishLock();}}
void IdlePlatform::screensaver(bool value,const QDBusMessage &m){if(d->saverGate.accept(d->bus,m.service())){d->noteChange("Screensaver:",d->saver,value,"active","inactive");d->saver=value;d->publishLock();}}
void IdlePlatform::sleeping(bool value,const QDBusMessage &m){if(owner(QDBusConnection::systemBus(),"org.freedesktop.login1",m)){d->noteChange("Sleep:",d->sleep,value,"preparing for sleep","resumed");d->sleep=value;d->publishLock();}}
void IdlePlatform::deviceSource(uint kind,double x,double y,const QDBusMessage &m){if(kind>=1&&kind<=4&&std::isfinite(x)&&std::isfinite(y)&&owner(d->bus,device,m))emit sourceChanged(int(kind),x,y);}
void IdlePlatform::shortcutPressed(const QString &app,const QString &name,qlonglong,const QDBusMessage &m){if(app==component&&name==action&&m.path()==d->shortcutPath&&owner(d->bus,accel,m))emit toggleRequested();}
int IdlePlatform::parseShortcut(const QString &text){
 int modifiers=0,key=0;
 for(const auto &part:text.split('+')){const auto token=part.trimmed().toLower();int modifier=0;
  if(token=="ctrl"||token=="control")modifier=Qt::CTRL;else if(token=="alt")modifier=Qt::ALT;else if(token=="shift")modifier=Qt::SHIFT;else if(token=="meta"||token=="super")modifier=Qt::META;
  if(modifier){if(modifiers&modifier)return 0;modifiers|=modifier;continue;}
  if(key||token.isEmpty())return 0;
  static const QHash<QString,int> special{{"esc",Qt::Key_Escape},{"escape",Qt::Key_Escape},{"tab",Qt::Key_Tab},{"backspace",Qt::Key_Backspace},{"return",Qt::Key_Return},{"enter",Qt::Key_Return},{"insert",Qt::Key_Insert},{"ins",Qt::Key_Insert},{"delete",Qt::Key_Delete},{"del",Qt::Key_Delete},{"home",Qt::Key_Home},{"end",Qt::Key_End},{"left",Qt::Key_Left},{"up",Qt::Key_Up},{"right",Qt::Key_Right},{"down",Qt::Key_Down},{"pageup",Qt::Key_PageUp},{"pgup",Qt::Key_PageUp},{"pagedown",Qt::Key_PageDown},{"pgdown",Qt::Key_PageDown},{"space",Qt::Key_Space},{"plus",Qt::Key_Plus},{"minus",Qt::Key_Minus}};
  key=special.value(token);
  if(!key&&token.startsWith('f')){bool ok=false;int n=token.mid(1).toInt(&ok);if(ok&&n>=1&&n<=35)key=Qt::Key_F1+n-1;}
  if(!key&&token.size()==1&&token[0].unicode()>32&&token[0].unicode()<127)key=token[0].toUpper().unicode();
  if(!key)return 0;
 }
 return modifiers&&key?modifiers|key:0;
}
