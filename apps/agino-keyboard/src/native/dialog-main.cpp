// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include "../i18n.h"
#include "../startupdpi.h"
#include <QMessageBox>
int main(int argc,char **argv){
 StartupDpiPolicy dpi;QApplication app(argc,argv);dpi.restore();AginoI18n::install(app);app.setOrganizationName("agino");app.setApplicationName("agino-keyboard");app.setApplicationDisplayName(QCoreApplication::translate("Keyboard","Screen Keyboard"));
 const bool information=argc>1&&QByteArray(argv[1])=="--information-message";
 const QString text=argc>2?QString::fromLocal8Bit(argv[2]):QString();
 if(information)QMessageBox::information(nullptr,QCoreApplication::translate("Keyboard","Screen Keyboard"),text);else QMessageBox::warning(nullptr,QCoreApplication::translate("Keyboard","Screen Keyboard"),text);
 return information?0:2;
}
