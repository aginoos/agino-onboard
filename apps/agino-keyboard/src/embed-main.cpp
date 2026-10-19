// SPDX-License-Identifier: GPL-3.0-or-later
// agino-keyboard-embed: the keyboard of the login and lock screens (agino, D-79).
//
// The session program (dde-session-shell) starts it, reads the X11 window id from the standard output
// and reparents the window into its own window, as it did with `onboard -e`. Differences to the keyboard
// of the desktop session (agino-keyboard, agino-keyboard-ui):
//  - no Fcitx5: the greeter has none, the lock screen has the input method of the user session, but the
//    password field is the only target. The keys are sent as X11 key events (XTest) to the window that has
//    the input focus, like a physical keyboard;
//  - no session services: no D-Bus name, no tray icon, no shortcut, so it never collides with the
//    keyboard of the user session;
//  - the compact layout, always shown, never dismissed, no settings page.
// It exits when its standard input is closed or its parent process ends.
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QSocketNotifier>
#include <QSet>
#include <QTimer>
#include <csignal>
#include <cstdio>
#include <sys/prctl.h>
#include <unistd.h>
#include "keyboardstate.h"
#include "fcitxclient.h"
#include "preferences.h"
#include "startupdpi.h"
#include "native/nativepanel.h"
// Xlib defines macros (None, Bool, ...) that break the Qt headers: last.
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

namespace {
// Sends the keys of the keyboard to the focused window as X11 key events.
class KeySender {
public:
 explicit KeySender(WId own):own_(own),display_(XOpenDisplay(nullptr)){}
 ~KeySender(){if(display_)XCloseDisplay(display_);}
 KeySender(const KeySender&)=delete;KeySender &operator=(const KeySender&)=delete;
 bool ready()const{return display_;}
 void send(KeySpec k,bool release){
  if(!display_)return;
  Window focus=None;int revert=0;XGetInputFocus(display_,&focus,&revert);
  // Nothing has the focus, or it is the keyboard itself (it never takes the focus, but never send to it).
  if(focus==None||focus==PointerRoot||focus==Window(own_))return;
  const KeyCode code=k.code?KeyCode(k.code):XKeysymToKeycode(display_,k.sym);
  if(!code)return;
  const bool shiftKey=k.sym==XK_Shift_L||k.sym==XK_Shift_R;
  if(shiftKey)shiftHeld_=!release;
  // A latched Shift (single tap, caps lock) only changes the symbol the keyboard sends, there is no Shift
  // key event: the key then needs a Shift of its own around it.
  if(!release&&!shiftKey&&!shiftHeld_&&needsShift(code,k.sym)){
   fake(shiftCode(),true);wrapped_.insert(code);
  }
  fake(code,!release);
  if(release&&wrapped_.remove(code))fake(shiftCode(),false);
  XFlush(display_);
 }
private:
 KeyCode shiftCode()const{return XKeysymToKeycode(display_,XK_Shift_L);}
 bool needsShift(KeyCode code,quint32 sym)const{
  const KeySym plain=XkbKeycodeToKeysym(display_,code,0,0),shifted=XkbKeycodeToKeysym(display_,code,0,1);
  return sym!=plain&&sym==shifted;
 }
 void fake(KeyCode code,bool press){if(code)XTestFakeKeyEvent(display_,code,press,CurrentTime);}
 WId own_;Display *display_;bool shiftHeld_=false;QSet<KeyCode> wrapped_;
};
}

int main(int argc,char **argv){
 // The panel has no editable control: no input context.
 qputenv("QT_IM_MODULE","compose");
 qunsetenv("WAYLAND_SOCKET");
 // The login and lock screens run on X11 (the keyboard is embedded by window id).
 qputenv("QT_QPA_PLATFORM","xcb");
 // Die with the process that started us (the session program), like the Fcitx keyboard UI does.
 prctl(PR_SET_PDEATHSIG,SIGTERM);
 StartupDpiPolicy dpiPolicy;
 QGuiApplication app(argc,argv);
 dpiPolicy.restore();
 app.setOrganizationName("agino");app.setApplicationName("agino-keyboard-embed");app.setApplicationDisplayName("屏幕键盘");
 app.setQuitOnLastWindowClosed(false);

 Preferences prefs;
 // Fixed look: the compact layout (524 x 208) fits the 600 x 200 of the lock screen; nothing from the user's own
 // preferences of the desktop keyboard is used (different application name, own settings file).
 prefs.setCompact(true);prefs.setFunctionRow(false);prefs.setMode(KeyboardState::Manual);prefs.setSafeInput(false);
 FcitxClient client;
 client.setStandalone(true);
 KeyboardState state;
 state.setMode(KeyboardState::Manual);
 state.setContext(1,true,false);   // there is always a target: the focused window
 NativePanel panel(prefs,client,state);
 panel.setFlags(Qt::FramelessWindowHint|Qt::X11BypassWindowManagerHint|Qt::WindowDoesNotAcceptFocus);
 const int height=panel.minimumHeight(),width=524;
 panel.setMinimumSize(QSize(width,height));panel.resize(width,height);
 panel.setPosition(-3000,-3000);   // mapped off screen until the session program reparents it

 KeySender keys(panel.winId());
 if(!keys.ready()){std::fprintf(stderr,"agino-keyboard-embed: cannot open the X11 display\n");return 2;}
 QObject::connect(&state,&KeyboardState::keyRequested,&app,[&keys](KeySpec k,bool release){keys.send(k,release);});
 QTimer clock;clock.setInterval(16);QElapsedTimer elapsed;elapsed.start();
 QObject::connect(&clock,&QTimer::timeout,&state,[&]{state.advanceClock(elapsed.elapsed());});
 clock.start();
 QObject::connect(&app,&QCoreApplication::aboutToQuit,&state,&KeyboardState::cancelAll);

 state.manualShow();
 panel.show();
 // Same protocol as `onboard -e`: the window id in decimal on the standard output.
 std::printf("%llu\n",static_cast<unsigned long long>(panel.winId()));
 std::fflush(stdout);

 // The end of the standard input (the session program closed the process) ends the program.
 QSocketNotifier input(STDIN_FILENO,QSocketNotifier::Read);
 QObject::connect(&input,&QSocketNotifier::activated,&app,[&app]{char buffer[64];if(read(STDIN_FILENO,buffer,sizeof(buffer))<=0)app.quit();});
 return app.exec();
}
