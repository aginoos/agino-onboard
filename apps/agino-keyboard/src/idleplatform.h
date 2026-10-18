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
