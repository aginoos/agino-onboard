#include <QtTest>
#include <QLineEdit>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QSettings>
#include <QKeyEvent>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnectionInterface>
#include <QProcess>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QSGRendererInterface>
#include <csignal>
#include <pwd.h>
#include <unistd.h>
#include "fcitxclient.h"
#include "virtualkeyboardui.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#undef Bool
struct StringPair {QString key,value;};
Q_DECLARE_METATYPE(StringPair)
QDBusArgument &operator<<(QDBusArgument &a,const StringPair &p){a.beginStructure();a<<p.key<<p.value;a.endStructure();return a;}
const QDBusArgument &operator>>(const QDBusArgument &a,StringPair &p){a.beginStructure();a>>p.key>>p.value;a.endStructure();return a;}
class SessionMock:public QObject {
 Q_OBJECT
 Q_CLASSINFO("D-Bus Interface","org.deepin.dde.SessionManager1")
 Q_PROPERTY(bool Locked READ locked)
public:bool locked()const{return false;}void notifyUnlocked(){emit LockedChanged(false);}
signals:void LockedChanged(bool);
};
class PreeditTarget:public QLineEdit {
public:QString preedit;
protected:void inputMethodEvent(QInputMethodEvent *event)override{preedit=event->preeditString();QLineEdit::inputMethodEvent(event);}
};
class KeyTarget:public QLineEdit {
public:QList<QPair<int,Qt::KeyboardModifiers>> releases;
protected:void keyReleaseEvent(QKeyEvent *e)override{releases.append({e->key(),e->modifiers()});QLineEdit::keyReleaseEvent(e);}
};
static QString sogouUid(){return QString::fromLocal8Bit(getpwuid(getuid())->pw_name)+"#im.module=fcitx5|im.name=com.sogou.ime.ng.fcitx5.deepin";}
class SogouWindowMonitor:public QObject {
 Q_OBJECT
public:
 QStringList shown,hidden;
 SogouWindowMonitor(){auto bus=QDBusConnection::sessionBus();bus.connect("com.cpis.panel","/com/cpis/panel","com.cpis.panel","ShowWindow",this,SLOT(show(QString,QByteArray,uint)));bus.connect("com.cpis.panel","/com/cpis/panel","com.cpis.panel","HideWindow",this,SLOT(hide(QString,QByteArray,uint)));}
public slots:
 void show(const QString &uid,const QByteArray &bytes,uint size){if(uid==sogouUid()&&size==uint(bytes.size()))shown.append(QString::fromUtf8(bytes.constData()));}
 void hide(const QString &uid,const QByteArray &bytes,uint size){if(uid==sogouUid()&&size==uint(bytes.size()))hidden.append(QString::fromUtf8(bytes.constData()));}
};
static QString sogouState(const QString &key){auto m=QDBusMessage::createMethodCall("com.cpis.panel","/com/cpis/panel","com.cpis.panel","AcquireEngineStat");m.setArguments({sogouUid(),key});return QDBusConnection::sessionBus().call(m).arguments().value(0).toString();}
class FcitxTest:public QObject {
 Q_OBJECT
 void focusTarget(QLineEdit &target,FcitxClient &client){
  target.show();QVERIFY(QTest::qWaitForWindowExposed(&target));
  if(QGuiApplication::platformName()=="xcb"){
   auto *display=XOpenDisplay(nullptr);QVERIFY(display);XSetInputFocus(display,Window(target.winId()),RevertToParent,CurrentTime);XSync(display,false);XCloseDisplay(display);
  }
  QTest::qWait(50);target.activateWindow();target.setFocus();QTRY_VERIFY(target.isActiveWindow()&&target.hasFocus());
  if(QGuiApplication::platformName()=="xcb"){
   auto backendFocused=[](){QDBusInterface controller("org.fcitx.Fcitx5","/controller","org.fcitx.Fcitx.Controller1");QDBusReply<QString> r=controller.call("DebugInfo");for(const auto &line:r.value().split('\n'))if(line.contains("program:test-fcitx ")&&line.endsWith("focus:1"))return true;return false;};
   QTRY_VERIFY_WITH_TIMEOUT(backendFocused(),5000);
  }
  client.refresh();QTRY_VERIFY_WITH_TIMEOUT(client.editable(),5000);QTRY_COMPARE(client.contextEpoch(),previousContext());

 }
 qint64 panelProcessId(const QProcess &process){QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");QDBusReply<QVariantMap> reply=ui.call("Status");return reply.isValid()&&reply.value().value("workerPid").toLongLong()>0?reply.value().value("workerPid").toLongLong():process.processId();}
 quint64 previousContext(){auto m=QDBusMessage::createMethodCall("org.agino.Keyboard1","/org/agino/Keyboard1","org.agino.Keyboard1","GetSnapshot");auto r=QDBusConnection::sessionBus().call(m);return r.arguments().isEmpty()?0:r.arguments().first().toULongLong();}
 bool candidatePopupVisible(Display *d){
  Window root,parent,*children=nullptr;unsigned count=0;bool visible=false;
  if(XQueryTree(d,DefaultRootWindow(d),&root,&parent,&children,&count)){
   for(unsigned n=0;n<count;++n){char *name=nullptr;XWindowAttributes a{};
    if(XFetchName(d,children[n],&name)&&name&&QByteArray(name)=="Fcitx5 Input Window"&&XGetWindowAttributes(d,children[n],&a)&&a.map_state==IsViewable)visible=true;
    if(name)XFree(name);
   }
   if(children)XFree(children);
  }
  return visible;
 }
 QRect panelRect(Display *d,qint64 pid){
  Window root=DefaultRootWindow(d),parent,*children=nullptr;unsigned count=0;QRect result;QList<Window> windows;
  if(XQueryTree(d,root,&root,&parent,&children,&count)){for(unsigned n=0;n<count;++n)windows.append(children[n]);if(children)XFree(children);}
  // Real window managers may reparent clients underneath their frame windows.
  Atom actual;int format;unsigned long size,left;unsigned char *clients=nullptr;
  XGetWindowProperty(d,root,XInternAtom(d,"_NET_CLIENT_LIST",False),0,4096,False,XA_WINDOW,&actual,&format,&size,&left,&clients);
  if(clients&&format==32)for(unsigned long n=0;n<size;++n)windows.append(reinterpret_cast<Window *>(clients)[n]);if(clients)XFree(clients);
  for(auto window:windows){unsigned char *data=nullptr;XWindowAttributes attributes{};
   XGetWindowProperty(d,window,XInternAtom(d,"_NET_WM_PID",False),0,1,False,XA_CARDINAL,&actual,&format,&size,&left,&data);
   const bool match=data&&size==1&&*reinterpret_cast<unsigned long*>(data)==static_cast<unsigned long>(pid);if(data)XFree(data);
   if(match&&XGetWindowAttributes(d,window,&attributes)&&attributes.map_state==IsViewable&&attributes.width>=524){int x,y;Window child;XTranslateCoordinates(d,window,root,0,0,&x,&y,&child);result=QRect(x,y,attributes.width,attributes.height);break;}
  }
  return result;
 }

private slots:
 void initTestCase(){qDBusRegisterMetaType<StringPair>();qDBusRegisterMetaType<QList<StringPair>>();if(qEnvironmentVariableIsSet("AGINO_TEST_XWAYLAND"))QCOMPARE(QGuiApplication::platformName(),QString("xcb"));}
 void cleanup(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_ALLOW_RESTART"))return;
  // A destroyed window's asynchronous frontend requests must settle before
  // another test creates a native window at a potentially reused address.
  auto unfocused=[](){auto m=QDBusMessage::createMethodCall("org.agino.Keyboard1","/org/agino/Keyboard1","org.agino.Keyboard1","GetSnapshot");auto r=QDBusConnection::sessionBus().call(m);return r.type()==QDBusMessage::ErrorMessage||(r.arguments().size()==12&&!r.arguments()[2].toBool());};
  QTRY_VERIFY_WITH_TIMEOUT(unfocused(),5000);QTest::qWait(50);
 }
 void sogouCandidatesAndPreedit(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_SOGOU"))QSKIP("Requires an isolated installed Sogou NG runtime");
  FcitxClient client;PreeditTarget target;focusTarget(target,client);
  client.setInputMethod("com.sogou.ime.ng.fcitx5.deepin");QTRY_COMPARE(client.inputMethod(),QString("com.sogou.ime.ng.fcitx5.deepin"));QTRY_VERIFY(client.editable());client.sendKey({0xff1b},false,client.contextEpoch());client.sendKey({0xff1b},true,client.contextEpoch());QTRY_VERIFY(sogouState("result_composition").isEmpty());
  client.setPanelVisible(true);QTRY_VERIFY_WITH_TIMEOUT(client.editable(),5000);
  const auto epoch=client.contextEpoch();
  for(char ch:QByteArray("nihao")){client.sendKey({quint32(ch)},false,epoch);client.sendKey({quint32(ch)},true,epoch);QTest::qWait(60);}
  QTRY_COMPARE_WITH_TIMEOUT(client.preedit(),QString("ni'hao"),5000);
  QTRY_VERIFY(client.candidates().size()>1);QCOMPARE(client.candidates(),sogouState("result_candidate_result").split('\n',Qt::SkipEmptyParts));const auto expected=client.candidates().at(1);QTRY_VERIFY(!target.preedit.isEmpty());QVERIFY(target.text().isEmpty());
  const auto oldVersion=client.candidateEpoch();client.selectCandidate(1,client.contextEpoch(),oldVersion);
  QTRY_COMPARE_WITH_TIMEOUT(target.text(),expected,5000);QTRY_VERIFY(target.preedit.isEmpty());
  QTRY_VERIFY(client.preedit().isEmpty());
  client.setPanelVisible(false);
 }
 void sogouPagingAndStaleSelection(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_SOGOU"))QSKIP("Requires isolated Sogou NG");
  FcitxClient client;PreeditTarget target;focusTarget(target,client);client.setInputMethod("com.sogou.ime.ng.fcitx5.deepin");QTRY_COMPARE(client.inputMethod(),QString("com.sogou.ime.ng.fcitx5.deepin"));QTRY_VERIFY(client.editable());client.sendKey({0xff1b},false,client.contextEpoch());client.sendKey({0xff1b},true,client.contextEpoch());QTRY_VERIFY(sogouState("result_composition").isEmpty());client.setPanelVisible(true);QTRY_VERIFY(client.editable());
  auto type=[&](const QByteArray &text){for(char ch:text){client.sendKey({quint32(ch)},false,client.contextEpoch());client.sendKey({quint32(ch)},true,client.contextEpoch());QTest::qWait(60);}};
  type("nihao");QTRY_COMPARE(client.preedit(),QString("ni'hao"));QTRY_VERIFY(client.hasNext());const auto first=client.candidates();const auto stale=client.candidateEpoch();
  client.requestPage(true);QTRY_VERIFY(client.candidates()!=first);QTRY_VERIFY(client.hasPrev());QVERIFY(target.text().isEmpty());
  client.selectCandidate(0,client.contextEpoch(),stale);QTest::qWait(100);QVERIFY(target.text().isEmpty());QVERIFY(!target.preedit.isEmpty());
  client.requestPage(false);QTRY_COMPARE(client.candidates(),first);QTRY_VERIFY(!client.hasPrev());
  client.sendKey({0xff1b},false,client.contextEpoch());client.sendKey({0xff1b},true,client.contextEpoch());QTRY_VERIFY(client.preedit().isEmpty());QTRY_VERIFY(target.preedit.isEmpty());
  type("nihao");QTRY_COMPARE(client.preedit(),QString("ni'hao"));QTRY_VERIFY(client.candidates().contains("尼"));client.selectCandidate(client.candidates().indexOf("尼"),client.contextEpoch(),client.candidateEpoch());QTRY_COMPARE(client.preedit(),QString("尼hao"));QVERIFY(target.text().isEmpty());
  client.selectCandidate(0,client.contextEpoch(),client.candidateEpoch());QTRY_VERIFY(!target.text().isEmpty());QTRY_VERIFY(target.preedit.isEmpty());client.setPanelVisible(false);
 }
 void sogouPopupRestoresWhenLeaseEnds(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_SOGOU")||QGuiApplication::platformName()!="xcb"||qEnvironmentVariableIsSet("AGINO_TEST_XWAYLAND"))QSKIP("CPIS independent window signals require native X11 frontend");
  SogouWindowMonitor windows;FcitxClient client;PreeditTarget target;focusTarget(target,client);client.setInputMethod("com.sogou.ime.ng.fcitx5.deepin");QTRY_COMPARE(client.inputMethod(),QString("com.sogou.ime.ng.fcitx5.deepin"));QTRY_VERIFY(client.editable());client.sendKey({0xff1b},false,client.contextEpoch());client.sendKey({0xff1b},true,client.contextEpoch());QTRY_VERIFY(sogouState("result_composition").isEmpty());QTRY_VERIFY(client.editable());
  for(char ch:QByteArray("nihao")){client.sendKey({quint32(ch)},false,client.contextEpoch());client.sendKey({quint32(ch)},true,client.contextEpoch());QTest::qWait(80);}
  QTRY_VERIFY(windows.shown.contains("composition"));windows.hidden.clear();windows.shown.clear();
  client.setPanelVisible(true);QTRY_COMPARE(client.preedit(),QString("ni'hao"));QTRY_VERIFY(windows.hidden.contains("composition"));
  QVERIFY(!windows.hidden.contains("status"));QVERIFY(!windows.hidden.contains("softkeyboard"));windows.shown.clear();
  client.setPanelVisible(false);QTRY_VERIFY(target.preedit.isEmpty());QTRY_VERIFY(client.candidates().isEmpty());client.sendKey({'h'},false,client.contextEpoch());client.sendKey({'h'},true,client.contextEpoch());QTRY_VERIFY(windows.shown.contains("composition"));
 }
 void sogouLeaseOwnerDisconnection(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_SOGOU"))QSKIP("Requires isolated Sogou NG");
  SogouWindowMonitor windows;FcitxClient client;PreeditTarget target;focusTarget(target,client);client.setInputMethod("com.sogou.ime.ng.fcitx5.deepin");QTRY_COMPARE(client.inputMethod(),QString("com.sogou.ime.ng.fcitx5.deepin"));QTRY_VERIFY(client.editable());client.sendKey({0xff1b},false,client.contextEpoch());client.sendKey({0xff1b},true,client.contextEpoch());QTRY_VERIFY(sogouState("result_composition").isEmpty());QTRY_VERIFY(client.editable());
  auto lease=std::make_unique<QDBusConnection>(QDBusConnection::connectToBus(QDBusConnection::SessionBus,"sogou-keyboard-lease"));
  auto m=QDBusMessage::createMethodCall("org.agino.Keyboard1","/org/agino/Keyboard1","org.agino.Keyboard1","SetPanelVisible");m.setArguments({true});QVERIFY(lease->call(m).type()!=QDBusMessage::ErrorMessage);client.refresh();
  for(char ch:QByteArray("nihao")){client.sendKey({quint32(ch)},false,client.contextEpoch());client.sendKey({quint32(ch)},true,client.contextEpoch());QTest::qWait(60);}
  QTRY_COMPARE(client.preedit(),QString("ni'hao"));QTRY_VERIFY(!target.preedit.isEmpty());if(QGuiApplication::platformName()=="xcb"&&!qEnvironmentVariableIsSet("AGINO_TEST_XWAYLAND"))QTRY_VERIFY(windows.hidden.contains("composition"));windows.shown.clear();
  QDBusConnection::disconnectFromBus("sogou-keyboard-lease");lease.reset();QTRY_VERIFY(target.preedit.isEmpty());QTRY_VERIFY(client.candidates().isEmpty());client.sendKey({'h'},false,client.contextEpoch());client.sendKey({'h'},true,client.contextEpoch());if(QGuiApplication::platformName()=="xcb"&&!qEnvironmentVariableIsSet("AGINO_TEST_XWAYLAND"))QTRY_VERIFY(windows.shown.contains("composition"));QVERIFY(target.text().isEmpty());
 }
 void sogouSensitiveInputAndFocusChange(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_SOGOU"))QSKIP("Requires isolated Sogou NG");
  FcitxClient client;PreeditTarget target;focusTarget(target,client);client.setInputMethod("com.sogou.ime.ng.fcitx5.deepin");QTRY_COMPARE(client.inputMethod(),QString("com.sogou.ime.ng.fcitx5.deepin"));QTRY_VERIFY(client.editable());client.sendKey({0xff1b},false,client.contextEpoch());client.sendKey({0xff1b},true,client.contextEpoch());QTRY_VERIFY(sogouState("result_composition").isEmpty());client.setPanelVisible(true);QTRY_VERIFY(client.editable());
  for(char ch:QByteArray("nihao")){client.sendKey({quint32(ch)},false,client.contextEpoch());client.sendKey({quint32(ch)},true,client.contextEpoch());QTest::qWait(60);}QTRY_COMPARE(client.preedit(),QString("ni'hao"));
  client.setSafeInput(true);QTRY_VERIFY(client.sensitive());QTRY_VERIFY(client.candidates().isEmpty());QTRY_VERIFY(client.preedit().isEmpty());QTRY_VERIFY(target.preedit.isEmpty());QCOMPARE(target.text(),QString());
  client.setSafeInput(false);QTRY_VERIFY(!client.sensitive());QLineEdit second;focusTarget(second,client);client.setInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethod(),QString("keyboard-us"));const auto oldTarget=target.text();client.sendKey({'a'},false,client.contextEpoch());client.sendKey({'a'},true,client.contextEpoch());QTRY_COMPARE(second.text(),QString("a"));QCOMPARE(target.text(),oldTarget);QTRY_VERIFY(client.candidates().isEmpty());QTRY_VERIFY(client.preedit().isEmpty());client.setPanelVisible(false);
 }
 void sogouLeaseRestoresAndKeepsConfiguration(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_SOGOU"))QSKIP("Requires isolated Sogou NG");
  FcitxClient client;PreeditTarget target;focusTarget(target,client);client.setInputMethod("com.sogou.ime.ng.fcitx5.deepin");QTRY_COMPARE(client.inputMethod(),QString("com.sogou.ime.ng.fcitx5.deepin"));QTRY_VERIFY(client.editable());client.sendKey({0xff1b},false,client.contextEpoch());client.sendKey({0xff1b},true,client.contextEpoch());QTRY_VERIFY(sogouState("result_composition").isEmpty());
  const QString uid=sogouUid();
  auto setting=[&](){auto m=QDBusMessage::createMethodCall("com.cpis.panel","/com/cpis/panel","com.cpis.panel","AcquireEngineStat");m.setArguments({uid,"config_preedit_type"});return QDBusConnection::sessionBus().call(m).arguments().value(0).toString();};
  const auto original=setting();QCOMPARE(original,QString("0"));
  client.setPanelVisible(true);QTRY_VERIFY(client.editable());for(char ch:QByteArray("nihao")){client.sendKey({quint32(ch)},false,client.contextEpoch());client.sendKey({quint32(ch)},true,client.contextEpoch());QTest::qWait(60);}
  QTRY_COMPARE(client.preedit(),QString("ni'hao"));QTRY_VERIFY(!target.preedit.isEmpty());QCOMPARE(setting(),original);
  client.setPanelVisible(false);QTRY_VERIFY(client.editable());QTRY_VERIFY(client.candidates().isEmpty());QTRY_VERIFY(target.preedit.isEmpty());QCOMPARE(setting(),original);QVERIFY(target.text().isEmpty());
  client.setPanelVisible(true);QTRY_VERIFY(client.editable());QTRY_COMPARE(client.preedit(),QString("ni'hao"));QTRY_VERIFY(!target.preedit.isEmpty());
  client.setInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethod(),QString("keyboard-us"));QTRY_VERIFY(client.candidates().isEmpty());QTRY_VERIFY(target.preedit.isEmpty());QCOMPARE(setting(),original);client.setPanelVisible(false);
 }
 void sogouGtkTarget(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_SOGOU"))QSKIP("Requires isolated Sogou NG");
  FcitxClient client;QProcess gtk;auto cleanup=qScopeGuard([&]{gtk.terminate();gtk.waitForFinished();});gtk.start(QCoreApplication::applicationDirPath()+"/test-gtk-target",{});QVERIFY(gtk.waitForStarted());
  QTRY_VERIFY(QDBusConnection::sessionBus().interface()->isServiceRegistered("org.agino.KeyboardTestTarget1").value());QDBusInterface target("org.agino.KeyboardTestTarget1","/org/agino/TestTarget","org.agino.KeyboardTestTarget1");target.call("Focus");QTRY_VERIFY(client.editable());QTest::qWait(100);
  client.setInputMethod("com.sogou.ime.ng.fcitx5.deepin");QTRY_COMPARE(client.inputMethod(),QString("com.sogou.ime.ng.fcitx5.deepin"));client.sendKey({0xff1b},false,client.contextEpoch());client.sendKey({0xff1b},true,client.contextEpoch());QTRY_VERIFY(sogouState("result_composition").isEmpty());
  auto text=[&](const QString &method){QDBusReply<QString> r=target.call(method);return r.value();};client.setPanelVisible(true);QTRY_VERIFY(client.editable());
  for(char ch:QByteArray("nihao")){client.sendKey({quint32(ch)},false,client.contextEpoch());client.sendKey({quint32(ch)},true,client.contextEpoch());QTest::qWait(60);}QTRY_COMPARE(text("GetPreedit"),QString("ni'hao"));QVERIFY(text("GetText").isEmpty());QTRY_VERIFY(!client.candidates().isEmpty());const auto expected=client.candidates().first();
  if(qEnvironmentVariableIsSet("AGINO_TEST_SOGOU_SCREENSHOT")){QSettings prefs("agino","agino-keyboard");prefs.setValue("mode",1);prefs.setValue("theme",1);prefs.sync();QProcess panel;panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--screenshot",qEnvironmentVariable("AGINO_TEST_SOGOU_SCREENSHOT")});QVERIFY(panel.waitForStarted());QTRY_COMPARE_WITH_TIMEOUT(panel.state(),QProcess::NotRunning,5000);QCOMPARE(panel.exitCode(),0);}
  client.selectCandidate(0,client.contextEpoch(),client.candidateEpoch());QTRY_COMPARE(text("GetText"),expected);QTRY_VERIFY(text("GetPreedit").isEmpty());client.setPanelVisible(false);
 }
 void uiOwnership(){auto contender=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"contender");QVERIFY(contender.registerService("org.fcitx.Fcitx5.VirtualKeyboard"));VirtualKeyboardUi ui;QVERIFY(!ui.start());auto owner=contender.interface()->serviceOwner("org.fcitx.Fcitx5.VirtualKeyboard");QCOMPARE(owner.value(),contender.baseService());contender.unregisterService("org.fcitx.Fcitx5.VirtualKeyboard");QDBusConnection::disconnectFromBus("contender");}
 void localPolicyBarrier(){
  FcitxClient client;QLineEdit target;focusTarget(target,client);
  const auto epoch=client.contextEpoch();client.setPolicyBlocked(true);
  QVERIFY(!client.editable());QVERIFY(!client.sendKey({quint32('a'),0,0},false,epoch));
  client.refreshSnapshot();QTest::qWait(100);QVERIFY(!client.editable());QVERIFY(target.text().isEmpty());
  client.refreshSnapshot();client.setPolicyBlocked(false);QTRY_VERIFY(client.editable());
  QVERIFY(client.sendKey({quint32('a'),0,0},false,client.contextEpoch()));QVERIFY(client.sendKey({quint32('a'),0,0},true,client.contextEpoch()));
  QTRY_COMPARE(target.text(),QString("a"));
 }
 void shortcutReleaseKeepsPanelVisible(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_ALLOW_RESTART")||QGuiApplication::platformName()!="xcb"||qEnvironmentVariableIsSet("AGINO_TEST_XWAYLAND"))QSKIP("Global X11 shortcut uses a private display and bus");
  QSettings prefs("agino","agino-keyboard");const auto previousMode=prefs.value("mode"),previousShortcut=prefs.value("shortcut");
  prefs.setValue("mode",0);prefs.setValue("shortcut","Ctrl+Alt+K");prefs.sync();
  auto restore=qScopeGuard([&]{if(previousMode.isValid())prefs.setValue("mode",previousMode);else prefs.remove("mode");if(previousShortcut.isValid())prefs.setValue("shortcut",previousShortcut);else prefs.remove("shortcut");prefs.sync();});
  FcitxClient client;QLineEdit target;focusTarget(target,client);QSignalSpy physical(&client,&FcitxClient::physicalKey);
  auto display=XOpenDisplay(nullptr);QVERIFY(display);
  const auto ctrl=XKeysymToKeycode(display,XK_Control_L),alt=XKeysymToKeycode(display,XK_Alt_L),trigger=XKeysymToKeycode(display,XK_k);
  QProcess panel;auto cleanup=qScopeGuard([&]{for(auto code:{trigger,alt,ctrl})XTestFakeKeyEvent(display,code,False,CurrentTime);XSync(display,false);panel.terminate();panel.waitForFinished();XCloseDisplay(display);});
  panel.setProcessChannelMode(QProcess::ForwardedChannels);panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
  auto status=[](){QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");QDBusReply<QVariantMap> r=ui.call("Status");return r;};
  QTRY_VERIFY(status().isValid());QVERIFY(!status().value().value("visible").toBool());QVERIFY(status().value().value("automatic").toBool());QTRY_VERIFY_WITH_TIMEOUT(status().value().value("shortcutError").toString().isEmpty(),5000);
  for(auto code:{ctrl,alt,trigger})XTestFakeKeyEvent(display,code,True,CurrentTime);XSync(display,false);
  QTRY_VERIFY_WITH_TIMEOUT(status().value().value("visible").toBool(),5000);
  const auto pressCount=physical.size();
  for(auto code:{trigger,alt,ctrl})XTestFakeKeyEvent(display,code,False,CurrentTime);XSync(display,false);
  QTRY_VERIFY(physical.size()>pressCount);QTest::qWait(200);
  QVERIFY(status().value().value("visible").toBool());QVERIFY(target.hasFocus());QVERIFY(target.text().isEmpty());
  QTest::qWait(3500);QVERIFY(status().value().value("visible").toBool());QVERIFY(status().value().value("viewCreated").toBool());
  for(auto code:{ctrl,alt,trigger})XTestFakeKeyEvent(display,code,True,CurrentTime);XSync(display,false);
  QTRY_VERIFY_WITH_TIMEOUT(!status().value().value("visible").toBool(),5000);
  for(auto code:{trigger,alt,ctrl})XTestFakeKeyEvent(display,code,False,CurrentTime);XSync(display,false);QTest::qWait(200);QVERIFY(!status().value().value("visible").toBool());QVERIFY(target.hasFocus());QVERIFY(target.text().isEmpty());
  for(auto code:{ctrl,alt,trigger})XTestFakeKeyEvent(display,code,True,CurrentTime);XSync(display,false);QTRY_VERIFY(status().value().value("visible").toBool());
  for(auto code:{trigger,alt,ctrl})XTestFakeKeyEvent(display,code,False,CurrentTime);XSync(display,false);QTest::qWait(200);QVERIFY(status().value().value("visible").toBool());

 }
 void candidatesStayInPanelAndRestore(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_CANDIDATE_POPUP"))QSKIP("Popup restoration uses a controlled candidate-UI session");
  auto d=XOpenDisplay(nullptr);QVERIFY(d);auto close=qScopeGuard([&]{XCloseDisplay(d);});
  FcitxClient client;QProcess gtk;auto gtkCleanup=qScopeGuard([&]{gtk.terminate();gtk.waitForFinished();});gtk.start(QCoreApplication::applicationDirPath()+"/test-gtk-target",{});QVERIFY(gtk.waitForStarted());
  QTRY_VERIFY(QDBusConnection::sessionBus().interface()->isServiceRegistered("org.agino.KeyboardTestTarget1").value());QDBusInterface target("org.agino.KeyboardTestTarget1","/org/agino/TestTarget","org.agino.KeyboardTestTarget1");target.call("Focus");QTRY_VERIFY(client.editable());QTest::qWait(100);client.setInputMethod("pinyin");QTRY_COMPARE(client.inputMethod(),QString("pinyin"));
  for(char c:QByteArray("nihao")){const auto code=XKeysymToKeycode(d,c);XTestFakeKeyEvent(d,code,True,CurrentTime);XTestFakeKeyEvent(d,code,False,CurrentTime);XSync(d,false);QTest::qWait(50);}
  QTRY_VERIFY(!client.candidates().isEmpty());
  QTRY_VERIFY(candidatePopupVisible(d));
  QProcess panel;auto cleanup=qScopeGuard([&]{panel.terminate();panel.waitForFinished();});
  panel.setProcessChannelMode(QProcess::ForwardedChannels);panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
  auto status=[](){QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");QDBusReply<QVariantMap> r=ui.call("Status");return r;};
  QTRY_VERIFY(status().isValid());QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");
  const auto originalCandidates=client.candidates();const auto originalPreedit=client.preedit();
  ui.call("Show");QTRY_VERIFY(status().value().value("visible").toBool());QTRY_VERIFY(!candidatePopupVisible(d));QCOMPARE(client.candidates(),originalCandidates);QCOMPARE(client.preedit(),originalPreedit);
  auto contender=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"popup-contender");auto hide=QDBusMessage::createMethodCall("org.agino.Keyboard1","/org/agino/Keyboard1","org.agino.Keyboard1","SetPanelVisible");hide.setArguments({false});QVERIFY(contender.call(hide).type()!=QDBusMessage::ErrorMessage);QTest::qWait(50);QVERIFY(!candidatePopupVisible(d));QDBusConnection::disconnectFromBus("popup-contender");
  auto epoch=client.contextEpoch();client.sendKey({'n'},false,epoch);client.sendKey({'n'},true,epoch);QTest::qWait(100);QVERIFY(!candidatePopupVisible(d));QVERIFY(!client.candidates().isEmpty());
  ui.call("Hide");QTRY_VERIFY(candidatePopupVisible(d));
  ui.call("Show");QTRY_VERIFY(!candidatePopupVisible(d));
  panel.kill();QVERIFY(panel.waitForFinished());QTRY_VERIFY(candidatePopupVisible(d));QDBusReply<QString> text=target.call("GetText");QVERIFY(text.value().isEmpty());
 }
 void titleDragKeepsPositionAfterUnload(){
  if(QGuiApplication::platformName()!="xcb"||qEnvironmentVariableIsSet("AGINO_TEST_XWAYLAND"))QSKIP("X11 pointer drag fixture");
  FcitxClient client;QLineEdit target;focusTarget(target,client);
  auto d=XOpenDisplay(nullptr);QVERIFY(d);QProcess panel;
  auto cleanup=qScopeGuard([&]{XTestFakeButtonEvent(d,1,False,CurrentTime);XSync(d,false);panel.terminate();panel.waitForFinished();XCloseDisplay(d);});
  panel.setProcessChannelMode(QProcess::ForwardedChannels);panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
  auto status=[](){QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");QDBusReply<QVariantMap> r=ui.call("Status");return r;};
  QTRY_VERIFY(status().isValid());QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");ui.call("Show");
  QTRY_VERIFY(!panelRect(d,panelProcessId(panel)).isEmpty());const QRect before=panelRect(d,panelProcessId(panel));
  const QPoint start=before.topLeft()+QPoint(before.width()/2,56+(before.height()-100)/10);
  XTestFakeMotionEvent(d,-1,start.x(),start.y(),CurrentTime);XTestFakeButtonEvent(d,1,True,CurrentTime);XSync(d,false);QTest::qWait(50);
  XTestFakeMotionEvent(d,-1,start.x()-60,start.y()-100,CurrentTime);XSync(d,false);QTest::qWait(100);
  XTestFakeButtonEvent(d,1,False,CurrentTime);XSync(d,false);
  QTRY_COMPARE(panelRect(d,panelProcessId(panel)).topLeft(),before.topLeft()+QPoint(-60,-100));const QRect moved=panelRect(d,panelProcessId(panel));
  QVERIFY(target.text().isEmpty());QVERIFY(target.hasFocus());QVERIFY(target.isActiveWindow());ui.call("Hide");QTRY_VERIFY_WITH_TIMEOUT(!status().value().value("viewCreated").toBool(),5000);
  ui.call("Show");QTRY_COMPARE(panelRect(d,panelProcessId(panel)),moved);QVERIFY(target.hasFocus());QVERIFY(target.isActiveWindow());
 }
 void actualPanel(){
  auto bus=QDBusConnection::sessionBus();SessionMock session;bool owns=bus.registerService("org.deepin.dde.SessionManager1");
  if(owns)QVERIFY(bus.registerObject("/org/deepin/dde/SessionManager1",&session,QDBusConnection::ExportAllProperties|QDBusConnection::ExportAllSignals));
  auto cleanup=qScopeGuard([&]{if(owns){bus.unregisterObject("/org/deepin/dde/SessionManager1");bus.unregisterService("org.deepin.dde.SessionManager1");}});
  FcitxClient client;QLineEdit target;focusTarget(target,client);QTRY_VERIFY(target.isActiveWindow());QTRY_VERIFY(client.editable());
  QProcess panel;panel.setProcessChannelMode(QProcess::ForwardedChannels);panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
  QTRY_VERIFY_WITH_TIMEOUT(QDBusConnection::sessionBus().interface()->isServiceRegistered("org.agino.KeyboardUi1").value(),5000);
  QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");
  auto visible=[&](){QDBusReply<QVariantMap> r=ui.call("Status");return r.isValid()&&r.value().value("visible").toBool();};
  QDBusReply<QVariantMap> status=ui.call("Status");QVERIFY(status.isValid());QVERIFY(status.value().contains("panelLoaded"));QVERIFY(!status.value().value("panelLoaded").toBool());QVERIFY(status.value().value("automatic").toBool());QTest::qWait(100);QVERIFY(!visible());
  QVERIFY(status.value().contains("viewCreated"));QVERIFY(!status.value().value("viewCreated").toBool());
  QElapsedTimer firstShow;firstShow.start();ui.call("Show");QTRY_VERIFY(visible());qInfo()<<"first show milliseconds"<<firstShow.elapsed();ui.call("Hide");QTRY_VERIFY(!visible());
  for(int n=0;n<100;++n){ui.call("Show");QTRY_VERIFY(visible());ui.call("Hide");QTRY_VERIFY(!visible());}
  ui.call("Show");QTRY_VERIFY(visible());if(owns){session.notifyUnlocked();QTest::qWait(100);QVERIFY(visible());}QVERIFY(target.hasFocus());QVERIFY(target.isActiveWindow());QVERIFY(client.editable());
  client.setInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethod(),QString("keyboard-us"));QTest::qWait(100);auto e=client.contextEpoch();client.sendKey({'z'},false,e);client.sendKey({'z'},true,e);QTRY_COMPARE(target.text(),QString("z"));
  panel.terminate();QVERIFY(panel.waitForFinished());
 }
 void densityModeUsesFixedSizeAndSurvivesReopen(){
  QSettings prefs("agino","agino-keyboard");const QStringList names{"compact","functionRow","heightPercent","opacityPercent","keySound"};QVariantMap old;for(const auto &name:names)old[name]=prefs.value(name);
  prefs.setValue("compact",false);prefs.setValue("functionRow",false);prefs.setValue("heightPercent",70);prefs.setValue("opacityPercent",100);prefs.setValue("keySound",false);prefs.sync();
  QProcess panel;auto cleanup=qScopeGuard([&]{panel.terminate();panel.waitForFinished();for(const auto &name:names){if(old[name].isValid())prefs.setValue(name,old[name]);else prefs.remove(name);}prefs.sync();});
  FcitxClient client;QLineEdit target;focusTarget(target,client);
  panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
  QTRY_VERIFY_WITH_TIMEOUT(QDBusConnection::sessionBus().interface()->isServiceRegistered("org.agino.KeyboardUi1").value(),5000);
  QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");auto status=[&](){QDBusReply<QVariantMap> r=ui.call("Status");return r.value();};
  ui.call("Show");QTRY_VERIFY(status().value("visible").toBool());QTRY_COMPARE(status().value("panelHeight").toInt(),344);
  auto width=status().value("panelWidth").toInt();QVERIFY(width>=828&&width<=934);
  prefs.setValue("compact",true);prefs.sync();ui.call("ReloadPreferences");QTRY_COMPARE(status().value("panelWidth").toInt(),524);QTRY_COMPARE(status().value("panelHeight").toInt(),208);QVERIFY(target.hasFocus());
  ui.call("Settings");QTRY_VERIFY(status().value("settingsVisible").toBool());QCOMPARE(status().value("panelHeight").toInt(),208);QCOMPARE(status().value("panelWidth").toInt(),524);
  ui.call("Hide");QTRY_VERIFY_WITH_TIMEOUT(!status().value("viewCreated").toBool(),5000);ui.call("Show");QTRY_COMPARE(status().value("panelHeight").toInt(),208);QCOMPARE(status().value("panelWidth").toInt(),524);
  prefs.setValue("functionRow",true);prefs.sync();ui.call("ReloadPreferences");QTRY_COMPARE(status().value("panelHeight").toInt(),242);
  prefs.setValue("compact",false);prefs.sync();ui.call("ReloadPreferences");QTRY_COMPARE(status().value("panelHeight").toInt(),400);QCOMPARE(status().value("panelWidth").toInt(),width);
  client.setInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethod(),QString("keyboard-us"));QTest::qWait(100);auto epoch=client.contextEpoch();client.sendKey({'q'},false,epoch);client.sendKey({'q'},true,epoch);QTRY_COMPARE(target.text(),QString("q"));QVERIFY(target.hasFocus());
 }
 void waylandDockReservesSpaceWithoutTakingFocus(){
  if(!QGuiApplication::platformName().startsWith("wayland"))QSKIP("Native Wayland dock behavior");
  QSettings prefs("agino","agino-keyboard");const auto oldFn=prefs.value("functionRow"),oldHeight=prefs.value("compact");
  prefs.setValue("functionRow",false);prefs.setValue("compact",false);prefs.sync();
  QProcess panel;auto cleanup=qScopeGuard([&]{panel.terminate();panel.waitForFinished();if(QTest::currentTestFailed()){QFile log("/tmp/agino-dock-trace.log");if(log.open(QIODevice::WriteOnly))log.write(panel.readAll());}
   if(oldFn.isValid())prefs.setValue("functionRow",oldFn);else prefs.remove("functionRow");
   if(oldHeight.isValid())prefs.setValue("compact",oldHeight);else prefs.remove("compact");prefs.sync();});
  FcitxClient client;QLineEdit target;target.setWindowFlag(Qt::FramelessWindowHint);focusTarget(target,client);target.showMaximized();
  QTRY_VERIFY(target.isMaximized());QTest::qWait(200);const int fullHeight=target.frameGeometry().height();QVERIFY(fullHeight>600);
  auto env=QProcessEnvironment::systemEnvironment();env.insert("WAYLAND_DEBUG","client");
  panel.setProcessEnvironment(env);panel.setProcessChannelMode(QProcess::MergedChannels);
  panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
  QTRY_VERIFY_WITH_TIMEOUT(QDBusConnection::sessionBus().interface()->isServiceRegistered("org.agino.KeyboardUi1").value(),5000);
  QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");
  auto status=[&](){QDBusReply<QVariantMap> r=ui.call("Status");return r.value();};
  ui.call("Show");QTRY_VERIFY(status().value("visible").toBool());
  QTRY_VERIFY(target.frameGeometry().height()<fullHeight-250);QTest::qWait(250);const int dockHeight=target.frameGeometry().height(),dockPanelHeight=status().value("panelHeight").toInt();
  QVERIFY(target.hasFocus());QVERIFY(target.isActiveWindow());
  prefs.setValue("functionRow",true);prefs.sync();ui.call("ReloadPreferences");
  QTRY_COMPARE(status().value("panelHeight").toInt(),dockPanelHeight+56);
  QTRY_VERIFY(target.frameGeometry().height()<dockHeight);QVERIFY(target.hasFocus());
  prefs.setValue("functionRow",false);prefs.sync();ui.call("ReloadPreferences");QTRY_COMPARE(target.frameGeometry().height(),dockHeight);
  for(int n=0;n<2;++n){
   ui.call("Hide");QTRY_COMPARE(target.frameGeometry().height(),fullHeight);
   QTRY_VERIFY_WITH_TIMEOUT(!status().value("viewCreated").toBool(),5000);
   ui.call("Show");QTRY_COMPARE(target.frameGeometry().height(),dockHeight);QVERIFY(target.hasFocus());QVERIFY(target.isActiveWindow());
  }
  ui.call("Hide");QTRY_COMPARE(target.frameGeometry().height(),fullHeight);
  panel.terminate();QVERIFY(panel.waitForFinished());const QByteArray trace=panel.readAll();
  QVERIFY(trace.contains(".set_anchor(2)"));QVERIFY(trace.contains(".set_keyboard_interactivity(0)"));
  QVERIFY(trace.contains(", 2, \"dock\")"));
  const auto ack=trace.indexOf(".ack_configure(");const auto buffer=trace.indexOf(".attach(wl_buffer");
  QVERIFY(ack>=0);QVERIFY(buffer>ack);QVERIFY(trace.contains(".set_exclusive_zone(0)"));
 }
 void waylandTitleDragIsContinuous(){
  if(!QGuiApplication::platformName().startsWith("wayland"))QSKIP("Native nested Wayland title drag");
  FcitxClient client;QLineEdit target;focusTarget(target,client);
  auto d=XOpenDisplay(nullptr);QVERIFY(d);QProcess panel;
  auto cleanup=qScopeGuard([&]{XTestFakeButtonEvent(d,1,False,CurrentTime);XSync(d,false);panel.terminate();panel.waitForFinished();XCloseDisplay(d);});
  panel.setProcessChannelMode(QProcess::ForwardedChannels);panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
  auto status=[](){QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");QDBusReply<QVariantMap> r=ui.call("Status");return r.value();};
  QTRY_VERIFY(!status().isEmpty());QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");ui.call("Show");QTRY_VERIFY(status().value("visible").toBool());
  const auto screen=qGuiApp->primaryScreen()->geometry();const auto first=status();const QPoint before((screen.width()-first.value("panelWidth").toInt())/2,screen.height()-first.value("panelHeight").toInt());
  QTRY_COMPARE(status().value("panelX").toInt(),before.x());QTRY_COMPARE(status().value("panelY").toInt(),before.y());
  Window root,parent,*children=nullptr;unsigned count=0;QPoint origin;bool found=false;
  QVERIFY(XQueryTree(d,DefaultRootWindow(d),&root,&parent,&children,&count));
  for(unsigned n=0;n<count;++n){XWindowAttributes a{};if(XGetWindowAttributes(d,children[n],&a)&&a.map_state==IsViewable&&a.width==screen.width()&&a.height==screen.height()){Window child;int x,y;XTranslateCoordinates(d,children[n],root,0,0,&x,&y,&child);origin=QPoint(x,y);found=true;break;}}
  if(children)XFree(children);QVERIFY(found);
  const QPoint press=origin+before+QPoint(first.value("panelWidth").toInt()/2,56+(first.value("panelHeight").toInt()-100)/10);
  XTestFakeMotionEvent(d,-1,press.x(),press.y(),CurrentTime);XTestFakeButtonEvent(d,1,True,CurrentTime);XSync(d,false);QTest::qWait(100);
  XTestFakeMotionEvent(d,-1,press.x()-30,press.y()-50,CurrentTime);XSync(d,false);QTest::qWait(100);
  XTestFakeMotionEvent(d,-1,press.x()-60,press.y()-100,CurrentTime);XSync(d,false);QTest::qWait(100);
  XTestFakeButtonEvent(d,1,False,CurrentTime);XSync(d,false);
  QVERIFY(status().value("visible").toBool());
  QTRY_COMPARE(status().value("panelX").toInt(),before.x()-60);QTRY_COMPARE(status().value("panelY").toInt(),before.y()-100);QVERIFY(target.text().isEmpty());QVERIFY(target.hasFocus());QVERIFY(target.isActiveWindow());
  client.setInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethod(),QString("keyboard-us"));QTest::qWait(100);
  const auto key=XKeysymToKeycode(d,XK_t);XTestFakeKeyEvent(d,key,True,CurrentTime);XTestFakeKeyEvent(d,key,False,CurrentTime);XSync(d,false);
  QTRY_COMPARE(target.text(),QString("t"));QVERIFY(status().value("visible").toBool());

 }
 void settingsUsePanelShellWithoutTakingFocus(){
  if(!QGuiApplication::platformName().startsWith("wayland"))QSKIP("Native Wayland settings shell");
  QTemporaryDir dir;QVERIFY(dir.isValid());const QString image=dir.filePath("settings.png");
  FcitxClient client;QLineEdit target;focusTarget(target,client);
  QProcess settings;auto env=QProcessEnvironment::systemEnvironment();env.insert("WAYLAND_DEBUG","client");
  settings.setProcessEnvironment(env);settings.setProcessChannelMode(QProcess::MergedChannels);
  settings.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--settings","--screenshot",image});
  QVERIFY(settings.waitForStarted());QVERIFY(settings.waitForFinished(10000));QCOMPARE(settings.exitCode(),0);
  const auto trace=settings.readAll();QVERIFY(trace.contains(".get_layer_surface("));
  QVERIFY(trace.contains(".set_keyboard_interactivity(0)"));QVERIFY(!trace.contains(".get_toplevel("));
  QImage screenshot(image);QVERIFY(!screenshot.isNull());QVERIFY(screenshot.width()>=828);
  QVERIFY(target.hasFocus());QVERIFY(target.isActiveWindow());
 }
 void currentInputMethodStatusFollowsFcitxWithoutOpeningSettings(){
  FcitxClient client;QLineEdit target;focusTarget(target,client);QTRY_VERIFY(client.inputMethods().contains("pinyin"));
  client.setInputMethod("pinyin");QTRY_COMPARE(client.inputMethod(),QString("pinyin"));
  QTRY_VERIFY(client.property("inputMethodStatus").toString().startsWith(QString::fromUtf8("中文：")));
  QVERIFY(!client.property("inputMethodStatus").toString().contains("pinyin"));
  client.setInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethod(),QString("keyboard-us"));
  QTRY_VERIFY(client.property("inputMethodStatus").toString().startsWith(QString::fromUtf8("英文：")));
  QVERIFY(!client.property("inputMethodStatus").toString().contains("keyboard-us"));
 }
 void inlineSettingsPreserveCompositionAndReuseProcess(){
  FcitxClient client;PreeditTarget target;focusTarget(target,client);QTRY_VERIFY(!client.inputMethods().isEmpty());
  client.refreshInputMethodNames();QTRY_VERIFY(client.inputMethodNames().contains("pinyin"));QVERIFY(client.inputMethodNames().value("pinyin").toString()!="pinyin");
  client.setInputMethod("pinyin");QTRY_COMPARE(client.inputMethod(),QString("pinyin"));QTest::qWait(100);
  auto e=client.contextEpoch();for(char c:QByteArray("nihao")){client.sendKey({quint32(c)},false,e);client.sendKey({quint32(c)},true,e);}
  QTRY_VERIFY(!client.candidates().isEmpty());
  QProcess panel;auto cleanup=qScopeGuard([&]{panel.terminate();panel.waitForFinished();});
  panel.setProcessChannelMode(QProcess::ForwardedChannels);panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
  auto status=[](){QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");QDBusReply<QVariantMap> r=ui.call("Status");return r.value();};
  QTRY_VERIFY(!status().isEmpty());QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");ui.call("Show");QTRY_VERIFY(status().value("visible").toBool());QTest::qWait(100);
  const auto before=client.preedit();const auto words=client.candidates();const auto caret=target.preedit;const auto epoch=client.contextEpoch();const auto geometry=status();
  ui.call("Settings");QTRY_VERIFY(status().value("settingsVisible").toBool());QVERIFY(status().value("visible").toBool());QTest::qWait(100);QCOMPARE(status().value("panelWidth"),geometry.value("panelWidth"));QCOMPARE(status().value("panelHeight"),geometry.value("panelHeight"));
  QCOMPARE(client.preedit(),before);QCOMPARE(client.candidates(),words);QCOMPARE(client.contextEpoch(),epoch);QCOMPARE(target.preedit,caret);QVERIFY(target.hasFocus());QVERIFY(target.text().isEmpty());
  QProcess entry;entry.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--settings"});QVERIFY(entry.waitForFinished(5000));QCOMPARE(entry.exitCode(),0);QVERIFY(panel.state()==QProcess::Running);QVERIFY(status().value("settingsVisible").toBool());
  QVERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered("org.agino.KeyboardSettings1").value());
  if(QGuiApplication::platformName()=="xcb"&&!qEnvironmentVariableIsSet("AGINO_TEST_XWAYLAND")){
   auto d=XOpenDisplay(nullptr);QVERIFY(d);auto close=qScopeGuard([&]{XCloseDisplay(d);});QTRY_VERIFY(!panelRect(d,panelProcessId(panel)).isEmpty());auto r=panelRect(d,panelProcessId(panel));
   const auto point=r.topLeft()+QPoint(r.width()-80,24);XTestFakeMotionEvent(d,-1,point.x(),point.y(),CurrentTime);XTestFakeButtonEvent(d,1,True,CurrentTime);XTestFakeButtonEvent(d,1,False,CurrentTime);XSync(d,false);
   QTRY_VERIFY(!status().value("settingsVisible").toBool());QVERIFY(status().value("visible").toBool());QCOMPARE(client.preedit(),before);QCOMPARE(client.candidates(),words);QCOMPARE(target.preedit,caret);QVERIFY(target.hasFocus());
  }
  ui.call("Hide");QTRY_VERIFY(!status().value("settingsVisible").toBool());QTRY_VERIFY_WITH_TIMEOUT(!status().value("viewCreated").toBool(),5000);
  ui.call("Show");QTRY_VERIFY(status().value("visible").toBool());QVERIFY(!status().value("settingsVisible").toBool());
 }
 void waylandOutputSwitchMovesReservation(){
  if(!QGuiApplication::platformName().startsWith("wayland")||qGuiApp->screens().size()!=2)QSKIP("Two-output native Wayland session");
  QSettings prefs("agino","agino-keyboard");const auto oldScreen=prefs.value("screenIndex");
  prefs.setValue("screenIndex",0);prefs.sync();
  QProcess panel;auto cleanup=qScopeGuard([&]{panel.terminate();panel.waitForFinished();
   if(oldScreen.isValid())prefs.setValue("screenIndex",oldScreen);else prefs.remove("screenIndex");prefs.sync();});
  FcitxClient client;QLineEdit target;target.setWindowFlag(Qt::FramelessWindowHint);focusTarget(target,client);target.showMaximized();
  QTRY_VERIFY(target.isMaximized());QTest::qWait(200);const int fullHeight=target.frameGeometry().height();
  panel.setProcessChannelMode(QProcess::ForwardedChannels);
  panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
  QTRY_VERIFY_WITH_TIMEOUT(QDBusConnection::sessionBus().interface()->isServiceRegistered("org.agino.KeyboardUi1").value(),5000);
  QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");
  auto visible=[&](){QDBusReply<QVariantMap> r=ui.call("Status");return r.value().value("visible").toBool();};
  ui.call("Show");QTRY_VERIFY(visible());QTRY_VERIFY(target.frameGeometry().height()<fullHeight-250);QTest::qWait(250);const int dockHeight=target.frameGeometry().height();
  prefs.setValue("screenIndex",1);prefs.sync();ui.call("ReloadPreferences");
  QTRY_COMPARE(target.frameGeometry().height(),fullHeight);QVERIFY(visible());QVERIFY(target.hasFocus());
  prefs.setValue("screenIndex",0);prefs.sync();ui.call("ReloadPreferences");
  QTRY_COMPARE(target.frameGeometry().height(),dockHeight);QVERIFY(visible());QVERIFY(target.hasFocus());
  ui.call("MoveBy",-60.0,-100.0);QTRY_COMPARE(target.frameGeometry().height(),fullHeight);QVERIFY(visible());QVERIFY(target.hasFocus());
  ui.call("Hide");auto loaded=[&](){QDBusReply<QVariantMap> r=ui.call("Status");return r.value().value("viewCreated").toBool();};QTRY_VERIFY_WITH_TIMEOUT(!loaded(),5000);
  ui.call("Show");QTRY_VERIFY(visible());QTest::qWait(200);QCOMPARE(target.frameGeometry().height(),fullHeight);QVERIFY(target.hasFocus());
 }
 void renderingPolicy(){
  if(QGuiApplication::platformName()!="xcb"||qEnvironmentVariableIsSet("AGINO_TEST_XWAYLAND")||!qEnvironmentVariableIsSet("AGINO_TEST_ALLOW_RESTART"))QSKIP("Rendering overrides use a private X11 session");
  FcitxClient client;QLineEdit target;focusTarget(target,client);
  for(int mode=0;mode<3;++mode){
   auto env=QProcessEnvironment::systemEnvironment();env.remove("QT_QUICK_BACKEND");env.remove("QSG_RHI_BACKEND");
   if(mode==1)env.insert("QT_QUICK_BACKEND","rhi");
   if(mode==2)env.insert("QSG_RHI_BACKEND","opengl");
   QProcess panel;panel.setProcessEnvironment(env);panel.setProcessChannelMode(QProcess::ForwardedChannels);
   auto cleanup=qScopeGuard([&]{panel.terminate();panel.waitForFinished();});
   panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
   QTRY_VERIFY_WITH_TIMEOUT(QDBusConnection::sessionBus().interface()->isServiceRegistered("org.agino.KeyboardUi1").value(),5000);
   QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");ui.call("Show");
   auto status=[&](){QDBusReply<QVariantMap> r=ui.call("Status");return r.value();};
   QTRY_VERIFY(status().value("visible").toBool());
   // Qt Quick backend flags apply only to the retained reference frontend.
   // The native QtGui frontend always rasterizes, with no scenegraph runtime.
   const bool nativeRaster=status().value("renderer").toString()==QStringLiteral("qtgui-raster");
   const int expected=int(nativeRaster||mode==0?QSGRendererInterface::Software:QSGRendererInterface::OpenGL);
   QTRY_COMPARE(status().value("graphicsApi").toInt(),expected);
   QVERIFY(target.hasFocus());
   ui.call("Hide");QTRY_VERIFY(!status().value("visible").toBool());
   ui.call("Show");QTRY_VERIFY(status().value("visible").toBool());
   QTRY_COMPARE(status().value("graphicsApi").toInt(),expected);
   panel.terminate();QVERIFY(panel.waitForFinished());
   QTRY_VERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered("org.agino.KeyboardUi1").value());
  }
 }
 void idleUnloadRestoresInput(){
  FcitxClient client;QLineEdit target;focusTarget(target,client);
  QProcess panel;panel.setProcessChannelMode(QProcess::ForwardedChannels);
  auto cleanup=qScopeGuard([&]{panel.terminate();panel.waitForFinished();});
  panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
  QTRY_VERIFY_WITH_TIMEOUT(QDBusConnection::sessionBus().interface()->isServiceRegistered("org.agino.KeyboardUi1").value(),5000);
  QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");
  auto status=[&](){QDBusReply<QVariantMap> r=ui.call("Status");return r.value();};
  ui.call("Show");QTRY_VERIFY(status().value("visible").toBool());
  ui.call("Hide");QTest::qWait(100);ui.call("Show");QTest::qWait(3200);
  QVERIFY(status().value("visible").toBool());QVERIFY(status().value("panelLoaded").toBool());
  for(int n=0;n<2;++n){
   ui.call("Hide");QTRY_VERIFY(!status().value("visible").toBool());
   QTRY_VERIFY_WITH_TIMEOUT(!status().value("viewCreated").toBool(),5000);
   QVERIFY(!status().value("panelLoaded").toBool());
   ui.call("Show");QTRY_VERIFY(status().value("visible").toBool());
   QVERIFY(target.hasFocus());QVERIFY(target.isActiveWindow());
   client.setInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethod(),QString("keyboard-us"));QTest::qWait(50);client.refresh();QTRY_COMPARE(client.contextEpoch(),previousContext());
   const auto epoch=client.contextEpoch();client.sendKey({'r'},false,epoch);client.sendKey({'r'},true,epoch);
   QTRY_COMPARE(target.text(),QString(n+1,'r'));
  }
 }
 void firstSubmittedFrameMeasured(){
  FcitxClient client;QLineEdit target;focusTarget(target,client);
  QProcess panel;auto cleanup=qScopeGuard([&]{panel.terminate();panel.waitForFinished();});
  panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
  QTRY_VERIFY_WITH_TIMEOUT(QDBusConnection::sessionBus().interface()->isServiceRegistered("org.agino.KeyboardUi1").value(),5000);
  QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");
  auto status=[&](){QDBusReply<QVariantMap> r=ui.call("Status");return r.value();};
  for(int n=0;n<2;++n){
   ui.call("Show");QTRY_VERIFY(status().value("visible").toBool());
   QVERIFY(status().value("showSerial").toULongLong()>0);
   QTRY_COMPARE(status().value("firstFrameSerial").toULongLong(),status().value("showSerial").toULongLong());
   QVERIFY(status().value("firstFrameMs").toDouble()>=0);
   QVERIFY(target.hasFocus());ui.call("Hide");
   QTRY_VERIFY_WITH_TIMEOUT(!status().value("viewCreated").toBool(),5000);
  }
 }
 void idleReturnsUnusedHeapPages(){
  FcitxClient client;QLineEdit target;focusTarget(target,client);
  QProcess panel;auto env=QProcessEnvironment::systemEnvironment();env.insert("QT_QUICK_BACKEND","software");panel.setProcessEnvironment(env);
  auto cleanup=qScopeGuard([&]{panel.terminate();panel.waitForFinished();});
  panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
  QTRY_VERIFY_WITH_TIMEOUT(QDBusConnection::sessionBus().interface()->isServiceRegistered("org.agino.KeyboardUi1").value(),5000);
  QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");
  auto status=[&](){QDBusReply<QVariantMap> r=ui.call("Status");return r.value();};
  auto heapBytes=[&](){
   QList<qint64> pids{panel.processId()};const auto child=status().value("workerPid").toLongLong();if(child>0)pids<<child;
   qint64 rss=0;const QRegularExpression mappingHeader("^[0-9a-f]+-[0-9a-f]+ ");
   for(auto pid:pids){QFile smaps(QString("/proc/%1/smaps").arg(pid));if(!smaps.open(QIODevice::ReadOnly))return qint64(-1);bool heap=false;
    for(const auto &line:smaps.readAll().split('\n')){
     if(mappingHeader.match(QString::fromLatin1(line)).hasMatch())heap=line.endsWith("[heap]");
     else if(heap&&line.startsWith("Rss:"))rss+=line.mid(4).simplified().split(' ').first().toLongLong()*1024;
    }
   }
   return rss;
  };
  ui.call("Show");QTRY_VERIFY(status().value("visible").toBool());QTest::qWait(300);
  const auto shownHeap=heapBytes();QVERIFY(shownHeap>0);
  ui.call("Hide");QTRY_VERIFY_WITH_TIMEOUT(!status().value("viewCreated").toBool(),5000);
  QTRY_VERIFY_WITH_TIMEOUT(heapBytes()>0&&heapBytes()<shownHeap*85/100,3000);
  ui.call("Show");QTRY_VERIFY(status().value("visible").toBool());QVERIFY(target.hasFocus());
 }
 void gtkTarget(){
  FcitxClient client;QProcess gtk;gtk.start(QCoreApplication::applicationDirPath()+"/test-gtk-target",{});QVERIFY(gtk.waitForStarted());
  QTRY_VERIFY_WITH_TIMEOUT(QDBusConnection::sessionBus().interface()->isServiceRegistered("org.agino.KeyboardTestTarget1").value(),5000);
  QDBusInterface target("org.agino.KeyboardTestTarget1","/org/agino/TestTarget","org.agino.KeyboardTestTarget1");target.call("Focus");QTRY_VERIFY(client.editable());QTest::qWait(100);auto e=client.contextEpoch();client.setInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethod(),QString("keyboard-us"));
  auto text=[&](){QDBusReply<QString> r=target.call("GetText");return r.value();};auto tap=[&](uint sym,uint mods=0){client.sendKey({sym,0,mods},false,e);client.sendKey({sym,0,mods},true,e);};
  tap('a');tap('b');QTRY_COMPARE(text(),QString("ab"));QTest::qWait(100);QCOMPARE(client.contextEpoch(),e);tap(0xff51);tap(0xffff);QTRY_COMPARE(text(),QString("a"));QTest::qWait(50);QTRY_COMPARE(client.contextEpoch(),previousContext());e=client.contextEpoch();client.setInputMethod("pinyin");QTRY_COMPARE(client.inputMethod(),QString("pinyin"));QTest::qWait(50);e=client.contextEpoch();for(char c:QByteArray("nihao"))tap(uint(c));QTRY_VERIFY(!client.candidates().isEmpty());QTest::qWait(100);client.selectCandidate(0,e,client.candidateEpoch());QTRY_COMPARE(text(),QString::fromUtf8("a你好"));
  target.call("Sensitive",true);QTRY_VERIFY(client.sensitive());QTest::qWait(100);e=client.contextEpoch();tap('p');QTRY_COMPARE(text(),QString("p"));QVERIFY(client.candidates().isEmpty());gtk.terminate();QVERIFY(gtk.waitForFinished());
 }
 void savedSafeInputStartup(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_ALLOW_RESTART"))QSKIP("Preferences test uses private session");
  QSettings prefs("agino","agino-keyboard");const auto previous=prefs.value("safeInput");prefs.setValue("safeInput",true);prefs.sync();
  QProcess panel;auto cleanup=qScopeGuard([&]{panel.terminate();panel.waitForFinished();if(previous.isValid())prefs.setValue("safeInput",previous);else prefs.remove("safeInput");prefs.sync();QDBusInterface bridge("org.agino.Keyboard1","/org/agino/Keyboard1","org.agino.Keyboard1");bridge.call("SetSafeInput",false);});
  FcitxClient client;QLineEdit target;focusTarget(target,client);panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());QTRY_VERIFY(client.sensitive());QTRY_VERIFY(client.editable());QTest::qWait(100);client.refresh();QTRY_COMPARE(client.contextEpoch(),previousContext());
  client.sendKey({'n'},false,client.contextEpoch());client.sendKey({'n'},true,client.contextEpoch());QTRY_COMPARE(target.text(),QString("n"));QVERIFY(client.candidates().isEmpty());
 }
 void inputMethodChoicePersists(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_ALLOW_RESTART"))QSKIP("Preferences test uses private session");
  QSettings prefs("agino","agino-keyboard");const auto previous=prefs.value("preferredIm");prefs.setValue("preferredIm","pinyin");prefs.sync();
  QProcess panel;auto cleanup=qScopeGuard([&]{panel.terminate();panel.waitForFinished();if(previous.isValid())prefs.setValue("preferredIm",previous);else prefs.remove("preferredIm");prefs.sync();});
  FcitxClient client;QLineEdit target;focusTarget(target,client);QTRY_VERIFY(!client.sensitive());client.setInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethod(),QString("keyboard-us"));panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());QTRY_COMPARE(client.inputMethod(),QString("pinyin"));QTRY_VERIFY(client.editable());
  client.setInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethod(),QString("keyboard-us"));QTest::qWait(200);auto e=client.contextEpoch();client.sendKey({'a'},false,e);client.sendKey({'a'},true,e);QTRY_COMPARE(target.text(),QString("a"));QTest::qWait(200);QCOMPARE(client.inputMethod(),QString("keyboard-us"));
 }
 void senderDisconnectReleasesKeys(){
  FcitxClient client;KeyTarget target;focusTarget(target,client);QTRY_VERIFY(client.editable());const auto e=client.contextEpoch();
  {auto peer=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"press-peer");auto msg=QDBusMessage::createMethodCall("org.agino.Keyboard1","/org/agino/Keyboard1","org.agino.Keyboard1","SendKey");msg.setArguments({uint(0xffe3),uint(0),uint(0),false,QVariant::fromValue(e)});QDBusReply<bool> accepted=peer.call(msg);QVERIFY(accepted.isValid()&&accepted.value());}
  QDBusConnection::disconnectFromBus("press-peer");QTRY_VERIFY(!target.releases.isEmpty());
  QDBusInterface bridge("org.agino.Keyboard1","/org/agino/Keyboard1","org.agino.Keyboard1");QDBusReply<bool> late=bridge.call("SendKey",uint(0xffe3),uint(0),uint(0),true,QVariant::fromValue(e));QVERIFY(late.isValid());QVERIFY(!late.value());
 }
 void modifierReleaseOrder(){
  if(!QGuiApplication::platformName().startsWith("wayland"))QSKIP("Native Wayland modifier state");
  FcitxClient client;KeyTarget target;focusTarget(target,client);QGuiApplication::inputMethod()->commit();QGuiApplication::sync();QTest::qWait(100);client.refresh();QTRY_VERIFY(client.editable());QTRY_VERIFY(!client.sensitive());client.setInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethod(),QString("keyboard-us"));QTest::qWait(100);auto e=client.contextEpoch();
  client.sendKey({0xffe3,0,0},false,e);client.sendKey({0xffe9,0,4},false,e);client.sendKey({0xffe3,0,0},true,e);client.sendKey({0xffe9,0,4},true,e);
  QTRY_VERIFY(target.releases.size()>=2);QVERIFY(target.releases[0].second.testFlag(Qt::AltModifier));QVERIFY(!target.releases[1].second.testFlag(Qt::ControlModifier));
  target.setText("ab");target.setCursorPosition(0);QTest::qWait(100);client.refresh();QTRY_COMPARE(client.contextEpoch(),previousContext());e=client.contextEpoch();target.releases.clear();client.sendKey({0xffe1},false,e);client.sendKey({0xffe2,0,1},false,e);client.sendKey({0xffe1},true,e);client.sendKey({0xff53},false,e);client.sendKey({0xff53},true,e);QTRY_COMPARE(target.selectedText(),QString("a"));client.sendKey({0xffe2,0,1},true,e);client.sendKey({0xff53},false,e);client.sendKey({0xff53},true,e);QTRY_VERIFY(target.selectedText().isEmpty());
  target.releases.clear();client.sendKey({0xffe9},false,e);client.sendKey({0xffe3,0,8},false,e);client.sendKey({0xffe9},true,e);client.sendKey({0xffe3,0,8},true,e);QTRY_COMPARE(target.releases.size(),2);QVERIFY(target.releases[0].second.testFlag(Qt::ControlModifier));QVERIFY(!target.releases[1].second.testFlag(Qt::AltModifier));
  target.clear();QTest::qWait(100);client.refresh();QTRY_COMPARE(client.contextEpoch(),previousContext());e=client.contextEpoch();client.sendKey({'a'},false,e);client.sendKey({'a'},true,e);QTRY_COMPARE(target.text(),QString("a"));
 }
 void panelPreeditStaysAtCaret(){
  FcitxClient client;PreeditTarget target;focusTarget(target,client);client.setPanelVisible(true);QTest::qWait(100);
  client.setInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethod(),QString("keyboard-us"));QTest::qWait(50);
  auto tap=[&](quint32 sym){auto e=client.contextEpoch();client.sendKey({sym},false,e);client.sendKey({sym},true,e);};
  tap('a');QTRY_COMPARE(target.text(),QString("a"));QVERIFY(target.preedit.isEmpty());
  client.setInputMethod("pinyin");QTRY_COMPARE(client.inputMethod(),QString("pinyin"));QTest::qWait(50);
  for(char c:QByteArray("nihao"))tap(c);
  QTRY_VERIFY(!client.candidates().isEmpty());QTRY_VERIFY(target.preedit.contains("ni"));QTRY_VERIFY(target.preedit.contains("hao"));QCOMPARE(target.text(),QString("a"));
  if(qEnvironmentVariableIsSet("AGINO_TEST_PREEDIT_DISABLED")){client.setPanelVisible(false);QTRY_VERIFY(target.preedit.isEmpty());QVERIFY(!client.candidates().isEmpty());client.setPanelVisible(true);QTRY_VERIFY(target.preedit.contains("hao"));}
  client.selectCandidate(0,client.contextEpoch(),client.candidateEpoch());QTRY_COMPARE(target.text(),QString::fromUtf8("a你好"));QTRY_VERIFY(target.preedit.isEmpty());QTRY_VERIFY(client.candidates().isEmpty());client.setPanelVisible(false);
 }
 void specialKeys(){
  FcitxClient client;QLineEdit target;focusTarget(target,client);QTRY_VERIFY(target.isActiveWindow());QTRY_VERIFY(client.editable());QGuiApplication::inputMethod()->commit();QGuiApplication::sync();QTest::qWait(100);client.refresh();QTRY_COMPARE(client.contextEpoch(),previousContext());QTRY_VERIFY(client.editable());QTRY_VERIFY(!client.sensitive());client.setInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethod(),QString("keyboard-us"));QTest::qWait(100);auto e=client.contextEpoch();
  auto tap=[&](quint32 sym,quint32 mods=0){client.sendKey({sym,0,mods},false,e);client.sendKey({sym,0,mods},true,e);};
  tap('a');tap('b');tap('c');QTRY_COMPARE(target.text(),QString("abc"));tap(0xff51);QTRY_COMPARE(target.cursorPosition(),2);tap(0xffff);QTRY_COMPARE(target.text(),QString("ab"));
  client.sendKey({0xffe3},false,e);tap('a',4);client.sendKey({0xffe3},true,e);QTRY_COMPARE(target.selectedText(),QString("ab"));tap(0xff08);QTRY_COMPARE(target.text(),QString());
  client.sendKey({0xffe1},false,e);tap('A',1);client.sendKey({0xffe1},true,e);QTRY_COMPARE(target.text(),QString("A"));QSignalSpy entered(&target,&QLineEdit::returnPressed);tap(0xff0d);QTRY_COMPARE(entered.size(),1);QTest::qWait(30);QCOMPARE(client.inputMethod(),QString("keyboard-us"));e=client.contextEpoch();tap('a');QTRY_COMPARE(target.text(),QString("Aa"));
 }
 void actualTargetAndStaleGuards(){
  FcitxClient client;
  QLineEdit target;focusTarget(target,client);
  QTRY_VERIFY_WITH_TIMEOUT(target.isActiveWindow()&&target.hasFocus(),5000);
  QTRY_VERIFY_WITH_TIMEOUT(client.connected(),5000);
  qInfo()<<"target platform"<<QGuiApplication::platformName()<<"active"<<target.isActiveWindow()<<"focus"<<target.hasFocus();
  QTRY_VERIFY_WITH_TIMEOUT(client.editable(),5000);
  QDBusInterface bridge("org.agino.Keyboard1","/org/agino/Keyboard1","org.agino.Keyboard1");
  QVERIFY(client.contextEpoch()<(quint64(1)<<53));
  client.setInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethod(),QString("keyboard-us"));QTest::qWait(100);auto e=client.contextEpoch();
  qInfo()<<"key context"<<e<<"authoritative"<<previousContext()<<"sensitive"<<client.sensitive();
  client.sendKey({'a'},false,e);client.sendKey({'a'},true,e);QTest::qWait(250);qInfo()<<"preedit length"<<client.preedit().size()<<"candidates"<<client.candidates().size()<<"im"<<client.inputMethod();QTRY_COMPARE(target.text(),QString("a"));
  QDBusReply<bool> rejected=bridge.call("SendKey",uint('b'),uint(0),uint(0),false,QVariant::fromValue(e-1));QVERIFY(rejected.isValid());QVERIFY(!rejected.value());
  client.sendKey({'b'},false,e-1);client.sendKey({'b'},true,e-1);QTest::qWait(80);QCOMPARE(target.text(),QString("a"));
  client.selectCandidate(0,e,client.candidateEpoch());QTest::qWait(50);QCOMPARE(target.text(),QString("a"));
  client.setInputMethod("pinyin");QTRY_COMPARE(client.inputMethod(),QString("pinyin"));
  for(char c:QByteArray("nihao")){client.sendKey({quint32(c)},false,e);client.sendKey({quint32(c)},true,e);}
  QTRY_VERIFY_WITH_TIMEOUT(!client.candidates().isEmpty(),4000);QVERIFY(!client.preedit().isEmpty());
  QTest::qWait(100);auto old=client.candidateEpoch();
  rejected=bridge.call("SelectCandidate",0,QVariant::fromValue(e),QVariant::fromValue(old-1));QVERIFY(rejected.isValid());QVERIFY(!rejected.value());client.selectCandidate(0,e,old-1);QTest::qWait(60);QCOMPARE(target.text(),QString("a"));
  client.selectCandidate(0,e,old);QTRY_COMPARE(target.text(),QString::fromUtf8("a你好"));
  target.setEchoMode(QLineEdit::Password);target.clear();target.clearFocus();target.setFocus();QTRY_VERIFY(client.sensitive());
  QTest::qWait(100);e=client.contextEpoch();client.sendKey({'n'},false,e);client.sendKey({'n'},true,e);QTRY_COMPARE(target.text(),QString("n"));QVERIFY(client.candidates().isEmpty());
  target.hide();QTRY_VERIFY(client.contextEpoch()!=e);if(qEnvironmentVariableIsSet("AGINO_TEST_ALLOW_RESTART"))QTRY_VERIFY(!client.editable());client.sendKey({'x'},false,e);client.sendKey({'x'},true,e);QTest::qWait(30);QCOMPARE(target.text(),QString("n"));
  rejected=bridge.call("SendKey",uint('x'),uint(0),uint(0),false,QVariant::fromValue(e));QVERIFY(rejected.isValid());QVERIFY(!rejected.value());
 }
 void focusSwitchRejectsOldKeys(){
  FcitxClient client;QLineEdit first,second;focusTarget(first,client);QTRY_VERIFY(client.editable());QTest::qWait(50);auto old=client.contextEpoch();
  QDBusInterface bridge("org.agino.Keyboard1","/org/agino/Keyboard1","org.agino.Keyboard1");
  for(int n=0;n<20;++n){
   auto &next=n%2?first:second;focusTarget(next,client);QTRY_VERIFY(client.editable());QTRY_VERIFY(client.contextEpoch()!=old);QTRY_COMPARE(client.contextEpoch(),previousContext());
   QDBusReply<bool> rejected=bridge.call("SendKey",uint('q'),uint(0),uint(0),false,QVariant::fromValue(old));QVERIFY(rejected.isValid());QVERIFY(!rejected.value());
   client.setInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethod(),QString("keyboard-us"));QTest::qWait(20);auto epoch=client.contextEpoch();const auto before=next.text();client.sendKey({'x'},false,epoch);client.sendKey({'x'},true,epoch);QTRY_COMPARE(next.text(),before+"x");old=epoch;
  }
  QCOMPARE(first.text(),QString(10,'x'));QCOMPARE(second.text(),QString(10,'x'));
 }
 void redundantResumeKeepsFocus(){
  FcitxClient client;QLineEdit target;focusTarget(target,client);QTRY_VERIFY(client.editable());QTest::qWait(50);const auto epoch=client.contextEpoch();
  QDBusInterface bridge("org.agino.Keyboard1","/org/agino/Keyboard1","org.agino.Keyboard1");QVERIFY(bridge.call("Suspend",false).type()!=QDBusMessage::ErrorMessage);client.refresh();QTest::qWait(50);QVERIFY(client.editable());QCOMPARE(client.contextEpoch(),epoch);
 }
 void suspendCycleRestoresCurrentFocus(){
  FcitxClient client;QLineEdit target;focusTarget(target,client);QTRY_VERIFY(client.editable());QTest::qWait(50);const auto old=client.contextEpoch();
  QDBusInterface bridge("org.agino.Keyboard1","/org/agino/Keyboard1","org.agino.Keyboard1");bridge.call("Suspend",true);QTRY_VERIFY(!client.editable());QVERIFY(client.candidates().isEmpty());
  bridge.call("Suspend",false);QTRY_VERIFY(client.editable());QVERIFY(client.contextEpoch()!=old);QVERIFY(client.candidates().isEmpty());
  QDBusReply<bool> rejected=bridge.call("SendKey",uint('q'),uint(0),uint(0),false,QVariant::fromValue(old));QVERIFY(rejected.isValid());QVERIFY(!rejected.value());QCOMPARE(target.text(),QString());
 }
 void x11FocusGroupRegistration(){
  if(QGuiApplication::platformName()!="xcb"||!qEnvironmentVariableIsSet("AGINO_TEST_ALLOW_RESTART"))QSKIP("Synthetic registration uses only a private X11 backend");
  QList<StringPair> options{{"program","registration-fixture"},{"display","x11:"}};
  
  // The raw reply also contains the 16-byte identifier used by the real Qt frontend.
  auto request=QDBusMessage::createMethodCall("org.fcitx.Fcitx5","/org/freedesktop/portal/inputmethod","org.fcitx.Fcitx.InputMethod1","CreateInputContext");request.setArguments({QVariant::fromValue(options)});request.setAutoStartService(false);auto reply=QDBusConnection::sessionBus().call(request);QVERIFY(reply.arguments().size()==2);
  QDBusInterface context("org.fcitx.Fcitx5",qdbus_cast<QDBusObjectPath>(reply.arguments()[0]).path(),"org.fcitx.Fcitx.InputContext1");auto cleanup=qScopeGuard([&]{context.call("DestroyIC");});
  const auto uuid=reply.arguments()[1].toByteArray();QCOMPARE(uuid.size(),16);QVERIFY(context.call("FocusIn").type()!=QDBusMessage::ErrorMessage);FcitxClient client;QTRY_VERIFY(client.editable());
  auto *display=XOpenDisplay(nullptr);QVERIFY(display);auto atom=XInternAtom(display,"_FCITX_SERVER",false);auto owner=XGetSelectionOwner(display,atom);QVERIFY(owner);XEvent event{};event.xclient.type=ClientMessage;event.xclient.window=owner;event.xclient.message_type=atom;event.xclient.format=8;memcpy(event.xclient.data.b,uuid.constData(),16);XSendEvent(display,owner,false,NoEventMask,&event);XSync(display,false);XCloseDisplay(display);QTest::qWait(100);client.refresh();QTest::qWait(50);QVERIFY(client.editable());
 }
 void hotReplacement(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_HOTREPLACE")||QGuiApplication::platformName().startsWith("wayland")||qEnvironmentVariableIsSet("AGINO_TEST_XWAYLAND"))QSKIP("Hot replacement uses a private X11 backend");
  FcitxClient client;QLineEdit target;focusTarget(target,client);QTRY_VERIFY(client.editable());
  QProcess replacement;replacement.setProcessChannelMode(QProcess::ForwardedChannels);replacement.start("agino-keyboard-fcitx-x11",{"--replace","--keep"});QVERIFY(replacement.waitForStarted());
  QTRY_COMPARE_WITH_TIMEOUT(QDBusConnection::sessionBus().interface()->servicePid("org.agino.Keyboard1").value(),uint(replacement.processId()),10000);
  replacement.terminate();QVERIFY(replacement.waitForFinished());QCOMPARE(replacement.exitStatus(),QProcess::NormalExit);QCOMPARE(replacement.exitCode(),0);

 }
 void backendRestart(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_ALLOW_RESTART")||!qEnvironmentVariableIsSet("AGINO_TEST_RESTART_CASE"))QSKIP("Backend restart is restricted to private test sessions");
  QSettings prefs("agino","agino-keyboard");const auto previous=prefs.value("safeInput");prefs.setValue("safeInput",true);prefs.sync();
  QProcess panel;auto cleanup=qScopeGuard([&]{panel.terminate();panel.waitForFinished();if(previous.isValid())prefs.setValue("safeInput",previous);else prefs.remove("safeInput");prefs.sync();});
  panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
  FcitxClient client;QLineEdit target;focusTarget(target,client);QTRY_VERIFY(target.isActiveWindow());QTRY_VERIFY(client.editable());QTRY_VERIFY(client.sensitive());QTest::qWait(100);const auto old=client.contextEpoch();
  QDBusReply<uint> pid=QDBusConnection::sessionBus().interface()->servicePid("org.fcitx.Fcitx5");QVERIFY(pid.isValid());QVERIFY(::kill(pid.value(),SIGKILL)==0);QTRY_VERIFY(!client.connected());QVERIFY(client.candidates().isEmpty());
  QProcess replacement;if(!QGuiApplication::platformName().startsWith("wayland")&&!qEnvironmentVariableIsSet("AGINO_TEST_XWAYLAND")){replacement.start("agino-keyboard-fcitx-x11",{"--keep"});QVERIFY(replacement.waitForStarted());}
  QTRY_VERIFY_WITH_TIMEOUT(client.connected(),10000);QTest::qWait(300);QLineEdit recovered;focusTarget(recovered,client);QTRY_VERIFY_WITH_TIMEOUT(client.editable(),10000);QVERIFY(client.contextEpoch()!=old);QTRY_VERIFY(client.sensitive());QTest::qWait(100);client.refresh();QTRY_COMPARE(client.contextEpoch(),previousContext());
  client.sendKey({'x'},false,old);client.sendKey({'x'},true,old);QTest::qWait(100);QCOMPARE(target.text(),QString());QVERIFY(client.candidates().isEmpty());auto epoch=client.contextEpoch();client.sendKey({'r'},false,epoch);client.sendKey({'r'},true,epoch);QTRY_COMPARE(recovered.text(),QString("r"));focusTarget(target,client);QTRY_VERIFY(client.editable());QTest::qWait(50);epoch=client.contextEpoch();client.sendKey({'g'},false,epoch);client.sendKey({'g'},true,epoch);QTRY_COMPARE(target.text(),QString("g"));
  if(replacement.state()!=QProcess::NotRunning){replacement.terminate();QVERIFY(replacement.waitForFinished());}
 }
};
QTEST_MAIN(FcitxTest)
#include "test_fcitx.moc"
