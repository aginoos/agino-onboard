// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QProcess>
#include <QEventLoop>
#include <sys/prctl.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <memory>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QTimer>
#include <QDBusInterface>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <DGuiApplicationHelper>
#include "i18n.h"
#include "keyboardstate.h"
#include "keysound.h"
#include "fcitxclient.h"
#include "desktopadapter.h"
#include "preferences.h"
#include "native/nativepanel.h"
#include "startupdpi.h"
#include "panelstatus.h"
#include "log.h"
#include "peercheck.h"
#include <QScopedValueRollback>
using Dtk::Gui::DGuiApplicationHelper;
// The path of a program as /proc/<pid>/exe shows it (links resolved); the path itself when the file is not there.
static QString programPath(const QString &path){const QString real=QFileInfo(path).canonicalFilePath();return real.isEmpty()?path:real;}
// Where the calls to the controller go: its verified unique name once known, so a program that claims the name later gets nothing.
static QString controllerName=QStringLiteral("org.agino.KeyboardUi1");
static void panelMessage(bool information,const QString &message){
 QProcess dialog;QEventLoop loop;
 const pid_t parent=getpid();dialog.setChildProcessModifier([parent]{if(prctl(PR_SET_PDEATHSIG,SIGTERM)<0||getppid()!=parent)_exit(2);});
 QObject::connect(&dialog,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&loop,&QEventLoop::quit);
 QObject::connect(&dialog,&QProcess::errorOccurred,&loop,&QEventLoop::quit);
 dialog.start(QCoreApplication::applicationDirPath()+"/agino-keyboard-dialog",{information?QStringLiteral("--information-message"):QStringLiteral("--error-message"),message});
 loop.exec();
}
class WindowManager:public QObject {
 Q_OBJECT
 Q_CLASSINFO("D-Bus Interface","org.agino.KeyboardUi1")
public:
 // resume: what the controller handed over (--resume-json) from the window that ended before.
 WindowManager(Preferences &p,FcitxClient &f,KeyboardState &s,DesktopAdapter &a,const PanelStatus &resume):prefs(p),client(f),state(s),desktop(a){
  connect(&client,&FcitxClient::changed,this,&WindowManager::syncContext);
  connect(&desktop,&DesktopAdapter::capabilitiesChanged,this,&WindowManager::apply);
  connect(&desktop,&DesktopAdapter::lockedChanged,this,[this](bool locked){if(locked_==locked)return;locked_=locked;state.cancelAll();state.setSource(KeyboardState::Mouse);state.dismiss();syncContext();});
  connect(&desktop,&DesktopAdapter::outputChanged,this,[this]{state.cancelAll();QTimer::singleShot(0,this,&WindowManager::resizePanel);});
  connect(&state,&KeyboardState::keyRequested,this,[this](KeySpec k,bool release){if(client.sendKey(k,release,client.contextEpoch())&&!release)sound.play();});
  connect(&state,&KeyboardState::visibleChanged,this,[this](bool visible){if(visible){idleTimer.stop();if(!showRequested_)showStart.start();showRequested_=false;if(!ensurePanel()||!validSize()||!state.visible())return;view->measureNextFrame(showStart,++showSerial_);}client.setPanelVisible(visible);desktop.setPanelVisible(visible);if(!visible&&view&&view->rootObject()){view->rootObject()->setProperty("expanded",false);idleTimer.start();}});
  QObject::connect(&prefs,&Preferences::changed,this,&WindowManager::apply);
  connect(&prefs,&Preferences::changed,this,[this]{synchronizeBrokerPolicy(++policySerial_);});
  connect(&state,&KeyboardState::settingsOpenChanged,this,&WindowManager::syncContext);

  state.useWallClock();timer.setInterval(16);connect(&timer,&QTimer::timeout,this,[this]{state.tick();});
  idleTimer.setSingleShot(true);idleTimer.setInterval(3000);
  connect(&idleTimer,&QTimer::timeout,this,[this]{unloadPanel();retire();});
  state.setCapsLock(resume.capsLock);desktop.restorePlacement(resume.toVariantMap(true));
  showSerial_=resume.showSerial;lastFrameSerial_=resume.firstFrameSerial;lastFrameMs_=resume.firstFrameMs;
  auto publish=[this]{QTimer::singleShot(0,this,[this]{emit StateChanged(Status());});};
  connect(&state,&KeyboardState::visibleChanged,this,publish);connect(&state,&KeyboardState::settingsOpenChanged,this,publish);
  connect(&state,&KeyboardState::clockNeededChanged,this,[this](bool needed){if(needed)timer.start();else timer.stop();});
 }
 // The window of the controller ends right after this, which returns everything to the system.
 void unloadPanel(){
   if(state.visible()||!view||view->isVisible())return;
   state.cancelAll();lastFrameMs_=view->firstFrameMilliseconds();lastFrameSerial_=view->firstFrameSerial();desktop.detach();view.reset();
 }
 bool ensurePanel(){
  idleTimer.stop();
  if(view&&view->rootObject())return true;
  if(!view){view=std::make_unique<NativePanel>(prefs,client,state);view->setTitle(tr("Screen Keyboard"));view->setScreen(selectedScreen());if(!desktop.attach(view.get())){view.reset();state.dismiss();panelMessage(false,desktop.status());return false;}}
  view->setScreenNames(availableScreenNames());
  if(!view->rootObject()){state.dismiss();return false;}
  QObject::connect(view->rootObject(),SIGNAL(settingsRequested()),this,SLOT(Settings()));
  QObject::connect(view->rootObject(),SIGNAL(moveRequested(double,double)),this,SLOT(MoveBy(double,double)));
  QObject::connect(view->rootObject(),SIGNAL(minimumHeightChanged()),this,SLOT(Resize()));
  if(!desktop.wayland()){view->setSystemMove(true);QObject::connect(view.get(),&NativePanel::systemMoveStarting,&desktop,&DesktopAdapter::beginSystemMove);}
  resizePanel();return true;
 }
 NativePanel *panel()const{return view.get();}
 void synchronizeBrokerPolicy(quint64 serial,int attempt=0){
  state.cancelAll();client.setPolicyBlocked(true);
  auto m=QDBusMessage::createMethodCall(controllerName,"/Keyboard","org.agino.KeyboardUi1","PreferencesChanged");
  auto watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m,2000),this);
  connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,watcher,serial,attempt]{
   QDBusPendingReply<bool> reply=*watcher;watcher->deleteLater();if(serial!=policySerial_)return;
   if(reply.isError()||!reply.value()){QTimer::singleShot(qMin(100<<qMin(attempt,5),2000),this,[this,serial,attempt]{if(serial==policySerial_)synchronizeBrokerPolicy(serial,attempt+1);});return;}
   client.refreshSnapshot();client.setPolicyBlocked(false);
  });
 }
 void sizeWarning(){state.dismiss();QTimer::singleShot(0,this,[this]{if(state.layoutFits())return;unloadPanel();warningOpen_=true;panelMessage(true,tr("The screen scaling is too large for the full keyboard. Choose the compact layout or reduce the scaling."));warningOpen_=false;idleTimer.start();});}
 bool validSize(){if(state.layoutFits())return true;sizeWarning();return false;}
 QStringList availableScreenNames()const{QStringList names;for(auto screen:qGuiApp->screens())names<<screen->name();return names;}
 QScreen *selectedScreen()const{auto screens=qGuiApp->screens();auto screen=prefs.screenIndex()>=0&&prefs.screenIndex()<screens.size()?screens[prefs.screenIndex()]:desktop.targetOutput();return screen?screen:qGuiApp->primaryScreen();}
 // Snapshot callbacks often leave geometry metadata unchanged. Avoid dirtying
 // QSettings and its directory watcher for every key/focus update.
 void resizePanel(){if(resizing_)return;QScopedValueRollback<bool> guard(resizing_,true);auto screens=qGuiApp->screens();auto screen=selectedScreen();if(!screen)return;if(!view||!view->rootObject())return;view->rootObject()->setProperty("screenNames",availableScreenNames());int minimum=view->rootObject()->property("minimumHeight").toInt();auto g=screen->geometry();const QSize requested(prefs.compact()?524:qMax(828,qMin(934,g.width()-24)),minimum);const bool wasVisible=state.visible();state.setLayoutFits(g.width()>=requested.width()&&g.height()>=requested.height());if(!state.layoutFits()){if(wasVisible)sizeWarning();return;}desktop.configurePanel(screen,requested);}
 void syncContext(){state.setContext(client.contextEpoch(),client.editable()&&!state.settingsOpen()&&!locked_,client.sensitive());if(!client.connected()){state.cancelAll();if(wasConnected_)state.dismiss();}wasConnected_=client.connected();QTimer::singleShot(0,this,&WindowManager::resizePanel);}
 void apply(){sound.setEnabled(prefs.keySound());state.cancelAll();auto mode=KeyboardState::Mode(prefs.mode());if(mode==KeyboardState::Automatic&&!desktop.capabilities().automaticReady())mode=KeyboardState::Manual;state.setMode(mode);DGuiApplicationHelper::instance()->setPaletteType(DGuiApplicationHelper::ColorType(prefs.theme()));if(view&&view->rootObject())view->rootObject()->setProperty("functionRow",prefs.functionRow());resizePanel();syncContext();}
public slots:
 QVariantMap Status()const{
  PanelStatus s=PanelStatus::fromVariantMap(desktop.placement());
  QObject *root=view?view->rootObject():nullptr;
  s.capsLock=bool(state.modifiers()&2);s.modifiers=state.modifiers();s.shortcutError=shortcutError_;
  s.inputMethodName=client.inputMethodNames().value(client.inputMethod(),client.inputMethod()).toString();s.inputMethodStatus=client.inputMethodStatus();s.inputLanguageCode=client.inputLanguageCode();s.methodsCount=client.inputMethods().size();
  s.epoch=client.contextEpoch();s.generation=client.candidateEpoch();s.connected=client.connected();s.editable=client.editable();s.sensitive=client.sensitive();
  s.expanded=root&&root->property("expanded").toBool();s.transparencySupported=root&&root->property("transparencySupported").toBool();
  s.panelX=view?view->x():0;s.panelY=view?view->y():0;s.panelWidth=view?view->width():0;s.panelHeight=view?view->height():0;
  s.visible=state.visible();s.settingsVisible=state.settingsOpen();s.panelLoaded=root!=nullptr;s.viewCreated=bool(view);
  s.showSerial=showSerial_;s.firstFrameSerial=view?view->firstFrameSerial():lastFrameSerial_;s.firstFrameMs=view?view->firstFrameMilliseconds():lastFrameMs_;
  s.graphicsApi=view?1:0;s.renderer=QStringLiteral("qtgui-raster");s.platform=QGuiApplication::platformName();s.automatic=desktop.capabilities().automaticReady();s.desktop=desktop.status();
  return s.toVariantMap();
 }
 void Show(){if(locked_||state.settingsOpen()||prefs.mode()==2)return;if(state.visible()){state.manualShow();return;}showStart.start();showRequested_=true;if(ensurePanel()&&validSize())state.manualShow();if(!state.visible()){showRequested_=false;if(view)idleTimer.start();}}
 void Hide(){state.dismiss();}
 void Toggle(){if(state.visible())Hide();else Show();}
 void CompositorHide(){state.cancelAll();client.refreshSnapshot();syncContext();}
 void ScheduleIdle(){synchronizeBrokerPolicy(++policySerial_);if(!state.visible())idleTimer.start();}
 void Settings(){if(locked_)return;state.cancelAll();if(ensurePanel()&&validSize()){client.refreshInputMethodNames();state.setSettingsOpen(true);}}

 void Resize(){resizePanel();}
 void MoveBy(double dx,double dy){state.cancelAll();desktop.moveBy(dx,dy);}
 void ReloadPreferences(){prefs.reload();}
 void RefreshInputMethods(){client.refreshInputMethodNames();}
signals:
 void StateChanged(const QVariantMap &state);
private:
 void retire(){
  if(state.visible()||warningOpen_)return;
  auto m=QDBusMessage::createMethodCall(controllerName,"/Keyboard","org.agino.KeyboardUi1","ReleaseIdle");m.setArguments({Status()});
  auto watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m,2000),this);
  connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,watcher]{QDBusPendingReply<bool> r=*watcher;watcher->deleteLater();if(!r.isError()&&r.value())qApp->quit();else if(!state.visible())idleTimer.start();});
 }
 KeySound sound;Preferences &prefs;FcitxClient &client;KeyboardState &state;DesktopAdapter &desktop;std::unique_ptr<NativePanel> view;bool warningOpen_=false,resizing_=false,wasConnected_=false,locked_=false,showRequested_=false;QString shortcutError_;QElapsedTimer elapsed,showStart;QTimer timer,idleTimer;quint64 policySerial_=0,showSerial_=0,lastFrameSerial_=0;double lastFrameMs_=-1;
};
int main(int argc,char **argv){
 // The native QtGui raster frontend has no Qt Quick/scenegraph backend.
 bool settings=false,hidden=false,managed=false,information=false;QString screenshot,errorMessage;QByteArray resumeJson;
 for(int i=1;i<argc;++i){
  const QByteArray a(argv[i]);
  if((a=="--error-message"||a=="--information-message")&&i+1<argc){information=a=="--information-message";errorMessage=QString::fromLocal8Bit(argv[++i]);}
  else if(a=="--resume-json"&&i+1<argc)resumeJson=argv[++i];
  else if(a=="--screenshot"&&i+1<argc&&qEnvironmentVariableIsSet("AGINO_TEST_HOOKS"))screenshot=QString::fromLocal8Bit(argv[++i]);
  else if(a=="--managed")managed=true;
  else if(a=="--settings")settings=true;
  else if(a=="--hidden")hidden=true;
 }
 // Only the controller starts the window (it hands over the state and verifies the window in turn); the dialog helper mode needs neither.
 if(!managed&&errorMessage.isEmpty()){std::fputs("agino-keyboard-ui is started by agino-keyboard\n",stderr);return 2;}
 if(qEnvironmentVariable("XDG_SESSION_TYPE")=="wayland"||!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))qputenv("QT_WAYLAND_SHELL_INTEGRATION","agino-keyboard");
 // The GUI connects normally; only compositor-launched Fcitx receives WAYLAND_SOCKET.
 qunsetenv("WAYLAND_SOCKET");
 // This input-provider surface has no editable controls. Its Fcitx bridge
 // is direct DBus; it must not register a second Fcitx input-client context.
 qputenv("QT_IM_MODULE","compose");
 StartupDpiPolicy dpiPolicy;std::unique_ptr<QGuiApplication> application=std::make_unique<QGuiApplication>(argc,argv);auto &app=*application;dpiPolicy.restore();AginoI18n::install(app);app.setOrganizationName("agino");app.setApplicationName("agino-keyboard");app.setApplicationDisplayName(QCoreApplication::translate("Keyboard","Screen Keyboard"));app.setQuitOnLastWindowClosed(false);
 if(!errorMessage.isEmpty()){panelMessage(information,errorMessage);return information?0:2;}

 auto bus=QDBusConnection::sessionBus();
 if(!bus.registerService("org.agino.KeyboardPanel1")){qCInfo(lcWindow())<<"A keyboard window of this session exists already";return 0;}
 Preferences prefs;FcitxClient client;
 KeyboardState state;DesktopAdapter desktop(nullptr,false);
 if(QGuiApplication::platformName().startsWith("wayland")&&!desktop.capabilities().waylandReady()){qCCritical(lcWindow()).noquote()<<desktop.status();return 4;}
 auto parentWatcher=new QDBusServiceWatcher("org.agino.KeyboardUi1",bus,QDBusServiceWatcher::WatchForOwnerChange,&app);
 const QString parentOwner=bus.interface()->serviceOwner("org.agino.KeyboardUi1").value();
 if(parentOwner.isEmpty())return 2;
 // Only the controller may drive the window and see its state (SEC-10); the window started by hand next to the controller counts as well.
 if(!PeerCheck::peerIs(bus,parentOwner,{programPath(QStringLiteral(AGINO_TRUSTED_CONTROLLER)),programPath(QCoreApplication::applicationDirPath()+"/agino-keyboard"),programPath(QCoreApplication::applicationDirPath()+"/../bin/agino-keyboard")})){qCWarning(lcWindow(),"org.agino.KeyboardUi1 is owned by %s, which is not the keyboard controller",qPrintable(parentOwner));return 2;}
 controllerName=parentOwner;
 QObject::connect(parentWatcher,&QDBusServiceWatcher::serviceOwnerChanged,&app,[&app,parentOwner](const QString &,const QString &,const QString &owner){if(owner!=parentOwner)app.quit();});
 WindowManager manager(prefs,client,state,desktop,PanelStatus::fromJson(resumeJson));if(!bus.registerObject("/Keyboard",&manager,QDBusConnection::ExportAllSlots|QDBusConnection::ExportAllSignals))qCCritical(lcWindow())<<"Cannot export the keyboard window object on the session bus:"<<bus.lastError().name();
 manager.apply();manager.ScheduleIdle();auto ready=QDBusMessage::createMethodCall(controllerName,"/Keyboard","org.agino.KeyboardUi1","WorkerReady");bus.asyncCall(ready,2000);
 if(settings)QTimer::singleShot(0,&manager,&WindowManager::Settings);else if(!hidden)QTimer::singleShot(0,&manager,&WindowManager::Show);
 if(!screenshot.isEmpty())QTimer::singleShot(800,&app,[&]{if(manager.ensurePanel())manager.panel()->grabWindow().save(screenshot);app.quit();});
 QObject::connect(&app,&QCoreApplication::aboutToQuit,&state,&KeyboardState::cancelAll);return app.exec();
}
#include "native-main.moc"
