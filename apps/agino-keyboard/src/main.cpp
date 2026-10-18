// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QProcess>
#include <QEventLoop>
#include <sys/prctl.h>
#include <unistd.h>
#include <signal.h>
#include <QFont>
#include <memory>
#include <QDir>
#include <QQmlContext>
#include <QQuickView>
#include <QQuickItem>
#include <QQuickStyle>
#include <QSGRendererInterface>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QTimer>
#include <QDBusInterface>
#include <QDBusConnectionInterface>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <DGuiApplicationHelper>
#include "keyboardstate.h"
#include "keysound.h"
#include "fcitxclient.h"
#include "virtualkeyboardui.h"
#include "candidaterows.h"
#include "desktopadapter.h"
#include "preferences.h"
#include "panelview.h"
#include "startupdpi.h"
#include "startupconfig.h"
#include <QScopedValueRollback>
#if defined(Q_OS_LINUX) && defined(__GLIBC__)
#include <malloc.h>
#endif
using Dtk::Gui::DGuiApplicationHelper;
static bool enableVirtualKeyboard(){
 // Only opt in to the user's virtual-keyboard addon; never replace their profile.
 const QString path=QDir(qEnvironmentVariable("XDG_CONFIG_HOME",QDir::homePath()+"/.config")).filePath("fcitx5/conf/virtualkeyboard.conf");
 if(!enableVirtualKeyboardConfig(path))return false;
 auto msg=QDBusMessage::createMethodCall("org.fcitx.Fcitx5","/controller","org.fcitx.Fcitx.Controller1","ReloadConfig");
 msg.setAutoStartService(false);QDBusConnection::sessionBus().asyncCall(msg,1000);return true;
}
static void panelMessage(bool information,const QString &message){
 if(qobject_cast<QApplication*>(qApp)){
  if(information)QMessageBox::information(nullptr,QStringLiteral("屏幕键盘"),message);
  else QMessageBox::warning(nullptr,QStringLiteral("屏幕键盘"),message);
  return;
 }
 QProcess dialog;QEventLoop loop;
 const pid_t parent=getpid();dialog.setChildProcessModifier([parent]{if(prctl(PR_SET_PDEATHSIG,SIGTERM)<0||getppid()!=parent)_exit(2);});
 QObject::connect(&dialog,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&loop,&QEventLoop::quit);
 QObject::connect(&dialog,&QProcess::errorOccurred,&loop,&QEventLoop::quit);
 dialog.start(QCoreApplication::applicationFilePath(),{information?QStringLiteral("--information-message"):QStringLiteral("--error-message"),message});
 loop.exec();
}
class WindowManager:public QObject {
 Q_OBJECT
 Q_CLASSINFO("D-Bus Interface","org.agino.KeyboardUi1")
public:
 WindowManager(Preferences &p,FcitxClient &f,KeyboardState &s,DesktopAdapter &a):prefs(p),client(f),state(s),desktop(a){
  connect(&client,&FcitxClient::changed,this,&WindowManager::syncContext);
  // Track device sources without dismissing the panel on physical keys.
  // Fcitx notifications include shortcut releases and do not control visibility.
  if(!qEnvironmentVariableIsSet("AGINO_MANAGED_UI"))connect(&desktop,&DesktopAdapter::inputSourceChanged,&state,&KeyboardState::setSource);
  connect(&desktop,&DesktopAdapter::capabilitiesChanged,this,&WindowManager::apply);
  connect(&desktop,&DesktopAdapter::lockedChanged,this,[this](bool locked){if(locked_==locked){if(!managed_)client.suspend(locked);return;}locked_=locked;state.cancelAll();state.setSource(KeyboardState::Mouse);state.dismiss();if(!managed_)client.suspend(locked);syncContext();});
  connect(&desktop,&DesktopAdapter::outputChanged,this,[this]{state.cancelAll();QTimer::singleShot(0,this,&WindowManager::resizePanel);});
  connect(&state,&KeyboardState::keyRequested,this,[this](KeySpec k,bool release){if(client.sendKey(k,release,client.contextEpoch())&&!release)sound.play();});
  connect(&state,&KeyboardState::visibleChanged,this,[this](bool visible){if(visible){idleTimer.stop();if(!showRequested_)showStart.start();showRequested_=false;if(!ensurePanel()||!validSize()||!state.visible())return;view->measureNextFrame(showStart,++showSerial_);}client.setPanelVisible(visible);desktop.setPanelVisible(visible);if(!visible&&view&&view->rootObject()){view->rootObject()->setProperty("expanded",false);idleTimer.start();}});
  QObject::connect(&prefs,&Preferences::changed,this,&WindowManager::apply);
  if(managed_)connect(&prefs,&Preferences::changed,this,[this]{synchronizeBrokerPolicy(++policySerial_);});
  connect(&state,&KeyboardState::settingsOpenChanged,this,&WindowManager::syncContext);

  elapsed.start();timer.setInterval(16);connect(&timer,&QTimer::timeout,this,[this]{state.advanceClock(elapsed.elapsed());});
  idleTimer.setSingleShot(true);idleTimer.setInterval(3000);
  connect(&idleTimer,&QTimer::timeout,this,[this]{unloadPanel();retire();});
  const auto saved=QJsonDocument::fromJson(qgetenv("AGINO_UI_RESUME")).object().toVariantMap();
  if(saved.value("resuming").toBool()&&!saved.value("applyPreferred").toBool()){preferredInitialized_=true;preferredIm_=prefs.preferredIm();}
  state.setCapsLock(saved.value("capsLock").toBool());desktop.restorePlacement(saved);
  showSerial_=saved.value("showSerial").toULongLong();lastFrameSerial_=saved.value("firstFrameSerial").toULongLong();lastFrameMs_=saved.value("firstFrameMs",-1.).toDouble();
  auto publish=[this]{QTimer::singleShot(0,this,[this]{emit StateChanged(Status());});};
  connect(&state,&KeyboardState::visibleChanged,this,publish);connect(&state,&KeyboardState::settingsOpenChanged,this,publish);connect(&state,&KeyboardState::modifiersChanged,this,publish);
  connect(&state,&KeyboardState::visibleChanged,this,[this](bool v){if(v&&state.visible()){state.advanceClock(elapsed.elapsed());timer.start();}else timer.stop();});
 }
 void unloadPanel(){
   if(state.visible()||!view||view->isVisible())return;
   state.cancelAll();lastFrameMs_=view->firstFrameMilliseconds();lastFrameSerial_=view->firstFrameSerial();desktop.detach();view.reset();
   // Qt 6.8.0: reset this GUI thread's font cache after every panel/font user
   // in the QML engine is gone. Surviving fonts reload by cache id on reuse.
   // cleanup() is an internal Qt hook; reassess it when upgrading Qt.
   QFont::cleanup();
#if defined(Q_OS_LINUX) && defined(__GLIBC__)
   // Return free allocator pages after the QML engine has been destroyed.
   // Run once per idle unload, never while typing or on short hide/show bursts.
   malloc_trim(0);
#endif
 }
 bool ensurePanel(){
  idleTimer.stop();
  if(view&&view->rootObject())return true;
  if(!view){view=std::make_unique<PanelView>();view->setResizeMode(QQuickView::SizeRootObjectToView);view->setTitle(tr("屏幕键盘"));view->setScreen(selectedScreen());if(!desktop.attach(view.get())){view.reset();state.dismiss();panelMessage(false,desktop.status());return false;}}
  view->setInitialProperties({{"controller",QVariant::fromValue(&state)},{"input",QVariant::fromValue(&client)},{"functionRow",prefs.functionRow()},{"preferences",QVariant::fromValue(&prefs)},{"screenNames",availableScreenNames()}});
  view->setSource(QUrl("qrc:/qml/KeyboardPanel.qml"));
  if(!view->rootObject()){state.dismiss();return false;}
  QObject::connect(view->rootObject(),SIGNAL(settingsRequested()),this,SLOT(Settings()));
  QObject::connect(view->rootObject(),SIGNAL(moveRequested(double,double)),this,SLOT(MoveBy(double,double)));
  QObject::connect(view->rootObject(),SIGNAL(minimumHeightChanged()),this,SLOT(Resize()));
  resizePanel();return true;
 }
 QQuickView *panel()const{return view.get();}
 void synchronizeBrokerPolicy(quint64 serial){
  state.cancelAll();client.setPolicyBlocked(true);
  auto m=QDBusMessage::createMethodCall("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1","PreferencesChanged");
  auto watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m,2000),this);
  connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,watcher,serial]{
   QDBusPendingReply<bool> reply=*watcher;watcher->deleteLater();if(serial!=policySerial_)return;
   if(reply.isError()||!reply.value()){QTimer::singleShot(100,this,[this,serial]{if(serial==policySerial_)synchronizeBrokerPolicy(serial);});return;}
   client.refreshSnapshot();client.setPolicyBlocked(false);
  });
 }
 void sizeWarning(){state.dismiss();QTimer::singleShot(0,this,[this]{if(state.layoutFits())return;unloadPanel();warningOpen_=true;panelMessage(true,tr("当前屏幕缩放不足以容纳完整键盘，请选择紧凑模式或减小缩放。"));warningOpen_=false;idleTimer.start();});}
 bool validSize(){if(state.layoutFits())return true;sizeWarning();return false;}
 QStringList availableScreenNames()const{QStringList names;for(auto screen:qGuiApp->screens())names<<screen->name();return names;}
 QScreen *selectedScreen()const{auto screens=qGuiApp->screens();auto screen=prefs.screenIndex()>=0&&prefs.screenIndex()<screens.size()?screens[prefs.screenIndex()]:desktop.targetOutput();return screen?screen:qGuiApp->primaryScreen();}
 // Snapshot callbacks often leave geometry metadata unchanged. Avoid dirtying
 // QSettings and its directory watcher for every key/focus update.
 void resizePanel(){if(resizing_)return;QScopedValueRollback<bool> guard(resizing_,true);auto screens=qGuiApp->screens();auto screen=selectedScreen();if(!screen)return;QSettings metadata;if(metadata.value("desktopStatus").toString()!=desktop.status())metadata.setValue("desktopStatus",desktop.status());QStringList availableNames;for(auto item:screens)availableNames<<item->name();if(metadata.value("screenNames").toStringList()!=availableNames)metadata.setValue("screenNames",availableNames);if(!view||!view->rootObject())return;view->rootObject()->setProperty("screenNames",availableScreenNames());int minimum=view->rootObject()->property("minimumHeight").toInt();auto g=screen->geometry();const QSize requested(prefs.compact()?524:qMax(828,qMin(934,g.width()-24)),minimum);const bool wasVisible=state.visible();state.setLayoutFits(g.width()>=requested.width()&&g.height()>=requested.height());if(!state.layoutFits()){if(wasVisible)sizeWarning();return;}desktop.configurePanel(screen,requested);QSettings info;const QString effectiveSize=QString("%1 × %2 px").arg(view->width()).arg(view->height());if(info.value("lastEffectiveSize").toString()!=effectiveSize)info.setValue("lastEffectiveSize",effectiveSize);if(info.value("desktopStatus").toString()!=desktop.status())info.setValue("desktopStatus",desktop.status());QStringList names;for(auto s:screens)names<<s->name();if(info.value("screenNames").toStringList()!=names)info.setValue("screenNames",names);}
 void syncContext(){state.setContext(client.contextEpoch(),client.editable()&&!state.settingsOpen()&&!locked_,client.sensitive());if(!client.connected()){state.cancelAll();if(wasConnected_)state.dismiss();}wasConnected_=client.connected();if(!managed_&&preferredPending_&&!state.settingsOpen()&&!locked_&&client.editable()&&!client.sensitive()&&client.inputMethods().contains(preferredIm_)){preferredPending_=false;if(client.inputMethod()!=preferredIm_)client.setInputMethod(preferredIm_);}QTimer::singleShot(0,this,&WindowManager::resizePanel);}
 void apply(){sound.setEnabled(prefs.keySound());if(!preferredInitialized_||preferredIm_!=prefs.preferredIm()){preferredInitialized_=true;preferredIm_=prefs.preferredIm();preferredPending_=!preferredIm_.isEmpty();}state.cancelAll();auto mode=KeyboardState::Mode(prefs.mode());if(mode==KeyboardState::Automatic&&!desktop.capabilities().automaticReady())mode=KeyboardState::Manual;state.setMode(mode);DGuiApplicationHelper::instance()->setPaletteType(DGuiApplicationHelper::ColorType(prefs.theme()));if(view&&view->rootObject())view->rootObject()->setProperty("functionRow",prefs.functionRow());if(!managed_)client.setSafeInput(prefs.safeInput());resizePanel();syncContext();}
 void compositorShow(){syncContext();}
 void compositorHide(){state.cancelAll();client.refreshSnapshot();syncContext();}
public slots:
 QVariantMap Status()const{auto value=desktop.placement();const QVariantMap status{{"capsLock",bool(state.modifiers()&2)},{"modifiers",state.modifiers()},{"shortcutError",shortcutError_},{"inputMethodName",client.inputMethodNames().value(client.inputMethod(),client.inputMethod()).toString()},{"inputMethodStatus",client.inputMethodStatus()},{"inputLanguageCode",client.inputLanguageCode()},{"methodsCount",client.inputMethods().size()},{"epoch",QVariant::fromValue(client.contextEpoch())},{"generation",QVariant::fromValue(client.candidateEpoch())},{"connected",client.connected()},{"editable",client.editable()},{"sensitive",client.sensitive()},{"expanded",view&&view->rootObject()&&view->rootObject()->property("expanded").toBool()},{"transparencySupported",view&&view->rootObject()&&view->rootObject()->property("transparencySupported").toBool()},{"panelX",view?view->x():0},{"panelY",view?view->y():0},{"panelWidth",view?view->width():0},{"panelHeight",view?view->height():0},{"visible",state.visible()},{"settingsVisible",state.settingsOpen()},{"panelLoaded",view&&view->rootObject()!=nullptr},{"viewCreated",bool(view)},{"showSerial",QVariant::fromValue(showSerial_)},{"firstFrameSerial",QVariant::fromValue(view?view->firstFrameSerial():lastFrameSerial_)},{"firstFrameMs",view?view->firstFrameMilliseconds():lastFrameMs_},{"graphicsApi",view?int(view->rendererInterface()->graphicsApi()):int(QSGRendererInterface::Unknown)},{"platform",QGuiApplication::platformName()},{"automatic",desktop.capabilities().automaticReady()},{"desktop",desktop.status()}};for(auto it=status.begin();it!=status.end();++it)value.insert(it.key(),it.value());return value;}
 void Show(){if(locked_||state.settingsOpen()||prefs.mode()==2)return;if(state.visible()){state.manualShow();return;}showStart.start();showRequested_=true;if(ensurePanel()&&validSize())state.manualShow();if(!state.visible()){showRequested_=false;if(view)idleTimer.start();}}
 void Hide(){state.dismiss();}
 void Toggle(){if(state.visible())Hide();else Show();}
 void CompositorHide(){compositorHide();}
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
  if(!qEnvironmentVariableIsSet("AGINO_MANAGED_UI")||state.visible()||warningOpen_)return;
  auto m=QDBusMessage::createMethodCall("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1","ReleaseIdle");m.setArguments({Status()});
  auto watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m,2000),this);
  connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,watcher]{QDBusPendingReply<bool> r=*watcher;watcher->deleteLater();if(!r.isError()&&r.value())qApp->quit();else if(!state.visible())idleTimer.start();});
 }
 KeySound sound;Preferences &prefs;FcitxClient &client;KeyboardState &state;DesktopAdapter &desktop;std::unique_ptr<PanelView> view;const bool managed_=qEnvironmentVariableIsSet("AGINO_MANAGED_UI");bool warningOpen_=false,resizing_=false,wasConnected_=false,locked_=false,preferredInitialized_=false,preferredPending_=false,showRequested_=false;QString preferredIm_,shortcutError_;QElapsedTimer elapsed,showStart;QTimer timer,idleTimer;quint64 policySerial_=0,showSerial_=0,lastFrameSerial_=0;double lastFrameMs_=-1;
};
int main(int argc,char **argv){
 // This panel draws 2D text and keys. Avoid a GL/LLVM pipeline for that work;
 // respect explicit Qt backend overrides for users who need hardware rendering.
 if(qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND")&&qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND"))qputenv("QT_QUICK_BACKEND","software");
 bool settings=false,hidden=false;QString screenshot,errorMessage;bool information=false;
 for(int i=1;i<argc;++i){if((QByteArray(argv[i])=="--error-message"||QByteArray(argv[i])=="--information-message")&&i+1<argc){information=QByteArray(argv[i])=="--information-message";errorMessage=QString::fromLocal8Bit(argv[++i]);}else if(QByteArray(argv[i])=="--settings")settings=true;if(QByteArray(argv[i])=="--hidden")hidden=true;if(QByteArray(argv[i])=="--screenshot"&&i+1<argc)screenshot=QString::fromLocal8Bit(argv[++i]);}
 if(qEnvironmentVariable("XDG_SESSION_TYPE")=="wayland"||!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))qputenv("QT_WAYLAND_SHELL_INTEGRATION","agino-keyboard");
 // The GUI connects normally; only compositor-launched Fcitx receives WAYLAND_SOCKET.
 qunsetenv("WAYLAND_SOCKET");QQuickStyle::setStyle("Chameleon");
 StartupDpiPolicy dpiPolicy;std::unique_ptr<QGuiApplication> application;if(!errorMessage.isEmpty())application=std::make_unique<QApplication>(argc,argv);else application=std::make_unique<QGuiApplication>(argc,argv);auto &app=*application;dpiPolicy.restore();app.setOrganizationName("agino");app.setApplicationName("agino-keyboard");app.setApplicationDisplayName("屏幕键盘");app.setQuitOnLastWindowClosed(false);
 if(!errorMessage.isEmpty()){panelMessage(information,errorMessage);return information?0:2;}
 qmlRegisterType<CandidateRows>("org.agino.keyboard",1,0,"CandidateRows");
 auto bus=QDBusConnection::sessionBus();const QString service=qEnvironmentVariableIsSet("AGINO_MANAGED_UI")?"org.agino.KeyboardPanel1":"org.agino.KeyboardUi1";
 if(!bus.registerService(service)){auto m=QDBusMessage::createMethodCall(service,"/Keyboard",service,settings?"Settings":"Show");bus.asyncCall(m,1000);return 0;}
 Preferences prefs;FcitxClient client;
 VirtualKeyboardUi ui;if(!qEnvironmentVariableIsSet("AGINO_MANAGED_UI")&&!ui.start()){panelMessage(false,QStringLiteral("已有屏幕键盘占用 Fcitx5 界面，请先关闭该应用。"));return 2;}if(ui.ownsInterface()&&!enableVirtualKeyboard()){panelMessage(false,QStringLiteral("无法保存 Fcitx5 屏幕键盘配置。"));return 2;}
 KeyboardState state;DesktopAdapter desktop;
 if(QGuiApplication::platformName().startsWith("wayland")&&!desktop.capabilities().waylandReady()){qCritical().noquote()<<desktop.status();return 4;}
 if(qEnvironmentVariableIsSet("AGINO_MANAGED_UI")){
  auto parentWatcher=new QDBusServiceWatcher("org.agino.KeyboardUi1",bus,QDBusServiceWatcher::WatchForOwnerChange,&app);
  const QString parentOwner=bus.interface()->serviceOwner("org.agino.KeyboardUi1").value();
  if(parentOwner.isEmpty())return 2;
  QObject::connect(parentWatcher,&QDBusServiceWatcher::serviceOwnerChanged,&app,[&app,parentOwner](const QString &,const QString &,const QString &owner){if(owner!=parentOwner)app.quit();});
 }
 WindowManager manager(prefs,client,state,desktop);bus.registerObject("/Keyboard",&manager,QDBusConnection::ExportAllSlots|QDBusConnection::ExportAllSignals);
 QObject::connect(&ui,&VirtualKeyboardUi::inputMethodsChanged,&client,&FcitxClient::refreshInputMethodNames);
 QObject::connect(&ui,&VirtualKeyboardUi::showRequested,&manager,&WindowManager::compositorShow);QObject::connect(&ui,&VirtualKeyboardUi::hideRequested,&manager,&WindowManager::compositorHide);
 manager.apply();if(qEnvironmentVariableIsSet("AGINO_MANAGED_UI")){
  manager.ScheduleIdle();auto ready=QDBusMessage::createMethodCall("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1","WorkerReady");bus.asyncCall(ready,2000);
 }
 if(settings)QTimer::singleShot(0,&manager,&WindowManager::Settings);else if(!hidden)QTimer::singleShot(0,&manager,&WindowManager::Show);
 if(!screenshot.isEmpty())QTimer::singleShot(800,&app,[&]{if(manager.ensurePanel())manager.panel()->grabWindow().save(screenshot);app.quit();});
 QObject::connect(&app,&QCoreApplication::aboutToQuit,&state,&KeyboardState::cancelAll);return app.exec();
}
#include "main.moc"
