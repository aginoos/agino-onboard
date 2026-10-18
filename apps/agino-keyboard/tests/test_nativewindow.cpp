// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QBackingStore>
#include <QPainter>
#include <QScreen>
#include <QProcess>
#include <DWindowManagerHelper>
#include <DGuiApplicationHelper>
#include "native/nativepanel.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>

class SolidWindow final:public QWindow {
 QBackingStore backing{this};
 void exposeEvent(QExposeEvent*)override{if(!isExposed())return;backing.resize(size());const QRegion region(QRect(QPoint(),size()));backing.beginPaint(region);QPainter p(backing.paintDevice());p.fillRect(QRect(QPoint(),size()),QColor("#40c080"));p.end();backing.endPaint();backing.flush(region);}
public:SolidWindow(){setFlags(Qt::FramelessWindowHint);resize(1200,800);}
};
class NativeWindowTest final:public QObject {
 Q_OBJECT
 struct Harness {Preferences prefs;FcitxClient input;KeyboardState state;NativePanel panel{prefs,input,state};Harness(){state.manualShow();panel.setFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowDoesNotAcceptFocus);}};
 static QImage pixels(QWindow &window){QImage result=window.screen()->grabWindow(window.winId()).toImage().convertToFormat(QImage::Format_RGB32);result.setDevicePixelRatio(1);return result;}
private slots:
 void init(){QSettings().clear();}
 void usesDefaultPlatformCursor(){
  Harness h;
  QCOMPARE(h.panel.cursor().shape(),Qt::ArrowCursor);
  QVERIFY(h.panel.cursor().pixmap().isNull());
  for(auto size:{QSize(934,344),QSize(524,208)}){
   h.prefs.setCompact(size.width()==524);h.panel.resize(size);h.panel.show();
   QVERIFY(QTest::qWaitForWindowExposed(&h.panel));
   QTest::qWait(1100);
   QCOMPARE(h.panel.cursor().shape(),Qt::ArrowCursor);
   QVERIFY(h.panel.cursor().pixmap().isNull());
   h.panel.hide();
  }
 }
 void boundedOutputMatchesFullRasterAfterResize(){
  QWindow window;window.setFlags(Qt::FramelessWindowHint);window.resize(934,344);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
  NativeX11Output output;NativeStyle style;style.palette=Dtk::Gui::DGuiApplicationHelper::instance()->applicationPalette();style.font=QGuiApplication::font();
  auto scene=[&](QPainter &p,QSizeF size){p.fillRect(QRectF(QPointF(),size),QColor("#f8f8f8"));for(const auto &key:NativePainting::keys(size,style.compact,style.functionRow))NativePainting::key(p,key,style,0,false,true);};
  for(auto size:{QSize(934,344),QSize(934,400),QSize(524,208),QSize(524,242)}){
   style.compact=size.width()==524;style.functionRow=size.height()==400||size.height()==242;window.resize(size);QCoreApplication::processEvents();
   const qreal scale=window.devicePixelRatio();QImage expected(qRound(size.width()*scale),qRound(size.height()*scale),QImage::Format_ARGB32_Premultiplied);expected.setDevicePixelRatio(scale);expected.fill(Qt::transparent);{QPainter p(&expected);scene(p,size);}expected=expected.convertToFormat(QImage::Format_RGB32);expected.setDevicePixelRatio(1);
   QVERIFY(output.paint(&window,scene));QVERIFY(output.retainedBytes()<=64*1024);QTRY_COMPARE(pixels(window),expected);
   window.hide();window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QVERIFY(output.paint(&window,scene));QTRY_COMPARE(pixels(window),expected);
  }
 }
 void roundedNativePanelRevealsBackground(){
  SolidWindow background;background.show();QVERIFY(QTest::qWaitForWindowExposed(&background));background.setPosition(0,0);
  Harness h;for(auto size:{QSize(934,344),QSize(934,400),QSize(524,208),QSize(524,242)}){
   h.prefs.setCompact(size.width()==524);h.prefs.setFunctionRow(size.height()==400||size.height()==242);h.panel.resize(size);h.panel.setPosition(40,40);h.panel.show();h.panel.raise();QVERIFY(QTest::qWaitForWindowExposed(&h.panel));
   auto capture=[&]{QPoint p=h.panel.mapToGlobal(QPoint());return h.panel.screen()->grabWindow(0,p.x(),p.y(),h.panel.width(),h.panel.height()).toImage();};
   QTRY_COMPARE(capture().pixelColor(0,0),QColor("#40c080"));QTRY_COMPARE(capture().pixelColor(capture().width()-1,0),QColor("#40c080"));QTRY_COMPARE(capture().pixelColor(0,capture().height()-1),QColor("#40c080"));QTRY_COMPARE(capture().pixelColor(capture().width()-1,capture().height()-1),QColor("#40c080"));QVERIFY(!h.panel.mask().contains(QPoint()));h.panel.hide();
  }
 }
 void compositorSelectionRestoresOpacity(){
  auto *display=XOpenDisplay(nullptr);QVERIFY(display);auto close=qScopeGuard([&]{XCloseDisplay(display);});const Atom selection=XInternAtom(display,"_NET_WM_CM_S0",False);QVERIFY(!XGetSelectionOwner(display,selection));
  SolidWindow background;background.show();QVERIFY(QTest::qWaitForWindowExposed(&background));background.setPosition(0,0);
  Harness h;h.prefs.setOpacityPercent(60);h.panel.resize(934,344);h.panel.setPosition(40,40);h.panel.show();h.panel.raise();QVERIFY(QTest::qWaitForWindowExposed(&h.panel));QTRY_VERIFY(!h.panel.transparencySupported());
  const QColor fill=Dtk::Gui::DGuiApplicationHelper::instance()->applicationPalette().window().color();auto center=[&]{QPoint p=h.panel.mapToGlobal(QPoint());const QImage image=h.panel.screen()->grabWindow(0,p.x(),p.y(),h.panel.width(),h.panel.height()).toImage();return image.pixelColor(qRound(400*h.panel.devicePixelRatio()),qRound(20*h.panel.devicePixelRatio()));};QTRY_COMPARE(center(),fill);
  QProcess compositor;auto stop=qScopeGuard([&]{if(compositor.state()!=QProcess::NotRunning){compositor.terminate();compositor.waitForFinished();}});compositor.start("xcompmgr",{"-c"});QVERIFY(compositor.waitForStarted());QTRY_VERIFY(XGetSelectionOwner(display,selection));QTRY_VERIFY(h.panel.transparencySupported());QTRY_VERIFY(h.panel.mask().isEmpty());
  const QColor bg("#40c080");auto blended=[&]{const auto color=center();return qAbs(color.red()-qRound(fill.red()*.6+bg.red()*.4))<=3&&qAbs(color.green()-qRound(fill.green()*.6+bg.green()*.4))<=3&&qAbs(color.blue()-qRound(fill.blue()*.6+bg.blue()*.4))<=3;};QTRY_VERIFY(blended());compositor.terminate();QVERIFY(compositor.waitForFinished());QTRY_VERIFY(!h.panel.transparencySupported());QTRY_VERIFY(!h.panel.mask().isEmpty());QTRY_COMPARE(center(),fill);
 }
 void deepinShadowPreservesGeometry(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_NATIVE_SHADOW"))QSKIP("Private Deepin KWin required");auto wm=Dtk::Gui::DWindowManagerHelper::instance();QTRY_VERIFY(wm->hasNoTitlebar());QTRY_VERIFY(wm->hasComposite());
  SolidWindow background;background.show();QVERIFY(QTest::qWaitForWindowExposed(&background));background.setPosition(0,0);
  Harness h;h.panel.resize(934,344);h.panel.setPosition(100,100);auto display=XOpenDisplay(nullptr);QVERIFY(display);auto close=qScopeGuard([&]{XCloseDisplay(display);});Atom dock=XInternAtom(display,"_NET_WM_WINDOW_TYPE_DOCK",False);XChangeProperty(display,h.panel.winId(),XInternAtom(display,"_NET_WM_WINDOW_TYPE",False),XA_ATOM,32,PropModeReplace,reinterpret_cast<unsigned char*>(&dock),1);XFlush(display);h.panel.show();h.panel.raise();QVERIFY(QTest::qWaitForWindowExposed(&h.panel));QCOMPARE(h.panel.size(),QSize(934,344));QTRY_COMPARE(h.panel.property("_d_windowRadius").toInt(),12);
  auto outside=[&]{QPoint p=h.panel.mapToGlobal(QPoint(h.panel.width()/2,h.panel.height()+8));return h.panel.screen()->grabWindow(0,p.x(),p.y(),1,1).toImage().pixelColor(0,0);};QTRY_VERIFY(outside().lightness()<QColor("#40c080").lightness()-5);
  h.prefs.setCompact(true);h.panel.resize(524,208);QTRY_COMPARE(h.panel.property("_d_windowRadius").toInt(),8);h.panel.hide();h.panel.show();QVERIFY(QTest::qWaitForWindowExposed(&h.panel));QTRY_VERIFY(outside().lightness()<QColor("#40c080").lightness()-5);
 }
};
QTEST_MAIN(NativeWindowTest)
#include "test_nativewindow.moc"
