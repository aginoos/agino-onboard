// SPDX-License-Identifier: GPL-3.0-or-later
// agino-keyboard-embed: the keyboard of the login and lock screens (agino, D-79).
//
// The session program (dde-session-shell) starts it, reads the X11 window id from the standard output
// and reparents the window into its own window, as it did with `onboard -e`. Differences to the keyboard
// of the desktop session (agino-keyboard, agino-keyboard-ui):
//  - no Fcitx5: the greeter has none, the lock screen has the input method of the user session, but the
//    password field is the only target. The keys are sent as X11 key events (XTest) to the window that has
//    the input focus, like a physical keyboard;
//  - no session services: no D-Bus name, no session bus, no tray icon, no shortcut, so it never collides with
//    the keyboard of the user session;
//  - the compact layout, always shown, never dismissed, no settings page, no files written.
// It exits when its standard input is closed, when its parent process ends or on SIGTERM/SIGINT/SIGHUP.
#include <QGuiApplication>
#include <QSocketNotifier>
#include <QTimer>
#include <csignal>
#include <cerrno>
#include <fcntl.h>
#include <cstdio>
#include <sys/prctl.h>
#include <unistd.h>
#include "keyboardstate.h"
#include "fcitxclient.h"
#include "i18n.h"
#include "keysender.h"
#include "preferences.h"
#include "startupdpi.h"
#include "native/nativepanel.h"
// Xlib defines macros (None, Bool, ...) that break the Qt headers: last.
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

namespace {
// KeyBackend on the X server of the session (its own connection, the Qt one is XCB).
class XlibBackend final:public KeyBackend {
public:
 explicit XlibBackend(WId own):own_(own),display_(XOpenDisplay(nullptr)){
  int event=0,error=0,major=0,minor=0;
  if(display_&&!XTestQueryExtension(display_,&event,&error,&major,&minor)){XCloseDisplay(display_);display_=nullptr;}
 }
 ~XlibBackend()override{if(display_)XCloseDisplay(display_);}
 bool ready()const{return display_;}
 bool focusAccepts()override{
  Window focus=None;int revert=0;XGetInputFocus(display_,&focus,&revert);
  // Nothing has the focus, or it is the keyboard itself (it never takes the focus, but never send to it).
  return focus!=None&&focus!=PointerRoot&&focus!=Window(own_);
 }
 quint8 keycodeFor(quint32 sym)override{return quint8(XKeysymToKeycode(display_,sym));}
 quint32 keysymAt(quint8 code,int level)override{return quint32(XkbKeycodeToKeysym(display_,code,group(),level));}
 bool capsLockOn()override{XkbStateRec state{};return XkbGetState(display_,XkbUseCoreKbd,&state)==Success&&(state.locked_mods&LockMask);}
 void fake(quint8 code,bool press)override{if(code)XTestFakeKeyEvent(display_,code,press,CurrentTime);}
 void flush()override{XFlush(display_);}
private:
 // The group of the keyboard (layout) the server uses for the next key event.
 int group()const{XkbStateRec state{};return XkbGetState(display_,XkbUseCoreKbd,&state)==Success?state.group:0;}
 WId own_;Display *display_;
};

int signalPipe[2]={-1,-1};
void onSignal(int){const char byte=1;if(write(signalPipe[1],&byte,1)<0){}}
// A protocol error of the X server must not end the program with the default Xlib handler.
int ignoreXError(Display *,XErrorEvent *){return 0;}
// The standard descriptors are open: otherwise the first descriptor Qt or Xlib opens would become fd 0 and the
// notifier on the standard input would read the X protocol.
void keepStandardDescriptors(){
 for(int fd=0;fd<=2;++fd)if(fcntl(fd,F_GETFD)<0&&errno==EBADF){const int opened=open("/dev/null",O_RDWR);if(opened>fd)close(opened);}
}
}

int main(int argc,char **argv){
 keepStandardDescriptors();
 // The panel has no editable control: no input context.
 qputenv("QT_IM_MODULE","compose");
 qunsetenv("WAYLAND_SOCKET");
 // The login and lock screens run on X11 (the keyboard is embedded by window id).
 qputenv("QT_QPA_PLATFORM","xcb");
 // Die with the process that started us (the session program), like the Fcitx keyboard UI does. The signal
 // ends the program through the event loop (below), so that the keys that are still down are released.
 const pid_t parent=getppid();
 prctl(PR_SET_PDEATHSIG,SIGTERM);
 if(getppid()!=parent)return 0;   // the parent ended before the signal was set up
 if(pipe2(signalPipe,O_CLOEXEC|O_NONBLOCK)==0){
  struct sigaction action{};action.sa_handler=onSignal;sigemptyset(&action.sa_mask);
  for(const int number:{SIGTERM,SIGINT,SIGHUP})sigaction(number,&action,nullptr);
 }
 XSetErrorHandler(ignoreXError);
 StartupDpiPolicy dpiPolicy;
 QGuiApplication app(argc,argv);
 dpiPolicy.restore();
 AginoI18n::install(app);
 app.setOrganizationName("agino");app.setApplicationName("agino-keyboard-embed");app.setApplicationDisplayName(QCoreApplication::translate("Keyboard","Screen Keyboard"));
 app.setQuitOnLastWindowClosed(false);

 // Fixed look: the compact layout (524 x 208) fits the 600 x 200 of the lock screen. The settings are only in memory:
 // nothing of the keyboard of the desktop session is used and nothing is written (the home of the greeter is lightdm's).
 Preferences prefs(nullptr,true);
 prefs.setCompact(true);prefs.setFunctionRow(false);prefs.setMode(KeyboardState::Manual);prefs.setSafeInput(false);
 FcitxClient client(nullptr,true);
 KeyboardState state;
 state.setMode(KeyboardState::Manual);
 state.useWallClock();
 state.setContext(1,true,false);   // there is always a target: the focused window
 NativePanel panel(prefs,client,state);
 panel.setEmbedded(true);
 panel.setFlags(Qt::FramelessWindowHint|Qt::X11BypassWindowManagerHint|Qt::WindowDoesNotAcceptFocus);
 const int height=panel.minimumHeight(),width=524;
 panel.setMinimumSize(QSize(width,height));panel.resize(width,height);
 panel.setPosition(-30000,-30000);   // mapped off screen until the session program reparents it

 XlibBackend backend(panel.winId());
 if(!backend.ready()){std::fprintf(stderr,"agino-keyboard-embed: cannot open the X11 display or it has no XTEST extension\n");return 2;}
 KeySender keys(backend);
 const auto keyConnection=QObject::connect(&state,&KeyboardState::keyRequested,&app,[&keys](KeySpec k,bool release){keys.send(k,release);});
 // The clock only serves the repeat of a held backspace: it runs while one is held.
 QTimer clock;clock.setInterval(16);
 QObject::connect(&clock,&QTimer::timeout,&state,&KeyboardState::tick);
 QObject::connect(&state,&KeyboardState::clockNeededChanged,&clock,[&clock](bool needed){if(needed)clock.start();else clock.stop();});
 QObject::connect(&app,&QCoreApplication::aboutToQuit,&state,&KeyboardState::cancelAll);
 // The panel never closes: if it is dismissed anyway it shows itself again (a hidden panel rejects all input).
 QObject::connect(&state,&KeyboardState::visibleChanged,&state,[&state](bool visible){if(!visible)QTimer::singleShot(0,&state,[&state]{state.manualShow();});});

 state.manualShow();
 panel.show();
 // Same protocol as `onboard -e`: the window id in decimal on the standard output.
 std::printf("%llu\n",static_cast<unsigned long long>(panel.winId()));
 std::fflush(stdout);

 QSocketNotifier signalNotifier(signalPipe[0],QSocketNotifier::Read);
 QObject::connect(&signalNotifier,&QSocketNotifier::activated,&app,[&app]{char buffer[16];if(read(signalPipe[0],buffer,sizeof(buffer))>=0)app.quit();});
 // The end of the standard input (the session program closed the process) ends the program.
 QSocketNotifier input(STDIN_FILENO,QSocketNotifier::Read);
 QObject::connect(&input,&QSocketNotifier::activated,&app,[&app]{char buffer[64];if(read(STDIN_FILENO,buffer,sizeof(buffer))<=0)app.quit();});
 const int result=app.exec();
 // The sender goes first when the variables are destroyed: nothing may reach it after this.
 QObject::disconnect(keyConnection);
 keys.releaseAll();
 return result;
}
