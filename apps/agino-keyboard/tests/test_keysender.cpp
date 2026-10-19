#include <QtTest>
#include "keysender.h"
// KeySender against a fake X server: the key events it sends for the symbols of the keyboard.
namespace {
struct Event {quint8 code;bool press;bool operator==(const Event &o)const{return code==o.code&&press==o.press;}};
class FakeBackend final:public KeyBackend {
public:
 // keycode -> symbols of the levels 0..3 of the current group
 QHash<quint8,QVector<quint32>> map;QVector<Event> events;bool focus=true,caps=false;
 FakeBackend(){
  map[38]={'a','A',0,0};map[10]={'1','!',0,0};map[24]={'q','Q','@',0};map[36]={0xff0d,0xff0d,0,0};
  map[50]={0xffe1,0xffe1,0,0};map[92]={0xfe03,0xfe03,0,0};map[22]={0xff08,0xff08,0,0};
 }
 bool focusAccepts()override{return focus;}
 quint8 keycodeFor(quint32 sym)override{for(auto it=map.begin();it!=map.end();++it)if(it.value().contains(sym))return it.key();return 0;}
 quint32 keysymAt(quint8 code,int level)override{return map.value(code).value(level);}
 bool capsLockOn()override{return caps;}
 void fake(quint8 code,bool press)override{if(code)events.append({code,press});}
 void flush()override{}
};
}
class KeySenderTest:public QObject {
 Q_OBJECT
private slots:
 void plainSymbolIsOneKey(){FakeBackend x;KeySender s(x);s.send({'a'},false);s.send({'a'},true);QCOMPARE(x.events,(QVector<Event>{{38,true},{38,false}}));}
 void latchedShiftWrapsTheKeyInAShift(){
  // the keyboard sends 'A' after a tap on Shift: no Shift key event, the key needs a Shift of its own
  FakeBackend x;KeySender s(x);s.send({'A'},false);s.send({'A'},true);
  QCOMPARE(x.events,(QVector<Event>{{50,true},{38,true},{38,false},{50,false}}));
 }
 void heldShiftIsNotWrappedAgain(){
  FakeBackend x;KeySender s(x);s.send({0xffe1},false);s.send({'!'},false);s.send({'!'},true);s.send({0xffe1},true);
  QCOMPARE(x.events,(QVector<Event>{{50,true},{10,true},{10,false},{50,false}}));
 }
 void level3SymbolIsWrappedInTheLevel3Shift(){
  FakeBackend x;KeySender s(x);s.send({'@'},false);s.send({'@'},true);
  QCOMPARE(x.events,(QVector<Event>{{92,true},{24,true},{24,false},{92,false}}));
 }
 void capsLockOfTheServerInvertsLetters(){
  FakeBackend x;x.caps=true;KeySender s(x);
  s.send({'a'},false);s.send({'a'},true);   // lower case needs the Shift that cancels the Caps Lock
  s.send({'A'},false);s.send({'A'},true);   // capital letters come out without it
  QCOMPARE(x.events,(QVector<Event>{{50,true},{38,true},{38,false},{50,false},{38,true},{38,false}}));
 }
 void symbolNotInTheGroupIsNotSent(){FakeBackend x;KeySender s(x);s.send({0x20ac},false);s.send({0x20ac},true);QVERIFY(x.events.isEmpty());}
 void noFocusDropsThePressButNeverTheRelease(){
  FakeBackend x;KeySender s(x);
  s.send({'A'},false);x.focus=false;
  s.send({'A'},true);                       // the release of a key that was sent is sent wherever the focus is
  s.send({'a'},false);s.send({'a'},true);   // nothing has the focus: the press is dropped, so is its release
  QCOMPARE(x.events,(QVector<Event>{{50,true},{38,true},{38,false},{50,false}}));
 }
 void releaseAllReleasesEverythingThatIsDown(){
  FakeBackend x;{KeySender s(x);s.send({'A'},false);s.send({0xff08},false);}
  QCOMPARE(x.events.size(),6);
  int down=0;for(const auto &e:x.events)down+=e.press?1:-1;QCOMPARE(down,0);
 }
};
QTEST_APPLESS_MAIN(KeySenderTest)
#include "test_keysender.moc"
