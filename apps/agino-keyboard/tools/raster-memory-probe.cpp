// SPDX-License-Identifier: GPL-3.0-or-later
// Private architecture probe, not a functional keyboard implementation.
#include <QGuiApplication>
#include <QWindow>
#include <QBackingStore>
#include <QPainter>
#include <QTimer>
#include <DGuiApplicationHelper>
class Probe final:public QWindow {
 QBackingStore store{this};
 void paint(){if(!isExposed())return;store.resize(size());store.beginPaint(QRegion(QRect(QPoint(),size())));QPainter p(store.paintDevice());p.setRenderHint(QPainter::Antialiasing);auto palette=Dtk::Gui::DGuiApplicationHelper::instance()->applicationPalette();p.fillRect(QRect(QPoint(),size()),palette.window());QFont font=QGuiApplication::font();font.setPixelSize(16);p.setFont(font);p.setPen(palette.windowText().color());p.drawText(QRect(40,0,850,48),Qt::AlignVCenter,QString::fromUtf8("你好    拟好    你    拟    泥    逆    中文：拼音输入"));
 const QStringList rows={"Esc ` 1 2 3 4 5 6 7 8 9 0 - = Back", "Tab Q W E R T Y U I O P [ ] \\ Del", "Language A S D F G H J K L ; ' Enter", "Shift Z X C V B N M , . / Shift",QString::fromUtf8("Ctrl Super Alt 空格 Alt Ctrl Hide")};
 for(int row=0;row<rows.size();++row){auto keys=rows[row].split(' ');qreal w=(width()-24.-(keys.size()-1)*6)/keys.size();for(int i=0;i<keys.size();++i){QRectF rect(12+i*(w+6),56+row*56,w,48);p.setPen(Qt::NoPen);p.setBrush(palette.button());p.drawRoundedRect(rect,7,7);font.setPixelSize(keys[i].size()>3?14:18);p.setFont(font);p.setPen(palette.buttonText().color());p.drawText(rect,Qt::AlignCenter,keys[i]);}}
 p.end();store.endPaint();store.flush(QRegion(QRect(QPoint(),size())),this);}
 bool event(QEvent *e)override{if(e->type()==QEvent::UpdateRequest){paint();return true;}return QWindow::event(e);}
 void exposeEvent(QExposeEvent*)override{paint();}
};
int main(int argc,char **argv){QGuiApplication app(argc,argv);app.setApplicationName("agino-private-raster-probe");Probe probe;probe.setFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowDoesNotAcceptFocus);probe.resize(934,344);probe.show();QTimer::singleShot(20000,&app,&QCoreApplication::quit);return app.exec();}
