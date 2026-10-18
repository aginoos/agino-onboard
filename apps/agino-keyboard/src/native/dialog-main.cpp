// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include "../startupdpi.h"
#include <QMessageBox>
int main(int argc,char **argv){
 StartupDpiPolicy dpi;QApplication app(argc,argv);dpi.restore();app.setOrganizationName("agino");app.setApplicationName("agino-keyboard");app.setApplicationDisplayName(QStringLiteral("屏幕键盘"));
 const bool information=argc>1&&QByteArray(argv[1])=="--information-message";
 const QString text=argc>2?QString::fromLocal8Bit(argv[2]):QString();
 if(information)QMessageBox::information(nullptr,QStringLiteral("屏幕键盘"),text);else QMessageBox::warning(nullptr,QStringLiteral("屏幕键盘"),text);
 return information?0:2;
}
