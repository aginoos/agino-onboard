// SPDX-License-Identifier: GPL-3.0-or-later
#include "idleplatform.h"
#include <QtTest>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusArgument>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QDBusObjectPath>
#include <QSignalSpy>
class AccelMock:public QDBusVirtualObject {
public:
 QList<int> assigned;int registrations=0,unregistrations=0,trayRegistrations=0;QString trayPath;QStringList lastAction;
 QString introspect(const QString &)const override{return {};}
 bool handleMessage(const QDBusMessage &m,const QDBusConnection &c)override{
  QVariantList reply;
  if(m.member()=="RegisterStatusNotifierItem"){++trayRegistrations;trayPath=m.arguments()[0].toString();}
  else if(m.member()=="doRegister"){++registrations;lastAction=m.arguments()[0].toStringList();}
  else if(m.member()=="unRegister"){++unregistrations;lastAction=m.arguments()[0].toStringList();}
  else if(m.member()=="setShortcut"){assigned=qdbus_cast<QList<int>>(m.arguments()[1]);reply<<QVariant::fromValue(assigned);}
  else if(m.member()=="getComponent")reply<<QVariant::fromValue(QDBusObjectPath("/component/agino_keyboard"));
  else return false;
  c.send(m.createReply(reply));return true;
 }
};
class DelayedLockMock:public QDBusVirtualObject {
public:
 QDBusMessage pending;
 QString introspect(const QString &)const override{return {};}
 bool handleMessage(const QDBusMessage &message,const QDBusConnection &)override{
  if(message.interface()!="org.freedesktop.DBus.Properties"||message.member()!="Get")return false;
  pending=message;return true;
 }
};
class IdlePlatformTest: public QObject {
 Q_OBJECT
private slots:
 QDBusMessage call(const QString &path,const QString &interface,const QString &member,const QVariantList &args={}){
  auto bus=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"idle-test-client");auto message=QDBusMessage::createMethodCall(QDBusConnection::sessionBus().baseService(),path,interface,member);message.setArguments(args);
  QDBusPendingCallWatcher pending(bus.asyncCall(message));QSignalSpy done(&pending,&QDBusPendingCallWatcher::finished);if(!pending.isFinished())done.wait(2000);
  return pending.reply();
 }
 void trayProtocol(){
  QVERIFY(QDBusConnection::sessionBus().isConnected());
  IdlePlatform platform;QSignalSpy toggles(&platform,&IdlePlatform::toggleRequested),shows(&platform,&IdlePlatform::showRequested),settings(&platform,&IdlePlatform::settingsRequested),quits(&platform,&IdlePlatform::quitRequested);
  auto reply=call("/StatusNotifierItem","org.freedesktop.DBus.Properties","GetAll",{QStringLiteral("org.kde.StatusNotifierItem")});
  QVERIFY2(reply.type()!=QDBusMessage::ErrorMessage,qPrintable(reply.errorMessage()));
  auto properties=qdbus_cast<QVariantMap>(reply.arguments()[0]);QCOMPARE(properties["IconName"].toString(),QString("agino-keyboard"));
  QCOMPARE(qvariant_cast<QDBusArgument>(properties["ToolTip"]).currentSignature(),QString("(sa(iiay)ss)"));
  QCOMPARE(qvariant_cast<QDBusArgument>(properties["IconPixmap"]).currentSignature(),QString("a(iiay)"));
  QCOMPARE(qvariant_cast<QDBusObjectPath>(properties["Menu"]).path(),QString("/MenuBar"));
  reply=call("/MenuBar","com.canonical.dbusmenu","GetLayout",{0,-1,QStringList()});
  QVERIFY2(reply.type()!=QDBusMessage::ErrorMessage,qPrintable(reply.errorMessage()));QCOMPARE(reply.signature(),QString("u(ia{sv}av)"));
  const auto argument=qvariant_cast<QDBusArgument>(reply.arguments()[1]);int id;QVariantMap map;QList<QDBusVariant> children;argument.beginStructure();argument>>id>>map>>children;argument.endStructure();QCOMPARE(id,0);QCOMPARE(children.size(),4);
  for(int n:{1,2,4}){reply=call("/MenuBar","com.canonical.dbusmenu","Event",{n,QStringLiteral("clicked"),QVariant::fromValue(QDBusVariant(0)),uint(0)});QVERIFY(reply.type()!=QDBusMessage::ErrorMessage);}
  QCOMPARE(shows.count(),1);QCOMPARE(settings.count(),1);QCOMPARE(quits.count(),1);
  call("/StatusNotifierItem","org.kde.StatusNotifierItem","Activate",{0,0});QCOMPARE(toggles.count(),1);
  reply=call("/MenuBar","com.canonical.dbusmenu","GetGroupProperties",{QVariant::fromValue(QList<int>{1,2,3,4}),QStringList()});QCOMPARE(reply.signature(),QString("a(ia{sv})"));
 }
 void watcherRestart(){
  auto bus=QDBusConnection::sessionBus();AccelMock mock;
  QVERIFY(bus.registerService("org.kde.StatusNotifierWatcher"));QVERIFY(bus.registerVirtualObject("/StatusNotifierWatcher",&mock));
  {IdlePlatform platform;QTRY_VERIFY(mock.trayRegistrations>=1);QCOMPARE(mock.trayPath,QString("/StatusNotifierItem"));bus.unregisterService("org.kde.StatusNotifierWatcher");QTest::qWait(20);QVERIFY(bus.registerService("org.kde.StatusNotifierWatcher"));QTRY_VERIFY(mock.trayRegistrations>=2);}
  bus.unregisterObject("/StatusNotifierWatcher");bus.unregisterService("org.kde.StatusNotifierWatcher");
 }
 void shortcutLifecycle(){
  auto bus=QDBusConnection::sessionBus();AccelMock mock;QVERIFY(bus.registerService("org.kde.kglobalaccel"));QVERIFY(bus.registerVirtualObject("/kglobalaccel",&mock));
  {
   IdlePlatform platform;QSignalSpy toggles(&platform,&IdlePlatform::toggleRequested);
   platform.setShortcut("Ctrl+Alt+K");QTRY_VERIFY(platform.shortcutError().isEmpty());QCOMPARE(mock.assigned,QList<int>{IdlePlatform::parseShortcut("Ctrl+Alt+K")});
   auto signal=QDBusMessage::createSignal("/component/agino_keyboard","org.kde.kglobalaccel.Component","globalShortcutPressed");signal.setArguments({QStringLiteral("agino-keyboard"),QStringLiteral("show-keyboard"),qlonglong(0)});bus.send(signal);QTRY_COMPARE(toggles.count(),1);
   auto spoof=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"idle-test-spoof");spoof.send(signal);QTest::qWait(30);QCOMPARE(toggles.count(),1);
   bus.unregisterService("org.kde.kglobalaccel");QTRY_VERIFY(!platform.shortcutError().isEmpty());
   QVERIFY(bus.registerService("org.kde.kglobalaccel"));QTRY_VERIFY(platform.shortcutError().isEmpty());QTRY_VERIFY(mock.registrations>=2);
   platform.setShortcut("Alt+Space");QTRY_COMPARE(mock.assigned,QList<int>{IdlePlatform::parseShortcut("Alt+Space")});QTRY_VERIFY(platform.shortcutError().isEmpty());
   int notifications=0;connect(&platform,&IdlePlatform::capabilitiesChanged,&platform,[&]{++notifications;platform.setShortcut("K");});
   platform.setShortcut("K");QVERIFY(!platform.shortcutError().isEmpty());QCOMPARE(notifications,1);platform.setShortcut("K");QCOMPARE(notifications,1);QTRY_VERIFY(mock.unregistrations>=3);QCOMPARE(mock.lastAction.size(),4);
  }
  bus.unregisterObject("/kglobalaccel");bus.unregisterService("org.kde.kglobalaccel");
 }
 void missingShortcutServiceIsIdempotent(){
  auto bus=QDBusConnection::sessionBus();AccelMock mock;QVERIFY(bus.registerService("org.kde.kglobalaccel"));QVERIFY(bus.registerVirtualObject("/kglobalaccel",&mock));
  IdlePlatform platform;int notifications=0;connect(&platform,&IdlePlatform::capabilitiesChanged,&platform,[&]{++notifications;platform.setShortcut("Ctrl+Alt+K");});
  platform.setShortcut("Ctrl+Alt+K");QTRY_VERIFY(platform.shortcutError().isEmpty());QTest::qWait(30);QCOMPARE(mock.registrations,1);QCOMPARE(notifications,1);
  bus.unregisterService("org.kde.kglobalaccel");QTRY_VERIFY(!platform.shortcutError().isEmpty());int count=notifications;QTest::qWait(30);QCOMPARE(notifications,count);QCOMPARE(mock.registrations,1);bus.unregisterObject("/kglobalaccel");
 }
 void trustedDesktopSignals(){
  auto bus=QDBusConnection::sessionBus();auto spoof=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"idle-test-spoof");
  QVERIFY(bus.registerService("org.agino.KeyboardDevice1"));QVERIFY(bus.registerService("org.deepin.dde.SessionManager1"));QVERIFY(bus.registerService("org.freedesktop.ScreenSaver"));
  const auto oldPlatform=qgetenv("QT_QPA_PLATFORM");const auto oldDisplay=qgetenv("WAYLAND_DISPLAY");qputenv("QT_QPA_PLATFORM","wayland");qputenv("WAYLAND_DISPLAY","agino-owned-nonexistent-display");
  {
   IdlePlatform platform;QVERIFY(platform.wayland());QSignalSpy sources(&platform,&IdlePlatform::sourceChanged),locks(&platform,&IdlePlatform::lockedChanged);
   auto source=QDBusMessage::createSignal("/org/agino/KeyboardDevice1","org.agino.KeyboardDevice1","SourceChanged");source.setArguments({uint(2),125.0,75.0});
   spoof.send(source);QTest::qWait(20);QCOMPARE(sources.count(),0);bus.send(source);QTRY_COMPARE(sources.count(),1);QCOMPARE(sources.last()[0].toInt(),2);QCOMPARE(sources.last()[1].toDouble(),125.0);
   source.setArguments({uint(99),0.0,0.0});bus.send(source);QTest::qWait(20);QCOMPARE(sources.count(),1);
   auto lock=QDBusMessage::createSignal("/org/deepin/dde/SessionManager1","org.deepin.dde.SessionManager1","LockedChanged");lock.setArguments({true});spoof.send(lock);QTest::qWait(20);QCOMPARE(locks.count(),0);bus.send(lock);QTRY_COMPARE(locks.count(),1);QCOMPARE(locks.last()[0].toBool(),true);
   auto saver=QDBusMessage::createSignal("/ScreenSaver","org.freedesktop.ScreenSaver","ActiveChanged");saver.setArguments({true});bus.send(saver);QTRY_COMPARE(locks.count(),2);
   lock.setArguments({false});bus.send(lock);QTRY_COMPARE(locks.count(),3);QCOMPARE(locks.last()[0].toBool(),true);
   saver.setArguments({false});bus.send(saver);QTRY_COMPARE(locks.count(),4);QCOMPARE(locks.last()[0].toBool(),false);
  }
  qputenv("QT_QPA_PLATFORM",oldPlatform);qputenv("WAYLAND_DISPLAY",oldDisplay);bus.unregisterService("org.agino.KeyboardDevice1");bus.unregisterService("org.deepin.dde.SessionManager1");bus.unregisterService("org.freedesktop.ScreenSaver");
 }
 void staleInitialLockSnapshot_data(){
  QTest::addColumn<bool>("replaceOwner");
  QTest::newRow("newer-lock-signal")<<false;
  QTest::newRow("replaced-session-owner")<<true;
 }
 void staleInitialLockSnapshot(){
  QFETCH(bool,replaceOwner);
  const QString service="org.deepin.dde.SessionManager1",path="/org/deepin/dde/SessionManager1";
  auto oldBus=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"idle-lock-old");
  auto bus=QDBusConnection::sessionBus();DelayedLockMock mock;
  QVERIFY(oldBus.registerService(service));QVERIFY(oldBus.registerVirtualObject(path,&mock));
  {
   IdlePlatform platform;QSignalSpy locks(&platform,&IdlePlatform::lockedChanged);
   QTRY_VERIFY(mock.pending.type()==QDBusMessage::MethodCallMessage);
   auto changed=QDBusMessage::createSignal(path,service,"LockedChanged");changed.setArguments({true});
   if(replaceOwner){QVERIFY(oldBus.unregisterService(service));QVERIFY(bus.registerService(service));}
   else {oldBus.send(changed);QTRY_COMPARE(locks.count(),1);QCOMPARE(locks.last()[0].toBool(),true);}
   oldBus.send(mock.pending.createReply({QVariant::fromValue(QDBusVariant(false))}));
   QTest::qWait(100);QCOMPARE(locks.count(),replaceOwner?0:1);
   if(replaceOwner){bus.send(changed);QTRY_COMPARE(locks.count(),1);QCOMPARE(locks.last()[0].toBool(),true);}
  }
  oldBus.unregisterObject(path);oldBus.unregisterService(service);if(replaceOwner)bus.unregisterService(service);
 }
 void shortcuts(){
  QCOMPARE(IdlePlatform::parseShortcut("Ctrl+Alt+K"),(int(Qt::CTRL)|int(Qt::ALT)|int(Qt::Key_K)));
  QCOMPARE(IdlePlatform::parseShortcut("super+shift+F35"),(int(Qt::META)|int(Qt::SHIFT)|int(Qt::Key_F35)));
  QCOMPARE(IdlePlatform::parseShortcut("Control + PgDown"),(int(Qt::CTRL)|int(Qt::Key_PageDown)));
  QCOMPARE(IdlePlatform::parseShortcut("Alt+Space"),(int(Qt::ALT)|int(Qt::Key_Space)));
  for(const auto *s:{"K","F1","Ctrl","Ctrl+","Ctrl+K+L","Ctrl+F36","Ctrl+é","Ctrl+Alt+,K","Ctrl+Ctrl+K"})QCOMPARE(IdlePlatform::parseShortcut(QString::fromUtf8(s)),0);
 }
};
QTEST_GUILESS_MAIN(IdlePlatformTest)
#include "test_idleplatform.moc"
