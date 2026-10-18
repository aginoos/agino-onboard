// SPDX-License-Identifier: GPL-3.0-or-later
// A minimal controlled protocol peer exercises the real loaded Qt plugin.
#include <QtTest>
#include <QProcess>
#include <QScreen>
#include <QWindow>
#include <wayland-server.h>
#include "layer-shell-server.h"
#include "desktopadapter.h"
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

struct Peer {
 struct Role { wl_resource *resource=nullptr,*surface=nullptr; bool configured=false;Peer *peer=nullptr; };
 struct Output { wl_global *global=nullptr; int x=0; };
 wl_display *display=wl_display_create();
 Output outputs[2];std::vector<std::unique_ptr<Role>> roles;
 std::atomic<int> count{0},top{0},left{0},zone{0},forcedWidth{0},forcedHeight{0};std::atomic<uint32_t> anchor{0},interactivity{999};std::atomic<bool> remove{false},stop{false};std::thread thread;

 static void destroy(wl_client *,wl_resource *resource){wl_resource_destroy(resource);}
 static const struct wl_region_interface *regionImpl(){
  static struct wl_region_interface impl={destroy,[](wl_client *,wl_resource *,int32_t,int32_t,int32_t,int32_t){},[](wl_client *,wl_resource *,int32_t,int32_t,int32_t,int32_t){}};
  return &impl;
 }
 static const struct wl_surface_interface *surfaceImpl(){
  static struct wl_surface_interface impl=[] {
   struct wl_surface_interface i{};i.destroy=destroy;
   i.attach=[](wl_client *,wl_resource *,wl_resource *,int32_t,int32_t){};
   i.damage=[](wl_client *,wl_resource *,int32_t,int32_t,int32_t,int32_t){};
   i.frame=[](wl_client *c,wl_resource *,uint32_t id){auto r=wl_resource_create(c,&wl_callback_interface,1,id);wl_callback_send_done(r,0);wl_resource_destroy(r);};
   i.set_opaque_region=[](wl_client *,wl_resource *,wl_resource *){};
   i.set_input_region=i.set_opaque_region;
   i.commit=[](wl_client *,wl_resource *r){auto peer=static_cast<Peer *>(wl_resource_get_user_data(r));
    for(auto &role:peer->roles)if(role->surface==r&&role->resource&&!role->configured){role->configured=true;zwlr_layer_surface_v1_send_configure(role->resource,wl_display_next_serial(peer->display),0,0);}};
   i.set_buffer_transform=[](wl_client *,wl_resource *,int32_t){};i.set_buffer_scale=i.set_buffer_transform;
   i.damage_buffer=i.damage;i.offset=[](wl_client *,wl_resource *,int32_t,int32_t){};return i;
  }();return &impl;
 }
 static const struct wl_compositor_interface *compositorImpl(){
  static struct wl_compositor_interface impl={
   [](wl_client *c,wl_resource *r,uint32_t id){auto s=wl_resource_create(c,&wl_surface_interface,wl_resource_get_version(r),id);wl_resource_set_implementation(s,surfaceImpl(),wl_resource_get_user_data(r),nullptr);},
   [](wl_client *c,wl_resource *,uint32_t id){auto r=wl_resource_create(c,&wl_region_interface,1,id);wl_resource_set_implementation(r,regionImpl(),nullptr,nullptr);}};
  return &impl;
 }
 static const struct zwlr_layer_surface_v1_interface *roleImpl(){
  static struct zwlr_layer_surface_v1_interface impl=[] {
   struct zwlr_layer_surface_v1_interface i{};
   i.set_size=[](wl_client *,wl_resource *,uint32_t,uint32_t){};
   i.set_anchor=[](wl_client *,wl_resource *r,uint32_t anchor){static_cast<Role *>(wl_resource_get_user_data(r))->peer->anchor=anchor;};
   i.set_exclusive_zone=[](wl_client *,wl_resource *r,int32_t zone){static_cast<Role *>(wl_resource_get_user_data(r))->peer->zone=zone;};
   i.set_margin=[](wl_client *,wl_resource *r,int32_t top,int32_t,int32_t,int32_t left){auto peer=static_cast<Role *>(wl_resource_get_user_data(r))->peer;peer->top=top;peer->left=left;};
   i.set_keyboard_interactivity=[](wl_client *,wl_resource *r,uint32_t interactivity){static_cast<Role *>(wl_resource_get_user_data(r))->peer->interactivity=interactivity;};
   i.get_popup=[](wl_client *,wl_resource *,wl_resource *){};
   i.ack_configure=[](wl_client *,wl_resource *,uint32_t){};
   i.destroy=destroy;i.set_layer=[](wl_client *,wl_resource *,uint32_t){};return i;
  }();return &impl;
 }
 static const struct zwlr_layer_shell_v1_interface *shellImpl(){
  static struct zwlr_layer_shell_v1_interface impl={
   [](wl_client *c,wl_resource *r,uint32_t id,wl_resource *surface,wl_resource *,uint32_t,const char *){
    auto peer=static_cast<Peer *>(wl_resource_get_user_data(r));auto role=std::make_unique<Role>();role->surface=surface;role->peer=peer;
    role->resource=wl_resource_create(c,&zwlr_layer_surface_v1_interface,wl_resource_get_version(r),id);
    wl_resource_set_implementation(role->resource,roleImpl(),role.get(),[](wl_resource *r){static_cast<Role *>(wl_resource_get_user_data(r))->resource=nullptr;});
    peer->roles.push_back(std::move(role));++peer->count;
   },destroy};return &impl;
 }
 Peer(){
  wl_display_init_shm(display);
  // Capability-only hint: this no-focus window uses the compose input context.
  static const wl_interface textHint={"zwp_text_input_manager_v3",1,0,nullptr,0,nullptr};
  wl_global_create(display,&textHint,1,this,[](wl_client *c,void *,uint32_t,uint32_t id){wl_resource_create(c,&textHint,1,id);});
  wl_global_create(display,&wl_compositor_interface,4,this,[](wl_client *c,void *data,uint32_t v,uint32_t id){auto r=wl_resource_create(c,&wl_compositor_interface,v,id);wl_resource_set_implementation(r,compositorImpl(),data,nullptr);});
  wl_global_create(display,&zwlr_layer_shell_v1_interface,3,this,[](wl_client *c,void *data,uint32_t v,uint32_t id){auto r=wl_resource_create(c,&zwlr_layer_shell_v1_interface,v,id);wl_resource_set_implementation(r,shellImpl(),data,nullptr);});
  for(int n=0;n<2;++n){outputs[n].x=n*1280;outputs[n].global=wl_global_create(display,&wl_output_interface,3,&outputs[n],[](wl_client *c,void *data,uint32_t v,uint32_t id){
   auto o=static_cast<Output *>(data);auto r=wl_resource_create(c,&wl_output_interface,v,id);
   static const struct wl_output_interface impl={destroy};wl_resource_set_implementation(r,&impl,data,nullptr);
   wl_output_send_geometry(r,o->x,0,300,200,WL_OUTPUT_SUBPIXEL_UNKNOWN,"Agino","test",WL_OUTPUT_TRANSFORM_NORMAL);
   wl_output_send_mode(r,WL_OUTPUT_MODE_CURRENT|WL_OUTPUT_MODE_PREFERRED,1280,800,60000);
   if(v>=2){wl_output_send_scale(r,1);wl_output_send_done(r);}
  });}
 }
 void start(){thread=std::thread([this]{while(!stop){wl_event_loop_dispatch(wl_display_get_event_loop(display),10);
  if(remove.exchange(false)){for(auto &role:roles)if(role->resource)zwlr_layer_surface_v1_send_closed(role->resource);wl_global_destroy(outputs[0].global);outputs[0].global=nullptr;}
  if(auto width=forcedWidth.exchange(0)){for(auto &role:roles)if(role->resource)zwlr_layer_surface_v1_send_configure(role->resource,wl_display_next_serial(display),width,forcedHeight.load());}
  wl_display_flush_clients(display);
 }});}
 ~Peer(){stop=true;if(thread.joinable())thread.join();wl_display_destroy_clients(display);wl_display_destroy(display);}
};

class ShellLifecycleTest:public QObject {
 Q_OBJECT
private slots:
 void floatingGeometryUsesMargins_data(){QTest::addColumn<double>("scale");QTest::newRow("100-percent")<<1.0;QTest::newRow("150-percent")<<1.5;QTest::newRow("200-percent")<<2.0;}
 void floatingGeometryUsesMargins(){
  QFETCH(double,scale);
  QTemporaryDir runtime;QVERIFY(runtime.isValid());qputenv("XDG_RUNTIME_DIR",runtime.path().toUtf8());
  Peer peer;QCOMPARE(wl_display_add_socket(peer.display,"agino-peer"),0);peer.start();
  QProcess client;auto cleanup=qScopeGuard([&]{client.kill();client.waitForFinished();});
  auto env=QProcessEnvironment::systemEnvironment();env.insert("WAYLAND_DISPLAY","agino-peer");env.insert("QT_QPA_PLATFORM","wayland");env.insert("QT_WAYLAND_SHELL_INTEGRATION","agino-keyboard");env.remove("WAYLAND_SOCKET");env.insert("QT_SCALE_FACTOR",QString::number(scale));client.setProcessEnvironment(env);
  client.start(QCoreApplication::applicationFilePath(),{"--floating-client"});QVERIFY(client.waitForStarted());
  QTRY_VERIFY_WITH_TIMEOUT(client.canReadLine(),5000);QCOMPARE(client.readLine(),QByteArray("ready\n"));
  QTRY_COMPARE(peer.anchor.load(),5u);QCOMPARE(peer.top.load(),qRound(80*scale));QCOMPARE(peer.left.load(),qRound(100*scale));QCOMPARE(peer.zone.load(),-1);QCOMPARE(peer.interactivity.load(),0u);
  client.write("move\n");client.waitForBytesWritten();
  QVERIFY(client.waitForFinished(5000));QVERIFY2(client.exitCode()==0,client.readAllStandardError().constData());
  QCOMPARE(peer.top.load(),qRound(160*scale));QCOMPARE(peer.left.load(),qRound(220*scale));QCOMPARE(peer.count.load(),1);
 }
 void negotiatedFloatingGeometryKeepsBottom(){
  QTemporaryDir runtime;QVERIFY(runtime.isValid());qputenv("XDG_RUNTIME_DIR",runtime.path().toUtf8());Peer peer;QCOMPARE(wl_display_add_socket(peer.display,"agino-peer"),0);peer.start();
  QProcess client;auto cleanup=qScopeGuard([&]{client.kill();client.waitForFinished();});auto env=QProcessEnvironment::systemEnvironment();env.insert("WAYLAND_DISPLAY","agino-peer");env.insert("QT_QPA_PLATFORM","wayland");env.insert("QT_IM_MODULE","compose");env.insert("QT_WAYLAND_SHELL_INTEGRATION","agino-keyboard");env.remove("WAYLAND_SOCKET");client.setProcessEnvironment(env);client.start(QCoreApplication::applicationFilePath(),{"--negotiated-client"});QVERIFY(client.waitForStarted());QTRY_VERIFY_WITH_TIMEOUT(client.canReadLine(),5000);QCOMPARE(client.readLine(),QByteArray("ready\n"));
  peer.forcedHeight=344;peer.forcedWidth=900;client.write("first\n");client.waitForBytesWritten();QTRY_VERIFY_WITH_TIMEOUT(client.canReadLine(),5000);QCOMPARE(client.readLine(),QByteArray("request\n"));peer.forcedHeight=400;peer.forcedWidth=900;client.write("second\n");client.waitForBytesWritten();QVERIFY(client.waitForFinished(5000));QVERIFY2(client.exitCode()==0,client.readAllStandardError().constData());
 }
 void closedRoleCannotRemapItsReplacement(){
  QTemporaryDir runtime;QVERIFY(runtime.isValid());qputenv("XDG_RUNTIME_DIR",runtime.path().toUtf8());
  Peer peer;QCOMPARE(wl_display_add_socket(peer.display,"agino-peer"),0);peer.start();
  QProcess client;auto cleanup=qScopeGuard([&]{client.kill();client.waitForFinished();});
  auto env=QProcessEnvironment::systemEnvironment();env.insert("WAYLAND_DISPLAY","agino-peer");env.insert("QT_QPA_PLATFORM","wayland");
  env.insert("QT_WAYLAND_SHELL_INTEGRATION","agino-keyboard");env.remove("WAYLAND_SOCKET");client.setProcessEnvironment(env);
  client.start(QCoreApplication::applicationFilePath(),{"--client"});QVERIFY(client.waitForStarted());
  QTRY_VERIFY_WITH_TIMEOUT(client.canReadLine(),5000);QCOMPARE(client.readLine(),QByteArray("ready\n"));
  QCOMPARE(peer.count.load(),1);peer.remove=true;
  QVERIFY(client.waitForFinished(10000));QVERIFY2(client.exitCode()==0,client.readAllStandardError().constData());
  // One initial role and one replacement. A stale timer would create a third.
  QCOMPARE(peer.count.load(),2);
 }
};

int floatingClientMain(int argc,char **argv){
 QGuiApplication app(argc,argv);QWindow panel;panel.setScreen(app.screens()[0]);panel.resize(934,320);
 panel.setProperty("_agino_keyboard_position",QPoint(100,80));panel.setProperty("_agino_keyboard_floating",true);panel.setProperty("_agino_keyboard_exclusive_zone",-1);
 panel.show();if(!QTest::qWaitForWindowExposed(&panel,3000))return 2;QTest::qWait(100);
 puts("ready");fflush(stdout);char command[16];if(!fgets(command,sizeof(command),stdin))return 3;
 panel.setProperty("_agino_keyboard_position",QPoint(220,160));QTest::qWait(100);
 return panel.position()==QPoint(220,160)?0:4;
}
int negotiatedClientMain(int argc,char **argv){
 QGuiApplication app(argc,argv);DesktopAdapter adapter;QWindow panel;panel.setScreen(app.screens()[0]);panel.resize(934,344);if(!adapter.attach(&panel))return 2;adapter.configurePanel(panel.screen(),QSize(934,344));adapter.setPanelVisible(true);if(!QTest::qWaitForWindowExposed(&panel,3000))return 3;adapter.moveBy(-100,-100);puts("ready");fflush(stdout);char command[16];if(!fgets(command,sizeof(command),stdin))return 4;
 auto waitSize=[&](QSize size){QElapsedTimer clock;clock.start();while(panel.size()!=size&&clock.elapsed()<3000)QTest::qWait(10);return panel.size()==size;};if(!waitSize(QSize(900,344)))return 5;const int bottom=panel.y()+panel.height();adapter.configurePanel(panel.screen(),QSize(934,400));puts("request");fflush(stdout);if(!fgets(command,sizeof(command),stdin))return 6;if(!waitSize(QSize(900,400)))return 7;QTest::qWait(50);if(panel.y()+panel.height()!=bottom){fprintf(stderr,"floating bottom changed: %d -> %d\n",bottom,panel.y()+panel.height());return 8;}return 0;
}
int clientMain(int argc,char **argv){
 QGuiApplication app(argc,argv);if(app.screens().size()!=2)return 2;
 QWindow panel;panel.setScreen(app.screens()[0]);panel.resize(934,320);panel.setProperty("_agino_keyboard_exclusive_zone",320);
 bool removed=false;QObject::connect(&app,&QGuiApplication::screenRemoved,&panel,[&](QScreen *){
  panel.hide();panel.setScreen(app.primaryScreen());panel.show();removed=true;
 });
 panel.show();if(!QTest::qWaitForWindowExposed(&panel,3000))return 3;
 if(panel.size()!=QSize(934,320))return 4; // zero configure preserves requested dimensions
 puts("ready");fflush(stdout);
 QElapsedTimer deadline;deadline.start();while(!removed&&deadline.elapsed()<5000)QTest::qWait(10);
 if(!removed||!QTest::qWaitForWindowExposed(&panel,3000))return 5;
 QTest::qWait(200);return panel.isVisible()&&panel.size()==QSize(934,320)?0:6;
}
int main(int argc,char **argv){
 if(argc>1&&QByteArray(argv[1])=="--negotiated-client")return negotiatedClientMain(argc,argv);
 if(argc>1&&QByteArray(argv[1])=="--floating-client")return floatingClientMain(argc,argv);
 if(argc>1&&QByteArray(argv[1])=="--client")return clientMain(argc,argv);
 QCoreApplication app(argc,argv);ShellLifecycleTest test;return QTest::qExec(&test,argc,argv);
}
#include "test_shelllifecycle.moc"
