#include <QtTest>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QProcess>
#include <QApplication>
#include "../src/startupdpi.h"
#include "../src/startupconfig.h"

class BackendHostedUi:public QObject {
 Q_OBJECT
 Q_CLASSINFO("D-Bus Interface","org.fcitx.Fcitx5.VirtualKeyboard1")
public slots:void ShowVirtualKeyboard(){}void HideVirtualKeyboard(){}
};
class StartupTest:public QObject {
 Q_OBJECT
private slots:
 void virtualKeyboardConfigPreservesOtherLines(){QTemporaryDir folder;const QString path=folder.filePath("virtualkeyboard.conf");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("# preserved comment\nEnableVirtualKeyboard=False\nOtherOption=custom-value\n");file.close();QVERIFY(enableVirtualKeyboardConfig(path));QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("# preserved comment\nEnableVirtualKeyboard=True\nOtherOption=custom-value\n"));file.close();const QString directory=folder.filePath("directory");QVERIFY(QDir().mkpath(directory));QVERIFY(!enableVirtualKeyboardConfig(directory));}
 void dpiPolicyIsAppliedAndEnvironmentRestored_data(){
  QTest::addColumn<QByteArray>("text");QTest::addColumn<int>("expected");
  using Policy=Qt::HighDpiScaleFactorRoundingPolicy;
  QTest::newRow("round")<<QByteArray("Round")<<int(Policy::Round);
  QTest::newRow("ceil")<<QByteArray("Ceil")<<int(Policy::Ceil);
  QTest::newRow("floor")<<QByteArray("Floor")<<int(Policy::Floor);
  QTest::newRow("prefer-floor")<<QByteArray("RoundPreferFloor")<<int(Policy::RoundPreferFloor);
  QTest::newRow("pass-through")<<QByteArray("PassThrough")<<int(Policy::PassThrough);
  QTest::newRow("case-insensitive")<<QByteArray("pAsStHrOuGh")<<int(Policy::PassThrough);
  QTest::newRow("invalid")<<QByteArray("invalid-policy")<<int(Policy::PassThrough);
 }
 void dpiPolicyIsAppliedAndEnvironmentRestored(){
  QFETCH(QByteArray,text);QFETCH(int,expected);QProcess probe;
  auto env=QProcessEnvironment::systemEnvironment();env.insert("QT_QPA_PLATFORM","dxcb;xcb");env.insert("QT_SCALE_FACTOR_ROUNDING_POLICY",QString::fromLatin1(text));probe.setProcessEnvironment(env);
  probe.start(QCoreApplication::applicationFilePath(),{"--dpi-probe"});QVERIFY(probe.waitForFinished());QCOMPARE(probe.exitCode(),0);
  QCOMPARE(probe.readAllStandardOutput().trimmed(),QByteArray::number(expected)+":"+text);
  const auto errors=probe.readAllStandardError();QVERIFY2(!errors.contains("setHighDpiScaleFactorRoundingPolicy must be called before"),errors.constData());
  if(text=="invalid-policy")QVERIFY(errors.contains("Unknown scale factor rounding policy"));
 }
 void backendHostedUiDoesNotBlockStartupOrChangeItsConfig(){
  auto backend=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"backend-fixture");
  auto busCleanup=qScopeGuard([&]{QDBusConnection::disconnectFromBus("backend-fixture");});
  QVERIFY(backend.registerService("org.fcitx.Fcitx5"));QVERIFY(backend.registerService("org.fcitx.Fcitx5.VirtualKeyboard"));
  BackendHostedUi existing;QVERIFY(backend.registerObject("/org/fcitx/virtualkeyboard/impanel",&existing,QDBusConnection::ExportAllSlots));
  QTemporaryDir config;QVERIFY(config.isValid());QDir().mkpath(config.filePath("fcitx5/conf"));
  QFile preferences(config.filePath("fcitx5/conf/virtualkeyboard.conf"));QVERIFY(preferences.open(QIODevice::WriteOnly));
  const QByteArray original="EnableVirtualKeyboard=False\nKeepSetting=preserved\n";preferences.write(original);preferences.close();
  QProcess panel;auto cleanup=qScopeGuard([&]{panel.terminate();panel.waitForFinished();});
  auto env=QProcessEnvironment::systemEnvironment();env.insert("XDG_CONFIG_HOME",config.path());
  env.insert("QT_QPA_PLATFORM","dxcb;xcb");env.insert("QT_SCALE_FACTOR_ROUNDING_POLICY","PassThrough");
  panel.setProcessEnvironment(env);panel.setProcessChannelMode(QProcess::MergedChannels);
  panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
  auto status=[](){QDBusInterface ui("org.agino.KeyboardUi1","/Keyboard","org.agino.KeyboardUi1");QDBusReply<QVariantMap> r=ui.call("Status");return r;};
  QTRY_VERIFY_WITH_TIMEOUT(status().isValid(),5000);
  QVERIFY(!status().value().value("visible").toBool());QVERIFY(!status().value().value("viewCreated").toBool());
  auto owner=backend.interface()->serviceOwner("org.fcitx.Fcitx5.VirtualKeyboard");QVERIFY(owner.isValid());QCOMPARE(owner.value(),backend.baseService());
  QVERIFY(preferences.open(QIODevice::ReadOnly));QCOMPARE(preferences.readAll(),original);
  panel.terminate();QVERIFY(panel.waitForFinished());
 }
 void deepinPlatformStartsWithoutLateDpiPolicyWarning(){
  QTemporaryDir config;QVERIFY(config.isValid());QProcess panel;auto cleanup=qScopeGuard([&]{panel.terminate();panel.waitForFinished();});
  auto env=QProcessEnvironment::systemEnvironment();env.insert("XDG_CONFIG_HOME",config.path());
  env.insert("QT_QPA_PLATFORM","dxcb;xcb");env.insert("QT_SCALE_FACTOR_ROUNDING_POLICY","PassThrough");
  panel.setProcessEnvironment(env);panel.setProcessChannelMode(QProcess::MergedChannels);
  panel.start(QCoreApplication::applicationDirPath()+"/agino-keyboard",{"--hidden"});QVERIFY(panel.waitForStarted());
  QTRY_VERIFY_WITH_TIMEOUT(QDBusConnection::sessionBus().interface()->isServiceRegistered("org.agino.KeyboardUi1").value(),5000);
  QTest::qWait(200);panel.terminate();QVERIFY(panel.waitForFinished());
  const auto log=panel.readAll();QVERIFY2(!log.contains("setHighDpiScaleFactorRoundingPolicy must be called before"),log.constData());
 }
};
int main(int argc,char **argv){
 if(argc==2&&QByteArray(argv[1])=="--dpi-probe"){
  StartupDpiPolicy policy;QApplication app(argc,argv);policy.restore();
  printf("%d:%s\n",int(QGuiApplication::highDpiScaleFactorRoundingPolicy()),qgetenv("QT_SCALE_FACTOR_ROUNDING_POLICY").constData());return 0;
 }
 QCoreApplication app(argc,argv);StartupTest test;return QTest::qExec(&test,argc,argv);
}
#include "test_startup.moc"
