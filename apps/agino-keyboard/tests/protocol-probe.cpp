#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QTimer>
#include <QFile>
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);
 if (!QDBusConnection::sessionBus().registerService("org.fcitx.Fcitx5.VirtualKeyboard")) return 2;
 QTimer::singleShot(1500, [&] {
  QDBusInterface b("org.fcitx.Fcitx5.VirtualKeyboardBackend", "/virtualkeyboard", "org.freedesktop.DBus.Introspectable");
  QDBusReply<QString> r=b.call("Introspect");
  if (!r.isValid()) app.exit(3); else { QFile f(argv[1]); if(!f.open(QIODevice::WriteOnly)) app.exit(4); else {f.write(r.value().toUtf8());app.exit(0);} }
 });
 return app.exec();
}
