// SPDX-License-Identifier: GPL-3.0-or-later
// agino-keyboard-embed: the keyboard of the login and lock screens (agino, D-79).
//
// The session program (dde-session-shell) starts it, reads the X11 window id from the standard output
// and reparents the window into its own window, as it did with `onboard -e`. Differences to the keyboard
// of the desktop session (agino-keyboard, agino-keyboard-ui):
//  - no Fcitx5: the greeter has none, the lock screen has the input method of the user session, but the
//    password field is the only target. The keys are sent as X11 key events (XTest), like a physical keyboard,
//    but only while the window of the session program (its own window tree or a window of its process) has
//    the input focus; with any other window focused (a popup, another program) they are dropped;
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
#include "focusguard.h"
#include "i18n.h"
#include "keysender.h"
#include "preferences.h"
#include "startupdpi.h"
#include "native/nativepanel.h"
// Xlib defines macros (None, Bool, ...) that break the Qt headers: last.
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

namespace {
// KeyBackend on the X server of the session (its own connection, the Qt one is XCB).
class XlibBackend final:public KeyBackend {
public:
 // session: the pid of the session program that started us (its windows are the only ones that get keys).
 XlibBackend(WId own,pid_t session):own_(own),session_(session),display_(XOpenDisplay(nullptr)){
  int event=0,error=0,major=0,minor=0;
  if(display_&&!XTestQueryExtension(display_,&event,&error,&major,&minor)){XCloseDisplay(display_);display_=nullptr;}
 }
 ~XlibBackend()override{if(display_)XCloseDisplay(display_);}
 bool ready()const{return display_;}
 bool focusAccepts()override{
  const char *reason=focusInSession();
  const bool accepted=!reason;
  // One line when the verdict is first known and when it changes (no line per key, never the key).
  if(!known_||reason!=verdict_){
   known_=true;verdict_=reason;
   if(accepted)std::fprintf(stderr,"agino-keyboard-embed: the input focus is in the window of the session program, keys are sent\n");
   else std::fprintf(stderr,"agino-keyboard-embed: keys are dropped (%s)\n",reason);
  }
  return accepted;
 }
 quint8 findKey(quint32 sym,int *level)override{
  // The keyboard mapping may have changed (the layouts of the user are loaded after the greeter started).
  while(XPending(display_)){XEvent event;XNextEvent(display_,&event);if(event.type==MappingNotify)XRefreshKeyboardMapping(&event.xmapping);}
  int first=0,last=0;XDisplayKeycodes(display_,&first,&last);const int current=group();
  // The plainest level first; the key and the level come from the same group.
  for(int l=0;l<4;++l)for(int code=first;code<=last;++code){
   const quint32 candidate=quint32(XkbKeycodeToKeysym(display_,code,current,l));
   if(candidate&&KeySender::character(candidate)==KeySender::character(sym)){*level=l;return quint8(code);}
  }
  return 0;
 }
 quint32 keysymAt(quint8 code,int level)override{return quint32(XkbKeycodeToKeysym(display_,code,group(),level));}
 bool capsLockOn()override{XkbStateRec state{};return XkbGetState(display_,XkbUseCoreKbd,&state)==Success&&(state.locked_mods&LockMask);}
 void fake(quint8 code,bool press)override{if(code)XTestFakeKeyEvent(display_,code,press,CurrentTime);}
 void flush()override{XFlush(display_);}
private:
 // The group of the keyboard (layout) the server uses for the next key event.
 int group()const{XkbStateRec state{};return XkbGetState(display_,XkbUseCoreKbd,&state)==Success?state.group:0;}
 // The window and its ancestors up to the top-level (the direct child of the root); empty on any error or a loop.
 QList<quint32> chainUp(Window window)const{
  QList<quint32> chain;
  for(int depth=0;depth<64;++depth){
   Window root=None,parent=None,*children=nullptr;unsigned int count=0;
   if(!XQueryTree(display_,window,&root,&parent,&children,&count))return {};
   if(children)XFree(children);
   chain.append(quint32(window));
   if(parent==None||parent==root)return chain;   // the top-level (or the root itself)
   window=parent;
  }
  return {};
 }
 // _NET_WM_PID (Qt sets it on its windows) of each window of the chain; 0 where there is none or the query failed.
 QList<quint32> pidsOf(const QList<quint32> &chain)const{
  QList<quint32> pids;
  // Qt creates the atom with its first window, after we started: look it up each time (Xlib caches the found atom).
  const Atom atom=XInternAtom(display_,"_NET_WM_PID",True);
  for(const quint32 window:chain){
   quint32 pid=0;
   if(atom!=None){
    Atom type=None;int format=0;unsigned long count=0,after=0;unsigned char *data=nullptr;
    if(XGetWindowProperty(display_,Window(window),atom,0,1,False,XA_CARDINAL,&type,&format,&count,&after,&data)==Success&&data){
     if(type==XA_CARDINAL&&format==32&&count==1)pid=quint32(*reinterpret_cast<unsigned long*>(data));   // format 32 is an array of long
     XFree(data);
    }
   }
   pids.append(pid);
  }
  return pids;
 }
 // Keys only go to the window of the session program: the lock screen or greeter that embedded the keyboard. That is the
 // window tree of the top-level of the keyboard (it was reparented into it), or a window of the process of that program
 // (the focused window may be a child, a focus proxy or a popup of it). Not the keyboard itself, no other program.
 // Not embedded yet the top-level of the keyboard is itself: nothing outside it matches by the tree, only by the pid
 // (the keyboard is off screen then, so nobody can press a key; the same rule needs no special case).
 // Why the keys are not sent, or null when the input focus is in the window of the session program.
 const char *focusInSession()const{
  Window focus=None;int revert=0;XGetInputFocus(display_,&focus,&revert);
  if(focus==None)return "no window has the input focus";
  if(focus==PointerRoot)return "the input focus follows the pointer";
  const QList<quint32> own=chainUp(Window(own_)),at=chainUp(focus);
  if(own.isEmpty()||at.isEmpty())return "the window tree could not be read";
  if(FocusGuard::isOwn(own,at))return "the input focus is in the keyboard itself";
  if(FocusGuard::sameToplevel(own,at)||FocusGuard::pidMatches(pidsOf(at),session_))return nullptr;
  return "the input focus is in a window of another program";
 }
 WId own_;pid_t session_;Display *display_;const char *verdict_=nullptr;bool known_=false;   // verdict_: the reason of the last answer (null: accepted)
};

int signalPipe[2]={-1,-1};
void onSignal(int){const int saved=errno;const char byte=1;if(write(signalPipe[1],&byte,1)<0){}errno=saved;}
// A protocol error of the X server must not end the program with the default Xlib handler.
int ignoreXError(Display *,XErrorEvent *){return 0;}
// The standard descriptors are open: otherwise the first descriptor Qt or Xlib opens would become fd 0 and the
// notifier on the standard input would read the X protocol.
// Returns true when the standard input was closed and is /dev/null now (then it says nothing about the session program).
bool keepStandardDescriptors(){
 bool inputReopened=false;
 for(int fd=0;fd<=2;++fd)if(fcntl(fd,F_GETFD)<0&&errno==EBADF){const int opened=open("/dev/null",O_RDWR);if(opened>fd)close(opened);if(fd==0)inputReopened=true;}
 return inputReopened;
}
}

int main(int argc,char **argv){
 const bool inputReopened=keepStandardDescriptors();
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

 XlibBackend backend(panel.winId(),parent);
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
 QSocketNotifier input(STDIN_FILENO,QSocketNotifier::Read);input.setEnabled(!inputReopened);
 QObject::connect(&input,&QSocketNotifier::activated,&app,[&app]{char buffer[64];const ssize_t n=read(STDIN_FILENO,buffer,sizeof(buffer));if(n==0||(n<0&&errno!=EAGAIN&&errno!=EINTR))app.quit();});
 const int result=app.exec();
 // The sender goes first when the variables are destroyed: nothing may reach it after this.
 QObject::disconnect(keyConnection);
 keys.releaseAll();
 return result;
}
