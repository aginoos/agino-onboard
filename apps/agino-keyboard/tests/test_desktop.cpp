#include <QtTest>
#include <QWindow>
#include <QDBusConnection>
#include <QDBusVirtualObject>
#include <QDBusMessage>
#include <QDBusVariant>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <memory>
#include "desktopadapter.h"
class DesktopDelayedLockMock:public QDBusVirtualObject {
public:
 QDBusMessage pending;
 QString introspect(const QString &)const override{return {};}
 bool handleMessage(const QDBusMessage &message,const QDBusConnection &)override{
  if(message.interface()!="org.freedesktop.DBus.Properties"||message.member()!="Get")return false;
  pending=message;return true;
 }
};
class DesktopTest:public QObject{Q_OBJECT
private slots:
 void replaceAndDestroyPanel(){
  DesktopAdapter adapter;QWindow old;old.resize(900,350);
  QVERIFY(adapter.attach(&old));adapter.setPanelVisible(true);QVERIFY(old.isVisible());
  auto next=std::make_unique<QWindow>();next->resize(900,350);
  QVERIFY(adapter.attach(next.get()));QVERIFY(!old.isVisible());
  adapter.setPanelVisible(true);QVERIFY(next->isVisible());
  next.reset();adapter.updateGeometry();adapter.setOutput(qGuiApp->primaryScreen());adapter.setPanelVisible(true);
  QWindow replacement;replacement.resize(900,350);QVERIFY(adapter.attach(&replacement));
  adapter.setPanelVisible(true);QVERIFY(replacement.isVisible());
  QVERIFY(replacement.flags().testFlag(Qt::WindowDoesNotAcceptFocus));
 }
 void floatingClampsAndRetainsSubpixelMotion(){
  DesktopAdapter adapter;QWindow panel;panel.resize(900,350);QVERIFY(adapter.attach(&panel));adapter.setPanelVisible(true);
  const QRect output=panel.screen()->geometry();
  adapter.moveBy(-100000,-100000);QCOMPARE(panel.position(),output.topLeft());
  for(int n=0;n<4;++n)adapter.moveBy(.25,.25);
  QCOMPARE(panel.position(),output.topLeft()+QPoint(1,1));
  adapter.moveBy(100000,100000);QCOMPARE(panel.position(),output.bottomRight()-QPoint(panel.width()-1,panel.height()-1));
  panel.resize(950,400);QTRY_COMPARE(panel.position(),output.bottomRight()-QPoint(panel.width()-1,panel.height()-1));
  const QPoint saved=panel.position();adapter.setPanelVisible(false);adapter.moveBy(-100,-100);adapter.setPanelVisible(true);QCOMPARE(panel.position(),saved);
 }
 void candidateResizeKeepsFloatingKeyPosition(){
  DesktopAdapter adapter;QWindow panel;panel.resize(900,300);QVERIFY(adapter.attach(&panel));adapter.setPanelVisible(true);adapter.moveBy(-20,-60);
  const QPoint bottom=panel.position()+QPoint(0,panel.height());
  adapter.configurePanel(panel.screen(),QSize(900,348));QCOMPARE(panel.position()+QPoint(0,panel.height()),bottom);
  adapter.configurePanel(panel.screen(),QSize(900,300));QCOMPARE(panel.position()+QPoint(0,panel.height()),bottom);
 }
 void x11PanelHasAFixedSize(){
  // The window manager must not maximize or tile the keyboard dragged to a screen edge: minimum = maximum = its size.
  DesktopAdapter adapter;QWindow panel;panel.resize(900,300);QVERIFY(adapter.attach(&panel));
  if(adapter.wayland())QSKIP("X11 only");
  for(const QSize size:{QSize(900,348),QSize(934,300),QSize(600,400),QSize(900,300)}){
   adapter.configurePanel(panel.screen(),size);QCOMPARE(panel.size(),size);QCOMPARE(panel.minimumSize(),size);QCOMPARE(panel.maximumSize(),size);
  }
 }
 void candidateResizeAfterUnloadKeepsFloatingBottom(){
  DesktopAdapter adapter;auto old=std::make_unique<QWindow>();old->resize(900,348);QVERIFY(adapter.attach(old.get()));adapter.setPanelVisible(true);adapter.moveBy(-20,-60);
  const QPoint bottom=old->position()+QPoint(0,old->height());adapter.detach();old.reset();
  QWindow panel;QVERIFY(adapter.attach(&panel));adapter.configurePanel(qGuiApp->primaryScreen(),QSize(900,300));adapter.setPanelVisible(true);
  QCOMPARE(panel.position()+QPoint(0,panel.height()),bottom);
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
  auto bus=QDBusConnection::sessionBus();DesktopDelayedLockMock mock;
  QVERIFY(oldBus.registerService(service));QVERIFY(oldBus.registerVirtualObject(path,&mock));
  {
   DesktopAdapter platform;QSignalSpy locks(&platform,&DesktopAdapter::lockedChanged);
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
 void locksAreIndependent(){DesktopAdapter a;QSignalSpy spy(&a,&DesktopAdapter::lockedChanged);a.lock(true);a.screensaver(false);QVERIFY(spy.last()[0].toBool());a.sleeping(true);a.lock(false);QVERIFY(spy.last()[0].toBool());a.sleeping(false);QVERIFY(!spy.last()[0].toBool());}
 void missingRoles(){DesktopCapabilities c;QVERIFY(!c.waylandReady());c.layer=true;QVERIFY(!c.waylandReady());c.text=true;QVERIFY(c.waylandReady());QVERIFY(!c.automaticReady());c.source=true;QVERIFY(c.automaticReady());}
 void nonfocusAndStruts(){QWindow target;target.resize(500,200);target.show();target.requestActivate();QTest::qWait(50);QWindow panel;panel.resize(900,350);DesktopAdapter adapter;QVERIFY(adapter.attach(&panel));adapter.setPanelVisible(true);QTest::qWait(50);QVERIFY(panel.flags().testFlag(Qt::WindowDoesNotAcceptFocus));auto d=XOpenDisplay(nullptr);QVERIFY(d);Atom actual;int format;unsigned long count,remaining;unsigned char *data=nullptr;
  XGetWindowProperty(d,panel.winId(),XInternAtom(d,"_NET_WM_STRUT_PARTIAL",False),0,12,False,XA_CARDINAL,&actual,&format,&count,&remaining,&data);QCOMPARE(count,12ul);QVERIFY(reinterpret_cast<unsigned long*>(data)[3]>0);XFree(data);
  adapter.moveBy(0,-100);
  auto strut=[&](){unsigned char *value=nullptr;XGetWindowProperty(d,panel.winId(),XInternAtom(d,"_NET_WM_STRUT_PARTIAL",False),0,12,False,XA_CARDINAL,&actual,&format,&count,&remaining,&value);unsigned long bottom=count==12?reinterpret_cast<unsigned long*>(value)[3]:ULONG_MAX;if(value)XFree(value);return bottom;};
  QTRY_COMPARE(strut(),0ul);adapter.setPanelVisible(false);QTRY_COMPARE(strut(),0ul);XCloseDisplay(d);
 }
};
QTEST_MAIN(DesktopTest)
#include "test_desktop.moc"
