// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include <memory>
class QDBusMessage;
class IdlePlatform: public QObject {
 Q_OBJECT
public:
 explicit IdlePlatform(QObject *parent=nullptr);
 ~IdlePlatform() override;
 bool wayland() const;
 bool automaticReady() const;
 QString status() const;
 QString shortcutError() const;
 void setShortcut(QString);
 static int parseShortcut(const QString &);
 // X11: false when the display cannot be opened (started again after the session ended); Wayland: true.
 static bool displayAvailable();
 // X11: end the process with STATUS when the connection to the display breaks (the session ends), instead of
 // Xlib's exit status 1, which a restart policy (Restart=on-failure) would take for a crash.
 static void exitWithDisplay(int status);
signals:
 void sourceChanged(int kind,double x,double y);
 void lockedChanged(bool);
 void capabilitiesChanged();
 void toggleRequested();
 void showRequested();
 void settingsRequested();
 void quitRequested();
private slots:
 void lock(bool,const QDBusMessage &);
 void screensaver(bool,const QDBusMessage &);
 void sleeping(bool,const QDBusMessage &);
 void deviceSource(uint,double,double,const QDBusMessage &);
 void shortcutPressed(const QString &,const QString &,qlonglong,const QDBusMessage &);
private:
 struct Private;
 std::unique_ptr<Private> d;
};
