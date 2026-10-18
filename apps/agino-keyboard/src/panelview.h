// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QQuickView>
#include <QQmlEngine>
#include "keyboardiconprovider.h"
#include <QQuickItem>
#include <QPainterPath>
#include <DWindowManagerHelper>
#include <DPlatformHandle>
#include <QTimer>
#include <QElapsedTimer>
#include <atomic>
#include <memory>
#include "x11compositor.h"

class PanelView final : public QQuickView {
public:
 PanelView() {
  engine()->addImageProvider("keyboard-icon",new KeyboardIconProvider);
  // The QML panel draws its rounded border; do not fill the native corners.
  setColor(Qt::transparent);
  if(QGuiApplication::platformName()=="xcb"){
   // Alpha pixels are black without an X11 compositor. Use a native shape
   // only in that case, retaining antialiased alpha edges when composited.
   auto refresh=[this]{updateNativeCorners();};
   compositorWatcher_=std::make_unique<X11CompositorWatcher>();
   // Disconnect these before QWindow's destructor hides the native surface:
   // its visibility notification must not access destroyed appearance state.
   connect(compositorWatcher_.get(),&X11CompositorWatcher::changed,compositorWatcher_.get(),refresh);
   connect(Dtk::Gui::DWindowManagerHelper::instance(),&Dtk::Gui::DWindowManagerHelper::hasCompositeChanged,compositorWatcher_.get(),refresh);
   connect(Dtk::Gui::DWindowManagerHelper::instance(),&Dtk::Gui::DWindowManagerHelper::hasNoTitlebarChanged,compositorWatcher_.get(),refresh);
   connect(this,&QWindow::widthChanged,compositorWatcher_.get(),refresh);
   connect(this,&QWindow::heightChanged,compositorWatcher_.get(),refresh);
   connect(this,&QWindow::screenChanged,compositorWatcher_.get(),refresh);
   connect(this,&QWindow::visibleChanged,compositorWatcher_.get(),refresh);
   connect(this,&QQuickView::statusChanged,compositorWatcher_.get(),refresh);
  }
  // Pair each completion with the request present at that frame's beginning;
  // a late completion of an older frame must not time a new show request.
  connect(this,&QQuickWindow::beforeFrameBegin,this,[frames=frames_]{
   frames->inFrame=std::atomic_load(&frames->requested);
  },Qt::DirectConnection);
  connect(this,&QQuickWindow::afterFrameEnd,this,[frames=frames_]{
   auto sample=frames->inFrame;if(!sample||sample->canceled.load())return;
   qint64 pending=-1;sample->submittedNs.compare_exchange_strong(pending,sample->started.nsecsElapsed());
  },Qt::DirectConnection);
  // Keep short hide/show bursts warm; release after an idle second. Retain
  // QML and keyboard state when recreating the scene graph on the next show.
  releaseTimer_.setSingleShot(true);
  releaseTimer_.setInterval(1000);
  connect(&releaseTimer_,&QTimer::timeout,this,[this]{
   if(isVisible())return;
   setPersistentGraphics(false);
   setPersistentSceneGraph(false);
   releaseResources();
  });
  // The timer dies before QQuickView's destructor hides its native window.
  // Tie this callback to the timer so teardown cannot restart a dead timer.
  connect(this,&QWindow::visibleChanged,&releaseTimer_,[this](bool visible){
   if(visible){releaseTimer_.stop();setPersistentGraphics(true);setPersistentSceneGraph(true);}
   else{auto sample=std::atomic_load(&frames_->requested);if(sample)sample->canceled=true;releaseTimer_.start();}
  });
 }
 void measureNextFrame(const QElapsedTimer &start,quint64 serial){
  auto sample=std::make_shared<FrameSample>();sample->started=start;sample->serial=serial;
  std::atomic_store(&frames_->requested,sample);
 }
 double firstFrameMilliseconds()const{
  auto sample=std::atomic_load(&frames_->requested);const qint64 ns=sample?sample->submittedNs.load():-1;
  return ns<0?-1:double(ns)/1000000;
 }
 quint64 firstFrameSerial()const{
  auto sample=std::atomic_load(&frames_->requested);return sample&&sample->submittedNs.load()>=0?sample->serial:0;
 }
protected:
 bool event(QEvent *event)override{
  const bool handled=QQuickView::event(event);
  if(event->type()==QEvent::DevicePixelRatioChange&&QGuiApplication::platformName()=="xcb")updateNativeCorners(true);
  return handled;
 }
private:
 void updateNativeCorners(bool force=false){
  QRegion clip;
  bool composited=Dtk::Gui::DWindowManagerHelper::instance()->hasComposite();
  // Some X11 compositors do not publish Deepin/KWin notifications. Their
  // standard compositor selection still proves that alpha is supported.
  composited=composited||x11CompositeAvailable();
  if(rootObject()&&rootObject()->metaObject()->indexOfProperty("transparencySupported")>=0)rootObject()->setProperty("transparencySupported",composited);
  const qreal radius=rootObject()?rootObject()->property("radius").toReal():0;
  // DDE KWin suppresses its shadow for alpha/frameless windows unless a native
  // window radius is declared. Use DTK's compositor-managed no-titlebar path;
  // never enable its fallback frame, which would alter dock/input geometry.
  if(Dtk::Gui::DWindowManagerHelper::instance()->hasNoTitlebar()&&Dtk::Gui::DPlatformHandle::isDXcbPlatform()){
   if(!decoration_){
    decoration_=std::make_unique<Dtk::Gui::DPlatformHandle>(this);
    decoration_->setEnableSystemResize(false);
    decoration_->setEnableSystemMove(false);
    decoration_->setBorderWidth(0); // QML already draws the panel border.
   }
   decoration_->setWindowRadius(qRound(radius));
  }
  if(radius>0&&!composited){
   QPainterPath outline;outline.addRoundedRect(QRectF(0,0,width(),height()),radius,radius);
   clip=QRegion(outline.toFillPolygon().toPolygon());
  }
  if(force||mask()!=clip)setMask(clip);
 }
 struct FrameSample{QElapsedTimer started;quint64 serial=0;std::atomic<qint64> submittedNs{-1};std::atomic_bool canceled{false};};
 struct Frames{std::shared_ptr<FrameSample> requested,inFrame;};
 std::shared_ptr<Frames> frames_=std::make_shared<Frames>();
 std::unique_ptr<X11CompositorWatcher> compositorWatcher_;
 std::unique_ptr<Dtk::Gui::DPlatformHandle> decoration_;
 QTimer releaseTimer_;
};
