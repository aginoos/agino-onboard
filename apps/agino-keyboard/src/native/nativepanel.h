// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nativecontrol.h"
#include "nativepainting.h"
#include "nativesettings.h"
#include "nativeaccessible.h"
#include "nativeoutput.h"
#include "keyboardstate.h"
#include "fcitxclient.h"
#include "preferences.h"
#include "nativecandidaterows.h"
#include "x11compositor.h"
#include <QWindow>
#include <QBackingStore>
#include <QElapsedTimer>
#include <QTimer>
#include <QImage>
#include <DPlatformHandle>
#include <memory>
class NativePanel final:public QWindow,public NativeAccessibleHost {
 Q_OBJECT
 Q_PROPERTY(bool expanded READ expanded WRITE setExpanded NOTIFY expandedChanged)
 Q_PROPERTY(bool functionRow READ functionRow WRITE setFunctionRow NOTIFY minimumHeightChanged)
 Q_PROPERTY(int minimumHeight READ minimumHeight NOTIFY minimumHeightChanged)
 Q_PROPERTY(qreal radius READ radius)
 Q_PROPERTY(QStringList screenNames READ screenNames WRITE setScreenNames)
 Q_PROPERTY(bool transparencySupported READ transparencySupported WRITE setTransparencySupported)
public:
 NativePanel(Preferences&,FcitxClient&,KeyboardState&);
 ~NativePanel()override;
 QObject *rootObject(){return this;}
 bool expanded()const{return expanded_;}void setExpanded(bool);
 bool functionRow()const{return functionRow_;}void setFunctionRow(bool);
 int minimumHeight()const{return prefs_.compact()?(functionRow_?242:208):(functionRow_?400:344);}
 qreal radius()const{return prefs_.compact()?8:12;}
 QStringList screenNames()const{return screens_;}void setScreenNames(const QStringList &s){if(screens_==s)return;screens_=s;settings_.updateScreens(s);requestUpdate();}
 bool transparencySupported()const{return transparency_;}void setTransparencySupported(bool);
 // Embedded in the login and lock screens: no settings gear, no input method label and no Hide or Language key (the keyboard is
 // always shown and there is no input method to switch); their places stay empty.
 void setEmbedded(bool value){if(embedded_==value)return;embedded_=value;requestUpdate();}
 // X11 desktop: a mouse drag asks the window manager to move the window (systemMoveStarting first), as a DTK title bar does.
 void setSystemMove(bool value){systemMove_=value;}
 void measureNextFrame(const QElapsedTimer&,quint64);
 double firstFrameMilliseconds()const{return frameMs_;}quint64 firstFrameSerial()const{return frameMs_<0?0:frameSerial_;}
 QImage grabWindow();
 QAccessibleInterface *accessibleRoot()const override{return QAccessible::queryAccessibleInterface(const_cast<NativePanel*>(this));}
 QWindow *accessibleWindow()const override{return const_cast<NativePanel*>(this);}
 QVector<NativeControl> accessibleControls()const override;
 bool invokeAccessible(const QString&,const QString&)override;
signals:
 void settingsRequested();void moveRequested(double,double);void systemMoveStarting();void minimumHeightChanged();void expandedChanged();
protected:bool event(QEvent*)override;void exposeEvent(QExposeEvent*)override;void resizeEvent(QResizeEvent*)override;
private:
 struct Contact {NativeControl control;QString scrollArea;QPointF originGlobal,previousGlobal,originLocal;bool moving=false,scrolling=false,alternative=false,suppressed=false;qint64 holdAt=-1;quint64 context=0;};
 void paint();void render(QPainter&,QSizeF);void refresh();void updateCorners(bool=false);
 void pointerBegin(int,QPointF,QPointF);void pointerMove(int,QPointF,QPointF);void pointerEnd(int,QPointF,bool=false);
 void finishScroll(const QString&,bool forward=true,bool backward=true);
 void cancelContacts();void activate(const NativeControl&,QPointF);void scroll(const QString&,qreal);
 void longPress();void scheduleHold();void updateIcon();NativeStyle style()const;
 QRectF keyArea(QSizeF)const;QRectF candidateViewport(QSizeF)const;void addControl(NativeControl);
 Preferences &prefs_;FcitxClient &input_;KeyboardState &state_;QBackingStore store_;NativeX11Output x11Output_;NativeSettings settings_;NativeCandidateRows candidateRows_;
 QVector<NativeControl> controls_;QHash<int,Contact> contacts_;int dragOwner_=-999999;
 quint64 alternativeGeneration_=0,alternativeContext_=0;bool layoutCompact_=false;
 bool expanded_=false,functionRow_=false,transparency_=true,painting_=false,embedded_=false,systemMove_=false;QStringList screens_;
 quint64 candidateContext_=0,candidateGeneration_=0;QStringList words_;qreal candidateOffset_=0,candidateContent_=0,expandedOffset_=0,expandedContent_=0;
 QTimer holdTimer_;QElapsedTimer pointerClock_;QStringList alternatives_;QRectF alternativeRect_;QString hovered_;
 QElapsedTimer frameStart_;quint64 frameSerial_=0;double frameMs_=-1;
 QImage icon_;int iconPixels_=0;QVector<qreal> candidateWidths_;QFont candidateFont_;qreal measuredWidth_=0;
 std::unique_ptr<X11CompositorWatcher> compositor_;std::unique_ptr<Dtk::Gui::DPlatformHandle> decoration_;
};
