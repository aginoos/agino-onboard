#pragma once
#include "keyboardstate.h"
#include <QWindow>
#include <QScreen>
#include <QVariantMap>
#include <QSocketNotifier>
#include <QPointer>
class QDBusMessage;
struct DesktopCapabilities {bool layer=false,text=false,source=false;bool waylandReady()const{return layer&&text;}bool automaticReady()const{return layer&&text&&source;}};
class DesktopAdapter:public QObject {
 Q_OBJECT
public:
 explicit DesktopAdapter(QObject *p=nullptr);~DesktopAdapter()override;
 bool attach(QWindow *);void detach();void setPanelVisible(bool);void setOutput(QScreen *);
 DesktopCapabilities capabilities()const{return caps_;}QString status()const;
 QVariantMap placement()const;void restorePlacement(const QVariantMap &);
 bool wayland()const{return wayland_;}void updateGeometry();
 void moveBy(double,double);void configurePanel(QScreen *,QSize);
 bool sendCompatibilityKey(KeySpec,bool);
signals:
 void capabilitiesChanged();void inputSourceChanged(KeyboardState::InputSource);void lockedChanged(bool);void outputChanged();
public slots:void lock(bool);void screensaver(bool);void sleeping(bool);void deviceSource(uint,double,double);
public:QScreen *targetOutput()const;
protected:bool eventFilter(QObject *,QEvent *)override;
private slots:
 void sessionLock(bool,const QDBusMessage &);
private:
 quint64 lockRevision_=0;
 void readXEvents();void strut(bool);void detectWayland();
 QPointer<QScreen> targetOutput_;QPointer<QWindow> window_;QPointer<QScreen> output_;DesktopCapabilities caps_;bool wayland_=false;void *display_=nullptr;int xiOpcode_=0;QSocketNotifier *notifier_=nullptr;bool locked_=false,screensaver_=false,sleeping_=false;
 QSize negotiatedSize_,requestedSize_;QPointer<QScreen> negotiatedOutput_,requestedOutput_;
 QSize retainedSize_;QPointer<QScreen> retainedOutput_;bool restoringGeometry_=false;
 bool configuring_=false,floating_=false;QPointF position_;
};
