// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QProcess>
#include <QSet>
#include <memory>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

// The embedded keyboard is reparented into a non-composited login/lock screen.
// Test the actual executable and drawable, not just its offscreen render image.
class EmbeddedVisualTest final:public QObject {
 Q_OBJECT
 struct DisplayCloser {void operator()(Display *display)const{if(display)XCloseDisplay(display);}};
 std::unique_ptr<Display,DisplayCloser> display_;
 QProcess keyboard_;
 Window keyboardWindow_=0,host_=0;
 bool attributes(XWindowAttributes &value)const{return XGetWindowAttributes(display_.get(),keyboardWindow_,&value);}
 bool viewable()const{XWindowAttributes value{};return attributes(value)&&value.map_state==IsViewable;}
 bool rowsHaveContent()const{
  XWindowAttributes value{};if(!attributes(value)||value.map_state!=IsViewable)return false;
  XImage *image=XGetImage(display_.get(),keyboardWindow_,0,0,value.width,value.height,AllPlanes,ZPixmap);
  if(!image)return false;
  const qreal scale=value.width/524.;bool complete=true;
  for(int row=0;row<5;++row){
   QSet<unsigned long> colors;const int y=qRound((52+row*34)*scale);
   if(y>=image->height){complete=false;break;}
   for(int x=qRound(8*scale);x<image->width-qRound(8*scale);++x)colors.insert(XGetPixel(image,x,y)&0xffffff);
   if(colors.size()<2){complete=false;break;}
  }
  XDestroyImage(image);return complete;
 }
private slots:
 void init(){
  display_.reset(XOpenDisplay(nullptr));QVERIFY(display_);
  keyboard_.start(QStringLiteral(AGINO_EMBED_EXECUTABLE));QVERIFY(keyboard_.waitForStarted());
  QByteArray output;
  QTRY_VERIFY_WITH_TIMEOUT(([&]{output+=keyboard_.readAllStandardOutput();return output.contains('\n');})(),5000);
  bool ok=false;keyboardWindow_=output.left(output.indexOf('\n')).trimmed().toULongLong(&ok);QVERIFY(ok);QVERIFY(keyboardWindow_);
 }
 void cleanup(){
  if(keyboard_.state()!=QProcess::NotRunning){keyboard_.closeWriteChannel();if(!keyboard_.waitForFinished(3000)){keyboard_.kill();keyboard_.waitForFinished();}}
  if(host_){XDestroyWindow(display_.get(),host_);host_=0;XSync(display_.get(),False);}
  keyboardWindow_=0;display_.reset();
 }
 void matchesHostVisualAndRepaintsAfterEmbedding(){
  Display *display=display_.get();XWindowAttributes child{},root{};
  QVERIFY(attributes(child));QVERIFY(XGetWindowAttributes(display,DefaultRootWindow(display),&root));
  // A 32-bit ARGB foreign child under the opaque host was cleared to black on
  // the affected virgl/glamor setup. This fails before the visual fix even in Xvfb.
  QCOMPARE(child.depth,root.depth);QCOMPARE(child.visual->visualid,root.visual->visualid);
  host_=XCreateSimpleWindow(display,DefaultRootWindow(display),20,20,child.width+32,child.height+32,0,0,0);
  QVERIFY(host_);XMapWindow(display,host_);XReparentWindow(display,keyboardWindow_,host_,16,16);XMapWindow(display,keyboardWindow_);
  XSetWindowBackground(display,keyboardWindow_,BlackPixel(display,DefaultScreen(display)));XSync(display,False);
  QTRY_VERIFY_WITH_TIMEOUT(viewable(),5000);QTRY_VERIFY_WITH_TIMEOUT(rowsHaveContent(),5000);
  for(int n=0;n<3;++n){
   XUnmapWindow(display,host_);XSync(display,False);QTRY_VERIFY(!viewable());
   XMapWindow(display,host_);XSync(display,False);QTRY_VERIFY(viewable());
   XClearArea(display,keyboardWindow_,0,0,0,0,True);XSync(display,False);
   QTRY_VERIFY_WITH_TIMEOUT(rowsHaveContent(),5000);
  }
  keyboard_.closeWriteChannel();QVERIFY(keyboard_.waitForFinished(3000));
  QCOMPARE(keyboard_.exitStatus(),QProcess::NormalExit);QCOMPARE(keyboard_.exitCode(),0);
 }
};
QTEST_GUILESS_MAIN(EmbeddedVisualTest)
#include "test_embedvisual.moc"
