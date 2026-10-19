// SPDX-License-Identifier: GPL-3.0-or-later
// Keep desktop control alive without loading any Qt graphics runtime.
#include <QCoreApplication>
#include <QDBusContext>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QProcess>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <sys/prctl.h>
#include <unistd.h>
#include <signal.h>
#include <QProcessEnvironment>
#include "i18n.h"
#include "keyboardstate.h"
#include "fcitxclient.h"
#include "virtualkeyboardui.h"
#include "preferences.h"
#include "startupconfig.h"
#include "idleplatform.h"
#include "peercheck.h"
#if defined(__GLIBC__)
#include <malloc.h>
#endif
// The path of a program as /proc/<pid>/exe shows it (links resolved); the path itself when the file is not there.
static QString programPath(const QString &path){const QString real=QFileInfo(path).canonicalFilePath();return real.isEmpty()?path:real;}
static constexpr auto publicService="org.agino.KeyboardUi1";
static constexpr auto workerService="org.agino.KeyboardPanel1";
static constexpr auto uiInterface="org.agino.KeyboardUi1";
static constexpr auto uiPath="/Keyboard";
class Broker final:public QObject,protected QDBusContext {
 Q_OBJECT
 Q_CLASSINFO("D-Bus Interface","org.agino.KeyboardUi1")
public:
 Broker(Preferences &p,FcitxClient &c,IdlePlatform &d):prefs(p),client(c),platform(d){
  auto bus=QDBusConnection::sessionBus();
  bus.connect(workerService,uiPath,uiInterface,"StateChanged",this,SLOT(workerState(QVariantMap,QDBusMessage)));
  connect(&worker,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus kind){
   const bool restart=retiring&&((code==0&&kind==QProcess::NormalExit)||retireKilled)&&!pending.isEmpty();retireKilled=false;
   if(!restart)pending.clear();
   retireTimer.stop();unanswered=0;ready=false;workerName.clear();retiring=false;cached["visible"]=false;cached["settingsVisible"]=false;cached["viewCreated"]=false;cached["panelLoaded"]=false;
   state.dismiss();if(code||kind==QProcess::CrashExit)cached["workerError"]=tr("The keyboard window has exited; show it again to restart it");
   if(restart)QTimer::singleShot(0,this,[this]{startWorker();});
   if(!capture.isEmpty())QCoreApplication::exit(code==0&&QFileInfo::exists(capture)?0:2);
#if defined(__GLIBC__)
   malloc_trim(0);
#endif
  });
  connect(&worker,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){ready=false;pending.clear();cached["workerError"]=worker.errorString();}});
  connect(&prefs,&Preferences::changed,this,&Broker::apply);
  connect(&client,&FcitxClient::changed,this,[this]{
   state.setContext(client.contextEpoch(),client.editable()&&!locked,client.sensitive());
   if(!client.connected()){state.dismiss();if(connectedBefore)request("Hide");}connectedBefore=client.connected();
   applyPreferred();
  });
  connect(&platform,&IdlePlatform::sourceChanged,this,[this](int kind,double x,double y){
   if(kind>=1&&kind<=3){sourceKind=kind;sourceX=x;sourceY=y;}state.setSource(KeyboardState::InputSource(kind));
  });
  connect(&platform,&IdlePlatform::lockedChanged,this,[this](bool value){locked=value;state.dismiss();client.suspend(value);if(value)request("Hide");});
  connect(&platform,&IdlePlatform::capabilitiesChanged,this,&Broker::apply);
  connect(&state,&KeyboardState::visibleChanged,this,[this](bool value){if(value&&!cached.value("visible").toBool())request("Show");});
  connect(&platform,&IdlePlatform::toggleRequested,this,&Broker::Toggle);
  connect(&platform,&IdlePlatform::showRequested,this,&Broker::Show);
  connect(&platform,&IdlePlatform::settingsRequested,this,&Broker::Settings);
  connect(&platform,&IdlePlatform::quitRequested,qApp,&QCoreApplication::quit);
  connect(&reload,&QTimer::timeout,&prefs,&Preferences::reload);reload.setSingleShot(true);reload.setInterval(50);
  // A keyboard window that agreed to end but does not must not leave the controller waiting for ever.
  retireTimer.setSingleShot(true);retireTimer.setInterval(5000);connect(&retireTimer,&QTimer::timeout,this,[this]{if(retiring&&worker.state()!=QProcess::NotRunning){qWarning("The keyboard window does not end, ending it");retireKilled=true;worker.kill();}});
  const QString folder=QFileInfo(QSettings().fileName()).absolutePath();QDir().mkpath(folder);files.addPath(folder);
  connect(&files,&QFileSystemWatcher::directoryChanged,this,[this]{reload.start();});
  apply();
 }
 ~Broker()override{if(worker.state()!=QProcess::NotRunning){worker.terminate();if(!worker.waitForFinished(3000)){worker.kill();worker.waitForFinished(1000);}}}
 void setCapture(QString path){capture=path;}
 void compositorShow(){client.refreshSnapshot();}
 void compositorHide(){client.refreshSnapshot();if(ready&&!retiring)relay("CompositorHide");}
public slots:
 void WorkerReady(){
  if(calledFromDBus()&&worker.state()!=QProcess::NotRunning&&fromWorker(message().service())){workerName=message().service();ready=true;dispatchPending();}
 }
 QVariantMap Status(){
  if(ready&&!retiring&&calledFromDBus()){
   auto requestMessage=message();auto connectionCopy=connection();setDelayedReply(true);
   auto m=QDBusMessage::createMethodCall(workerTarget(),uiPath,uiInterface,"Status");
   auto watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m,3000),this);
   connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,watcher,requestMessage,connectionCopy]{QDBusPendingReply<QVariantMap> r=*watcher;watcher->deleteLater();if(!r.isError())updateCache(r.value());connectionCopy.send(requestMessage.createReply(QVariantList{status()}));});
   return {};
  }
  return status();
 }
 void Show(){if(locked||prefs.mode()==2)return;request("Show");}
 void Settings(){if(!locked)request("Settings");}
 void Hide(){state.dismiss();request("Hide");}
 void Toggle(){if(locked||prefs.mode()==2){Hide();return;}if(worker.state()!=QProcess::NotRunning)request("Toggle");else Show();}
 bool PreferencesChanged(){prefs.reload();return client.connected()&&client.policyReady();}
 void ReloadPreferences(){prefs.reload();if(ready&&!retiring)relay("ReloadPreferences");}
 void RefreshInputMethods(){client.refreshInputMethodNames();if(ready)relay("RefreshInputMethods");}
 void MoveBy(double x,double y){if(ready&&!retiring)relay("MoveBy",{x,y});}
 void Resize(){if(ready&&!retiring)relay("Resize");}
 bool ReleaseIdle(const QVariantMap &value){
  if(!calledFromDBus()||!fromWorker(message().service())||activeRequests)return false;
  updateCache(value);if(cached.value("visible").toBool())return false;
  resume=value;resume["resuming"]=true;retiring=true;retireTimer.start();return true;
 }
 void workerState(const QVariantMap &value,const QDBusMessage &signal){if(fromWorker(signal.service()))updateCache(value);}
private:
 // Calls go to the verified connection once the window has said it is ready (the name may change hands afterwards).
 QString workerTarget()const{return workerName.isEmpty()?QString::fromLatin1(workerService):workerName;}
 // SEC-10: the sender owns the name of the keyboard window and runs the keyboard window program (or the one started by this process);
 // one verdict per unique name, forgotten when a window is started.
 bool fromWorker(const QString &sender){
  const auto owner=QDBusConnection::sessionBus().interface()->serviceOwner(workerService);
  if(!owner.isValid()||sender!=owner.value())return false;
  const auto known=verdicts.constFind(sender);if(known!=verdicts.cend())return known.value();
  const int verdict=PeerCheck::check(QDBusConnection::sessionBus(),sender,{programPath(QStringLiteral(AGINO_TRUSTED_UI)),workerExe});
  if(verdict<0)return false;   // the program could not be found: not remembered, asked again next time
  const bool ok=verdict>0;verdicts.insert(sender,ok);
  if(!ok)qWarning("Ignoring the bus connection %s: it owns %s but is not the keyboard window program",qPrintable(sender),workerService);
  return ok;
 }
 QVariantMap status()const{
  auto value=cached;for(auto k:{"visible","settingsVisible","viewCreated","panelLoaded","expanded","transparencySupported"})if(!value.contains(k))value[k]=false;for(auto k:{"panelX","panelY","panelWidth","panelHeight","graphicsApi","showSerial","firstFrameSerial"})if(!value.contains(k))value[k]=0;const bool alive=worker.state()!=QProcess::NotRunning;
  value["workerPid"]=alive?worker.processId():qint64(0);value["workerPids"]=QVariant::fromValue(alive?QList<qlonglong>{worker.processId()}:QList<qlonglong>{});
  value["shortcutError"]=platform.shortcutError();value["platform"]=platform.wayland()?"wayland":"xcb";value["automatic"]=platform.automaticReady();value["desktop"]=platform.status();
  if(!ready||retiring){
  value["connected"]=client.connected();value["editable"]=client.editable()&&!locked;value["sensitive"]=client.sensitive();
  value["epoch"]=QVariant::fromValue(client.contextEpoch());value["generation"]=QVariant::fromValue(client.candidateEpoch());
  value["inputMethodName"]=client.inputMethodNames().value(client.inputMethod(),client.inputMethod());value["inputMethodStatus"]=client.inputMethodStatus();value["inputLanguageCode"]=client.inputLanguageCode();value["methodsCount"]=client.inputMethods().size();
  }
  if(!alive){for(auto k:{"visible","settingsVisible","viewCreated","panelLoaded","expanded","transparencySupported"})value[k]=false;for(auto k:{"panelX","panelY","panelWidth","panelHeight","graphicsApi"})value[k]=0;}
  if(!value.contains("firstFrameMs"))value["firstFrameMs"]=-1.;
  return value;
 }
 void updateCache(QVariantMap value){cached=value;const bool applyPreferred=resume.value("applyPreferred").toBool();resume=value;resume["resuming"]=true;resume["applyPreferred"]=applyPreferred;if(!value.value("visible").toBool())state.dismiss();this->applyPreferred();}
 void applyPreferred(){if(preferredPending&&client.editable()&&!client.sensitive()&&!locked&&!cached.value("settingsVisible").toBool()&&client.inputMethods().contains(preferred)){preferredPending=false;if(client.inputMethod()!=preferred)client.setInputMethod(preferred);}}
 void apply(){
  if(!initialized||preferred!=prefs.preferredIm()){initialized=true;preferred=prefs.preferredIm();preferredPending=!preferred.isEmpty();resume["applyPreferred"]=true;}
  state.setMode(prefs.mode()==2?KeyboardState::Off:prefs.mode()==0&&platform.automaticReady()?KeyboardState::Automatic:KeyboardState::Manual);
  client.setSafeInput(prefs.safeInput());client.suspend(locked);platform.setShortcut(prefs.shortcut());applyPreferred();
 }
 void request(const QString &action){
  if(worker.state()==QProcess::NotRunning){if(action=="Hide")return;pending=action;startWorker();}
  else if(!ready||retiring){if(action=="Show"&&pending=="Settings")return;if(action=="Toggle")pending=(pending=="Show"||pending=="Settings")?"Hide":"Show";else pending=action;}
  else relay(action);
 }
 void startWorker(){
  if(worker.state()!=QProcess::NotRunning||pending.isEmpty())return;
  ++workerGeneration;ready=false;retiring=false;cached["workerError"]=QString();
  QString executable=QCoreApplication::applicationDirPath()+"/agino-keyboard-ui";
  if(!QFileInfo::exists(executable))executable=QCoreApplication::applicationDirPath()+"/../libexec/agino-keyboard-ui";
  workerExe=programPath(executable);verdicts.clear();workerName.clear();
  auto env=QProcessEnvironment::systemEnvironment();env.insert("AGINO_MANAGED_UI","1");
  auto handoff=resume;handoff["sourceKind"]=sourceKind;handoff["sourceX"]=sourceX;handoff["sourceY"]=sourceY;
  env.insert("AGINO_UI_RESUME",QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(handoff)).toJson(QJsonDocument::Compact)));
  worker.setProcessEnvironment(env);worker.setProcessChannelMode(QProcess::ForwardedChannels);
  QStringList args{"--hidden"};if(!capture.isEmpty())args<<"--screenshot"<<capture;
  worker.start(executable,args);resume["resuming"]=true;resume["applyPreferred"]=false;
 }
 void dispatchPending(){if(pending.isEmpty())return;auto action=pending;pending.clear();relay(action);}
 void relay(const QString &method,const QVariantList &arguments={}){
  auto m=QDBusMessage::createMethodCall(workerTarget(),uiPath,uiInterface,method);m.setArguments(arguments);++activeRequests;const auto generation=workerGeneration;
  auto watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m,10000),this);
  connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,watcher,generation]{
   QDBusPendingReply<> reply=*watcher;watcher->deleteLater();--activeRequests;
   // The calls of a window that has ended come back as "disconnected": they say nothing about the window of now.
   if(generation!=workerGeneration)return;
   // A worker that does not answer twice in a row hangs: end it, the next request starts a new one.
   const auto type=reply.isError()?reply.error().type():QDBusError::NoError;
   if((type==QDBusError::NoReply||type==QDBusError::Timeout||type==QDBusError::TimedOut)&&!reply.error().message().contains("disconnected")){if(++unanswered>=2&&worker.state()!=QProcess::NotRunning){unanswered=0;qWarning("The keyboard window does not answer, ending it");worker.kill();}}
   else unanswered=0;
  });
 }
 Preferences &prefs;FcitxClient &client;IdlePlatform &platform;KeyboardState state;QProcess worker;QFileSystemWatcher files;QTimer reload,retireTimer;
 QVariantMap cached,resume;QString pending,preferred,capture;bool ready=false,retiring=false,locked=false,initialized=false,preferredPending=false,connectedBefore=false;int activeRequests=0,unanswered=0,sourceKind=0;quint64 workerGeneration=0;bool retireKilled=false;double sourceX=0,sourceY=0;QHash<QString,bool> verdicts;QString workerExe,workerName;
};
static void errorDialog(const QString &message){
 QString path=QCoreApplication::applicationDirPath()+"/agino-keyboard-ui";if(!QFileInfo::exists(path))path=QCoreApplication::applicationDirPath()+"/../libexec/agino-keyboard-ui";
 QProcess dialog;const pid_t parent=getpid();dialog.setChildProcessModifier([parent]{if(prctl(PR_SET_PDEATHSIG,SIGTERM)<0||getppid()!=parent)_exit(2);});dialog.start(path,{"--error-message",message});if(dialog.waitForStarted()&&!dialog.waitForFinished(60000))dialog.kill();
}
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);app.setOrganizationName("agino");app.setApplicationName("agino-keyboard");AginoI18n::install(app);
 bool hidden=false,settings=false;QString capture;for(int i=1;i<argc;++i){QByteArray a(argv[i]);hidden|=a=="--hidden";settings|=a=="--settings";if(a=="--screenshot"&&i+1<argc&&qEnvironmentVariableIsSet("AGINO_TEST_HOOKS"))capture=QString::fromLocal8Bit(argv[++i]);}
 auto bus=QDBusConnection::sessionBus();
 if(!bus.registerService(publicService)){
  // Another instance runs: ask it to show the keyboard, but only when it is the controller (SEC-10); anything else holding the name gets nothing.
  const QString owner=bus.interface()->serviceOwner(publicService).value();
  if(!owner.isEmpty()&&!PeerCheck::peerIs(bus,owner,{programPath(QStringLiteral(AGINO_TRUSTED_CONTROLLER)),programPath(app.applicationFilePath())})){qWarning("%s is owned by %s, which is not the keyboard controller: not using it",publicService,qPrintable(owner));return 2;}
  auto m=QDBusMessage::createMethodCall(owner.isEmpty()?QString::fromLatin1(publicService):owner,uiPath,uiInterface,settings?"Settings":"Show");bus.asyncCall(m,1000);return 0;
 }
 VirtualKeyboardUi ui;if(!ui.start()){errorDialog(QCoreApplication::translate("Keyboard","Another screen keyboard is using the Fcitx5 interface. Close that application first."));return 2;}
 if(ui.ownsInterface()){
  const QString path=QDir(qEnvironmentVariable("XDG_CONFIG_HOME",QDir::homePath()+"/.config")).filePath("fcitx5/conf/virtualkeyboard.conf");
  bool configChanged=false;
  if(!enableVirtualKeyboardConfig(path,&configChanged)){errorDialog(QCoreApplication::translate("Keyboard","Cannot save the Fcitx5 screen keyboard configuration."));return 2;}
  if(configChanged){auto m=QDBusMessage::createMethodCall("org.fcitx.Fcitx5","/controller","org.fcitx.Fcitx.Controller1","ReloadConfig");m.setAutoStartService(false);bus.asyncCall(m,1000);}
 }
 Preferences prefs;FcitxClient client;IdlePlatform platform;Broker broker(prefs,client,platform);broker.setCapture(capture);
 bus.registerObject(uiPath,&broker,QDBusConnection::ExportAllSlots);
 QObject::connect(&ui,&VirtualKeyboardUi::inputMethodsChanged,&client,&FcitxClient::refreshInputMethodNames);
 QObject::connect(&ui,&VirtualKeyboardUi::showRequested,&broker,&Broker::compositorShow);QObject::connect(&ui,&VirtualKeyboardUi::hideRequested,&broker,&Broker::compositorHide);
 if(settings)QTimer::singleShot(0,&broker,&Broker::Settings);else if(!hidden||!capture.isEmpty())QTimer::singleShot(0,&broker,&Broker::Show);
 if(!capture.isEmpty()){
  QTimer::singleShot(15000,&app,[&]{app.exit(2);});
 }
 return app.exec();
}
#include "broker.moc"
