#include <QtTest>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>

class ShellSelectionTest : public QObject {
 Q_OBJECT
private slots:
 void bundledIntegrationIsActiveBeforeMapping() {
  QWindow window;
  window.create();
  auto native=QGuiApplication::platformNativeInterface();
  QVERIFY(native);
  QVERIFY2(native->nativeResourceForWindow("agino-keyboard-shell",&window),
           "keyboard shell plugin must load before a window is mapped");
  QVERIFY(!window.isVisible());
 }
};
int main(int argc,char **argv) {
 qputenv("QT_WAYLAND_SHELL_INTEGRATION","agino-keyboard");
 QGuiApplication app(argc,argv);ShellSelectionTest test;
 return QTest::qExec(&test,argc,argv);
}
#include "test_shellselection.moc"
