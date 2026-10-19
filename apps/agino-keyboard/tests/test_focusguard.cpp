#include <QtTest>
#include "focusguard.h"
// The decision of the embedded keyboard where it may type (SEC-03), from parent chains and pids; the Xlib side is not tested here.
using Chain=QList<quint32>;
class FocusGuardTest:public QObject {
 Q_OBJECT
private slots:
 void ownWindowAndItsInside(){
  const Chain own{10,20,30};   // the keyboard in a container in the top-level 30
  QVERIFY(FocusGuard::isOwn(own,Chain{10,20,30}));
  QVERIFY(FocusGuard::isOwn(own,Chain{11,10,20,30}));   // a child of the keyboard
  QVERIFY(!FocusGuard::isOwn(own,Chain{20,30}));        // the container is not the keyboard
  QVERIFY(!FocusGuard::isOwn(Chain{},Chain{10}));
 }
 void sameTopLevel(){
  const Chain own{10,20,30};
  QVERIFY(FocusGuard::sameToplevel(own,Chain{40,30}));     // another widget of the session program window
  QVERIFY(FocusGuard::sameToplevel(own,Chain{20,30}));
  QVERIFY(!FocusGuard::sameToplevel(own,Chain{50,60}));    // another application
  QVERIFY(!FocusGuard::sameToplevel(own,Chain{}));
  QVERIFY(!FocusGuard::sameToplevel(Chain{},Chain{30}));
 }
 void unparentedMatchesOnlyInsideItself(){
  const Chain own{10};   // a direct child of the root: not embedded yet
  QVERIFY(!FocusGuard::sameToplevel(own,Chain{50}));
  QVERIFY(!FocusGuard::sameToplevel(own,Chain{51,50}));
  QVERIFY(FocusGuard::isOwn(own,Chain{11,10}));   // the only windows of that tree are the keyboard's own
 }
 void pids(){
  QVERIFY(FocusGuard::pidMatches(Chain{0,4242,0},4242));   // the focus proxy or an ancestor of the focus belongs to the program
  QVERIFY(!FocusGuard::pidMatches(Chain{0,4243},4242));
  QVERIFY(!FocusGuard::pidMatches(Chain{},4242));
  QVERIFY(!FocusGuard::pidMatches(Chain{0,0},0));      // no pid is not a match for no session
  QVERIFY(!FocusGuard::pidMatches(Chain{1},1));        // init: the parent ended
  QVERIFY(!FocusGuard::pidMatches(Chain{4242},-1));
 }
};
QTEST_APPLESS_MAIN(FocusGuardTest)
#include "test_focusguard.moc"
