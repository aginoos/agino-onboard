#include <QtTest>
#include "autostart.h"
// The per-user autostart entry that exists only in the automatic mode.
class AutostartTest:public QObject {
 Q_OBJECT
 QTemporaryDir dir_;QString templ_,config_;
 void put(const QString &path,const QByteArray &text){QDir().mkpath(QFileInfo(path).absolutePath());QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write(text);}
private slots:
 void init(){QVERIFY(dir_.isValid());templ_=dir_.filePath("template.desktop");config_=dir_.filePath("config");
  if(QFileInfo(config_).isDir())QVERIFY(QDir(config_).removeRecursively());else if(QFile::exists(config_))QVERIFY(QFile::remove(config_));
  put(templ_,"[Desktop Entry]\nExec=agino-keyboard --hidden\nX-Agino-Autostart-Managed=true\n");}
 void createdOnlyWhenWanted(){
  QVERIFY(Autostart::sync(false,templ_,config_));QVERIFY(!QFile::exists(Autostart::entryPath(config_)));
  QVERIFY(Autostart::sync(true,templ_,config_));QVERIFY(QFile::exists(Autostart::entryPath(config_)));QVERIFY(Autostart::isOurs(Autostart::entryPath(config_)));
  QVERIFY(Autostart::sync(false,templ_,config_));QVERIFY(!QFile::exists(Autostart::entryPath(config_)));
 }
 void refreshedWhenTheTemplateChanges(){
  QVERIFY(Autostart::sync(true,templ_,config_));
  put(templ_,"[Desktop Entry]\nExec=agino-keyboard --hidden --new\nX-Agino-Autostart-Managed=true\n");
  QVERIFY(Autostart::sync(true,templ_,config_));
  QFile f(Autostart::entryPath(config_));QVERIFY(f.open(QIODevice::ReadOnly));QVERIFY(f.readAll().contains("--new"));
 }
 void identicalEntryIsNotRewritten(){
  QVERIFY(Autostart::sync(true,templ_,config_));
  const QString entry=Autostart::entryPath(config_);const QDateTime before=QDateTime::fromSecsSinceEpoch(123456789).toUTC();
  QFile file(entry);QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.setFileTime(before,QFileDevice::FileModificationTime));file.close();
  QVERIFY(Autostart::sync(true,templ_,config_));QCOMPARE(QFileInfo(entry).lastModified(),before);
 }
 void failedRefreshPreservesTheEntry(){
  QVERIFY(Autostart::sync(true,templ_,config_));const QString entry=Autostart::entryPath(config_);
  QFile old(entry);QVERIFY(old.open(QIODevice::ReadOnly));const QByteArray saved=old.readAll();old.close();
  put(templ_,"[Desktop Entry]\nExec=agino-keyboard --hidden --new\nX-Agino-Autostart-Managed=true\n");
  const QString parent=QFileInfo(entry).absolutePath();const auto permissions=QFile::permissions(parent);
  QVERIFY(QFile::setPermissions(parent,QFileDevice::ReadOwner|QFileDevice::ExeOwner));
  auto restore=qScopeGuard([&]{QFile::setPermissions(parent,permissions);});
  QFile probe(QDir(parent).filePath("probe"));
  if(probe.open(QIODevice::WriteOnly)){probe.close();QFile::remove(probe.fileName());QSKIP("Directory write permission is not enforced for this user");}
  QVERIFY(!Autostart::sync(true,templ_,config_));
  QFile after(entry);QVERIFY(after.open(QIODevice::ReadOnly));QCOMPARE(after.readAll(),saved);
 }
 void configurationPathIsAFileFails(){
  put(config_,"not a directory\n");QVERIFY(!Autostart::sync(true,templ_,config_));QVERIFY(!QFile::exists(Autostart::entryPath(config_)));
 }
 void usersOwnEntryIsKept(){
  const QString mine=Autostart::entryPath(config_);put(mine,"[Desktop Entry]\nExec=my-own-keyboard\n");
  QVERIFY(Autostart::sync(true,templ_,config_));QVERIFY(Autostart::sync(false,templ_,config_));
  QFile f(mine);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),QByteArray("[Desktop Entry]\nExec=my-own-keyboard\n"));
 }
 void missingTemplateFails(){QVERIFY(!Autostart::sync(true,dir_.filePath("none.desktop"),config_));QVERIFY(!QFile::exists(Autostart::entryPath(config_)));}
};
QTEST_APPLESS_MAIN(AutostartTest)
#include "test_autostart.moc"
