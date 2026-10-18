#include <QtTest>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QFont>
#include <QFontMetrics>
#include <QStaticText>
#include <QPainter>
#include "panelview.h"
#include <QScreen>
#include <QProcess>
#include <DWindowManagerHelper>
#include <private/qhighdpiscaling_p.h>
#include <private/qwindow_p.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>

class PanelViewTest : public QObject {
 Q_OBJECT
private slots:
 void deepinNativeShadowPreservesPanelGeometry() {
  if(!qEnvironmentVariableIsSet("AGINO_TEST_NATIVE_SHADOW"))QSKIP("Requires private Deepin KWin");
  auto wm=Dtk::Gui::DWindowManagerHelper::instance();QTRY_VERIFY(wm->hasNoTitlebar());QTRY_VERIFY(wm->hasComposite());
  QQuickView background;background.setFlags(Qt::FramelessWindowHint);background.setColor(QColor("#80c0e0"));background.resize(1100,700);background.show();QVERIFY(QTest::qWaitForWindowExposed(&background));background.setPosition(0,0);
  PanelView panel;panel.setFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowDoesNotAcceptFocus);panel.setResizeMode(QQuickView::SizeRootObjectToView);
  panel.setSource(QUrl("data:text/plain,import QtQuick; Rectangle { color: 'white'; radius: 12 }"));panel.resize(524,208);panel.setPosition(180,180);
  auto display=XOpenDisplay(nullptr);QVERIFY(display);auto close=qScopeGuard([&]{XCloseDisplay(display);});auto dock=XInternAtom(display,"_NET_WM_WINDOW_TYPE_DOCK",False);XChangeProperty(display,panel.winId(),XInternAtom(display,"_NET_WM_WINDOW_TYPE",False),XA_ATOM,32,PropModeReplace,reinterpret_cast<unsigned char*>(&dock),1);XFlush(display);
  panel.show();panel.raise();QVERIFY(QTest::qWaitForWindowExposed(&panel));
  QCOMPARE(panel.size(),QSize(524,208));
  QTRY_COMPARE(panel.property("_d_windowRadius").toInt(),12);
  auto outside=[&](){auto p=panel.mapToGlobal(QPoint(panel.width()/2,panel.height()+8));return panel.screen()->grabWindow(0,p.x(),p.y(),1,1).toImage().pixelColor(0,0);};
  QTRY_VERIFY(outside().lightness()<QColor("#80c0e0").lightness()-5);
  panel.rootObject()->setProperty("radius",8);panel.resize(525,208);QTRY_COMPARE(panel.property("_d_windowRadius").toInt(),8);
  panel.hide();panel.show();QVERIFY(QTest::qWaitForWindowExposed(&panel));QTRY_COMPARE(panel.property("_d_windowRadius").toInt(),8);QTRY_VERIFY(outside().lightness()<QColor("#80c0e0").lightness()-5);
 }
 void fontCacheCleanupPreservesRenderedText() {
  QImage reference;
  QFont retainedFont;retainedFont.setPixelSize(24);
  QFontMetrics retainedMetrics(retainedFont);const QString label=QString::fromUtf8("屏幕键盘 中/英 Esc");
  const int initialWidth=retainedMetrics.horizontalAdvance(label);
  QStaticText retainedText(label);retainedText.prepare(QTransform(),retainedFont);
  auto renderRetained=[&](){QImage image(600,100,QImage::Format_ARGB32);image.fill(Qt::white);QPainter painter(&image);painter.setFont(retainedFont);painter.setPen(Qt::black);painter.drawStaticText(20,20,retainedText);return image;};
  const auto retainedPixels=renderRetained();
  for(int cycle=0;cycle<3;++cycle){
   auto panel=std::make_unique<PanelView>();
   panel->setSource(QUrl::fromEncoded("data:text/plain,"+QUrl::toPercentEncoding(QString::fromUtf8("import QtQuick; Rectangle { width: 600; height: 150; color: 'white'; Text { anchors.centerIn: parent; text: '屏幕键盘 输入法 Esc 中/英 123'; font.pixelSize: 24; color: 'black' } }"))));
   QVERIFY(panel->rootObject());panel->resize(600,150);panel->show();
   QVERIFY(QTest::qWaitForWindowExposed(panel.get()));
   QTRY_VERIFY(panel->isSceneGraphInitialized());
   QImage current;QTRY_VERIFY(!(current=panel->grabWindow()).isNull());
   int darkPixels=0;for(int y=0;y<current.height();++y)for(int x=0;x<current.width();++x)if(current.pixelColor(x,y).lightness()<128)++darkPixels;
   QVERIFY(darkPixels>200);
   if(cycle==0)reference=current;else QCOMPARE(current,reference);
   panel->hide();panel.reset();QFont::cleanup();
   QCOMPARE(retainedMetrics.horizontalAdvance(label),initialWidth);QCOMPARE(renderRetained(),retainedPixels);
  }
 }
 void roundedPanelHasTransparentCorners() {
  PanelView panel;panel.setResizeMode(QQuickView::SizeRootObjectToView);
  panel.setSource(QUrl("data:text/plain,import QtQuick; Rectangle { color: 'white'; radius: 12 }"));
  panel.resize(934,344);panel.show();QVERIFY(QTest::qWaitForWindowExposed(&panel));
  QImage image;QTRY_VERIFY(!(image=panel.grabWindow()).isNull());
  QCOMPARE(image.pixelColor(0,0).alpha(),0);QCOMPARE(image.pixelColor(image.width()-1,0).alpha(),0);
  QCOMPARE(image.pixelColor(0,image.height()-1).alpha(),0);QCOMPARE(image.pixelColor(image.width()-1,image.height()-1).alpha(),0);
  QCOMPARE(image.pixelColor(image.width()/2,image.height()/2),QColor("white"));
 }
 void roundedPanelRevealsDesktopWithoutCompositor() {
  if(QGuiApplication::platformName()!="xcb")QSKIP("X11 desktop capture");
  QQuickView background;background.setFlags(Qt::FramelessWindowHint);background.setColor(QColor("#40c080"));background.resize(1100,700);background.show();
  QVERIFY(QTest::qWaitForWindowExposed(&background));background.setPosition(0,0);
  PanelView panel;panel.setFlags(Qt::FramelessWindowHint|Qt::WindowDoesNotAcceptFocus);panel.setResizeMode(QQuickView::SizeRootObjectToView);
  panel.setSource(QUrl("data:text/plain,import QtQuick; Rectangle { color: 'white'; radius: 12 }"));
  auto visibleCorner=[&](){const QPoint origin=panel.mapToGlobal(QPoint(0,0));return panel.screen()->grabWindow(0,origin.x(),origin.y(),panel.width(),panel.height()).toImage();};
  for(const QSize size:{QSize(828,344),QSize(934,400),QSize(524,208),QSize(524,242)}){
   panel.rootObject()->setProperty("radius",size.width()==524?8:12);panel.resize(size);panel.setPosition(40,40);panel.show();panel.raise();QVERIFY(QTest::qWaitForWindowExposed(&panel));
   QImage image;QTRY_VERIFY(!(image=visibleCorner()).isNull());
   QTRY_COMPARE(visibleCorner().pixelColor(0,0),QColor("#40c080"));image=visibleCorner();
   QCOMPARE(image.pixelColor(image.width()-1,0),QColor("#40c080"));QCOMPARE(image.pixelColor(0,image.height()-1),QColor("#40c080"));QCOMPARE(image.pixelColor(image.width()-1,image.height()-1),QColor("#40c080"));
   QCOMPARE(image.pixelColor(image.width()/2,image.height()/2),QColor("white"));
   panel.hide();QTest::qWait(1100);
  }
 }
 void compositorChangeSwitchesNativeClip() {
  if(!qEnvironmentVariable("QT_QPA_PLATFORM").startsWith("dxcb"))QSKIP("DTK compositor notifications");
  auto display=XOpenDisplay(nullptr);QVERIFY(display);const auto toggling=XInternAtom(display,"_NET_KDE_COMPOSITE_TOGGLING",False);
  // Exercise the same root property used by Deepin KWin to notify DXcb.
  auto notify=[&](unsigned char active){XChangeProperty(display,DefaultRootWindow(display),toggling,toggling,8,PropModeReplace,&active,1);XSync(display,False);};notify(0);
  PanelView panel;panel.setResizeMode(QQuickView::SizeRootObjectToView);panel.setSource(QUrl("data:text/plain,import QtQuick; Rectangle { property bool transparencySupported: true; color: 'white'; radius: 12; opacity: transparencySupported ? 0.6 : 1 }"));panel.resize(934,344);panel.show();QVERIFY(QTest::qWaitForWindowExposed(&panel));
  QCOMPARE(panel.rootObject()->property("transparencySupported").toBool(),false);QCOMPARE(panel.rootObject()->opacity(),1.0);
  QVERIFY(!panel.mask().contains(QPoint(0,0)));QVERIFY(panel.mask().contains(QPoint(467,172)));
  notify(1);
  QTRY_VERIFY(Dtk::Gui::DWindowManagerHelper::instance()->hasComposite());QTRY_VERIFY(panel.mask().isEmpty());QTRY_VERIFY(panel.rootObject()->property("transparencySupported").toBool());QTRY_COMPARE(panel.rootObject()->opacity(),0.6);
  notify(0);
  QTRY_VERIFY(!Dtk::Gui::DWindowManagerHelper::instance()->hasComposite());QTRY_VERIFY(!panel.mask().isEmpty());QVERIFY(!panel.mask().contains(QPoint(0,0)));QTRY_COMPARE(panel.rootObject()->opacity(),1.0);
  XDeleteProperty(display,DefaultRootWindow(display),toggling);XCloseDisplay(display);
 }
 void compositorSelectionEnablesRealTransparency(){
  if(QGuiApplication::platformName()!="xcb")QSKIP("Private X11 compositor");
  auto display=XOpenDisplay(nullptr);QVERIFY(display);auto close=qScopeGuard([&]{XCloseDisplay(display);});const auto selection=XInternAtom(display,"_NET_WM_CM_S0",False);QVERIFY(!XGetSelectionOwner(display,selection));
  QQuickView background;background.setFlags(Qt::FramelessWindowHint);background.setColor(QColor("#40c080"));background.resize(1100,700);background.show();QVERIFY(QTest::qWaitForWindowExposed(&background));background.setPosition(0,0);
  PanelView panel;panel.setFlags(Qt::FramelessWindowHint|Qt::WindowDoesNotAcceptFocus);panel.setResizeMode(QQuickView::SizeRootObjectToView);
  panel.setSource(QUrl("data:text/plain,import QtQuick; Rectangle { property bool transparencySupported: false; color: 'white'; radius: 8; opacity: transparencySupported ? 0.6 : 1 }"));panel.resize(524,208);panel.setPosition(40,40);panel.show();panel.raise();QVERIFY(QTest::qWaitForWindowExposed(&panel));
  QTRY_VERIFY(!panel.rootObject()->property("transparencySupported").toBool());QVERIFY(!panel.mask().isEmpty());
  QProcess compositor;auto stop=qScopeGuard([&]{if(compositor.state()!=QProcess::NotRunning){compositor.terminate();compositor.waitForFinished();}});compositor.start("xcompmgr",{"-c"});QVERIFY(compositor.waitForStarted());QTRY_VERIFY(XGetSelectionOwner(display,selection));
  QTRY_VERIFY(panel.rootObject()->property("transparencySupported").toBool());QTRY_VERIFY(panel.mask().isEmpty());
  auto center=[&](){auto origin=panel.mapToGlobal(QPoint(0,0));return panel.screen()->grabWindow(0,origin.x(),origin.y(),panel.width(),panel.height()).toImage().pixelColor(262,104);};
  QTRY_VERIFY(qAbs(center().red()-179)<=3&&qAbs(center().green()-230)<=3&&qAbs(center().blue()-204)<=3);
  compositor.terminate();QVERIFY(compositor.waitForFinished());QTRY_VERIFY(!panel.rootObject()->property("transparencySupported").toBool());QTRY_VERIFY(!panel.mask().isEmpty());QTRY_COMPARE(center(),QColor("white"));
  compositor.start("xcompmgr",{"-c"});QVERIFY(compositor.waitForStarted());QTRY_VERIFY(panel.rootObject()->property("transparencySupported").toBool());QTRY_VERIFY(panel.mask().isEmpty());QTRY_VERIFY(qAbs(center().red()-179)<=3);

 }
 void nativeClipTracksLiveScaleChange() {
  if(QGuiApplication::platformName()!="xcb")QSKIP("X11 native shape");
  PanelView panel;panel.setResizeMode(QQuickView::SizeRootObjectToView);panel.setSource(QUrl("data:text/plain,import QtQuick; Rectangle { color: 'white'; radius: 12 }"));panel.resize(200,100);panel.show();QVERIFY(QTest::qWaitForWindowExposed(&panel));
  auto display=XOpenDisplay(nullptr);QVERIFY(display);
  auto shape=[&](){int count=0,order=0;auto rectangles=XShapeGetRectangles(display,panel.winId(),ShapeBounding,&count,&order);QRegion region;for(int n=0;n<count;++n)region+=QRect(rectangles[n].x,rectangles[n].y,rectangles[n].width,rectangles[n].height);XFree(rectangles);return region;};
  const qreal global=qEnvironmentVariableIsSet("QT_SCALE_FACTOR")?qEnvironmentVariable("QT_SCALE_FACTOR").toDouble():1;
  const QPoint probe(qRound(2*global),qRound(12*global));QVERIFY(shape().contains(probe));
  // Isolate the DPR event from geometry signals: a native clip must refresh
  // even if no width/height notification is delivered with the new scale.
  const QSignalBlocker blocker(&panel);auto *screen=panel.screen();
  for(qreal factor:{2.0,1.5,1.0}){
   QHighDpiScaling::setScreenFactor(screen,factor);QWindowPrivate::get(&panel)->updateDevicePixelRatio();QGuiApplication::sync();
   QCOMPARE(shape().contains(probe),factor!=2.0);
   QVERIFY(!shape().contains(QPoint(0,0)));QVERIFY(shape().contains(QPoint(100,50)));
  }
  XCloseDisplay(display);
 }
 void retainedContentSurvivesReopen() {
  PanelView panel;
  panel.setResizeMode(QQuickView::SizeRootObjectToView);
  panel.setSource(QUrl("data:text/plain,import QtQuick; Rectangle { color: 'white' }"));
  QVERIFY(panel.rootObject());auto *root=panel.rootObject();
  panel.resize(934,380);panel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&panel));
  QTRY_VERIFY(panel.isSceneGraphInitialized());
  if(qEnvironmentVariable("QT_QUICK_BACKEND")=="software")QCOMPARE(panel.rendererInterface()->graphicsApi(),QSGRendererInterface::Software);
  auto centerColor=[&](){const auto image=panel.grabWindow();return image.isNull()?QColor():image.pixelColor(image.width()/2,image.height()/2);};
  QTRY_COMPARE(centerColor(),QColor("white"));
  panel.hide();QTest::qWait(1200);
  QCOMPARE(panel.rootObject(),root);root->setProperty("color",QColor("blue"));
  panel.show();QVERIFY(QTest::qWaitForWindowExposed(&panel));
  QTRY_COMPARE(centerColor(),QColor("blue"));
 }
 void quickReopenKeepsRenderingResources() {
  PanelView panel;
  panel.setSource(QUrl("data:text/plain,import QtQuick; Rectangle { color: 'white' }"));
  panel.resize(934,380);panel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&panel));
  QTRY_VERIFY(panel.isSceneGraphInitialized());
  QSignalSpy invalidated(&panel,&QQuickWindow::sceneGraphInvalidated);
  panel.hide();QTest::qWait(100);panel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&panel));QTest::qWait(1200);
  QCOMPARE(invalidated.count(),0);
 }
 void hideReleasesRenderingButRetainsQml() {
  PanelView panel;
  panel.setResizeMode(QQuickView::SizeRootObjectToView);
  panel.setSource(QUrl("data:text/plain,import QtQuick; Rectangle { color: 'white'; property int retainedValue: 42 }"));
  QVERIFY(panel.rootObject());
  auto *root=panel.rootObject();
  QSignalSpy initialized(&panel,&QQuickWindow::sceneGraphInitialized);
  QSignalSpy invalidated(&panel,&QQuickWindow::sceneGraphInvalidated);
  panel.resize(934,380);panel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&panel));
  QTRY_VERIFY_WITH_TIMEOUT(initialized.count()>0,5000);
  panel.hide();
  QTRY_VERIFY_WITH_TIMEOUT(invalidated.count()>0,2000);
  QCOMPARE(panel.rootObject(),root);
  QCOMPARE(root->property("retainedValue").toInt(),42);
  const int before=initialized.count();panel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&panel));
  QTRY_VERIFY_WITH_TIMEOUT(initialized.count()>before,5000);
  QCOMPARE(panel.rootObject(),root);
 }
};
QTEST_MAIN(PanelViewTest)
#include "test_panelview.moc"
