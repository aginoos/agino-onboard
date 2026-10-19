#include <QtTest>
#include <QMap>
#include "keysender.h"
// KeySender against a fake X server: the key events it sends for the symbols of the keyboard.
namespace {
struct Event {quint8 code;bool press;bool operator==(const Event &o)const{return code==o.code&&press==o.press;}};
class FakeBackend final:public KeyBackend {
public:
 // keycode -> symbols of the levels 0..3 of the groups
 QMap<quint8,QVector<quint32>> map[2];int group=0;QVector<Event> events;bool focus=true,caps=false;
 FakeBackend(){
  map[0][38]={'a','A',0,0};map[0][10]={'1','!',0,0};map[0][24]={'q','Q','@',0};map[0][25]={'w','W',0xe9,0xc9};map[0][36]={0xff0d,0xff0d,0,0};
  map[0][50]={0xffe1,0xffe1,0,0};map[0][92]={0xfe03,0xfe03,0,0};map[0][22]={0xff08,0xff08,0,0};
  // the second layout has the first two letters of the keys 38 and 24 the other way round
  map[1]=map[0];map[1][38]={'q','Q',0,0};map[1][24]={'a','A',0,0};
 }
 bool focusAccepts()override{return focus;}
 quint8 findKey(quint32 sym,int *level)override{
  for(int l=0;l<4;++l)for(auto it=map[group].begin();it!=map[group].end();++it)
   if(it.value().value(l)&&KeySender::character(it.value().value(l))==KeySender::character(sym)){*level=l;return it.key();}
  return 0;
 }
 quint32 keysymAt(quint8 code,int level)override{return map[group].value(code).value(level);}
 bool capsLockOn()override{return caps;}
 void fake(quint8 code,bool press)override{if(code)events.append({code,press});}
 void flush()override{}
};
using Events=QVector<Event>;
}
class KeySenderTest:public QObject {
 Q_OBJECT
private slots:
 void plainSymbolIsOneKey(){FakeBackend x;KeySender s(x);s.send({'a'},false);s.send({'a'},true);QCOMPARE(x.events,(Events{{38,true},{38,false}}));}
 void latchedShiftWrapsTheKeyInAShift(){
  // the keyboard sends 'A' after a tap on Shift: no Shift key event, the key needs a Shift of its own
  FakeBackend x;KeySender s(x);s.send({'A'},false);s.send({'A'},true);
  QCOMPARE(x.events,(Events{{50,true},{38,true},{38,false},{50,false}}));
 }
 void heldShiftIsNotWrappedAgain(){
  FakeBackend x;KeySender s(x);s.send({0xffe1},false);s.send({'!'},false);s.send({'!'},true);s.send({0xffe1},true);
  QCOMPARE(x.events,(Events{{50,true},{10,true},{10,false},{50,false}}));
 }
 void heldShiftIsReleasedAroundASymbolWithoutShift(){
  // the Shift of the keyboard is held and cancels a latched caps lock of the keyboard: the key is 'a', not 'A'
  FakeBackend x;KeySender s(x);s.send({0xffe1},false);s.send({'a'},false);s.send({'a'},true);s.send({0xffe1},true);
  QCOMPARE(x.events,(Events{{50,true},{50,false},{38,true},{38,false},{50,true},{50,false}}));
 }
 void heldShiftWithTheCapsLockOfTheServer(){
  // Shift held and Caps Lock on in the server: Shift + Caps Lock gives the small letter, the capital needs no Shift
  FakeBackend x;x.caps=true;KeySender s(x);s.send({0xffe1},false);s.send({'A'},false);s.send({'A'},true);s.send({0xffe1},true);
  QCOMPARE(x.events,(Events{{50,true},{50,false},{38,true},{38,false},{50,true},{50,false}}));
 }
 void level3SymbolIsWrappedInTheLevel3Shift(){
  FakeBackend x;KeySender s(x);s.send({'@'},false);s.send({'@'},true);
  QCOMPARE(x.events,(Events{{92,true},{24,true},{24,false},{92,false}}));
 }
 void shiftAndLevel3ForTheFourthLevel(){
  FakeBackend x;KeySender s(x);s.send({0xc9},false);s.send({0xc9},true);
  QCOMPARE(x.events,(Events{{50,true},{92,true},{25,true},{25,false},{92,false},{50,false}}));
 }
 void unicodeKeysymMatchesTheLatin1Symbol(){
  FakeBackend x;KeySender s(x);s.send({0x1000000+0xe9},false);s.send({0x1000000+0xe9},true);
  QCOMPARE(x.events,(Events{{92,true},{25,true},{25,false},{92,false}}));
 }
 void capsLockOfTheServerInvertsLetters(){
  FakeBackend x;x.caps=true;KeySender s(x);
  s.send({'a'},false);s.send({'a'},true);   // lower case needs the Shift that cancels the Caps Lock
  s.send({'A'},false);s.send({'A'},true);   // capital letters come out without it
  QCOMPARE(x.events,(Events{{50,true},{38,true},{38,false},{50,false},{38,true},{38,false}}));
 }
 void theKeyComesFromTheCurrentGroup(){
  FakeBackend x;x.group=1;KeySender s(x);s.send({'a'},false);s.send({'a'},true);
  QCOMPARE(x.events,(Events{{24,true},{24,false}}));
 }
 void theReleaseGoesToTheKeyThatWasPressed(){
  FakeBackend x;KeySender s(x);s.send({'a'},false);x.group=1;s.send({'a'},true);
  QCOMPARE(x.events,(Events{{38,true},{38,false}}));
 }
 void symbolNotInTheGroupIsNotSent(){FakeBackend x;KeySender s(x);s.send({0x20ac},false);s.send({0x20ac},true);QVERIFY(x.events.isEmpty());}
 void noFocusDropsThePressButNeverTheRelease(){
  FakeBackend x;KeySender s(x);
  s.send({'A'},false);x.focus=false;
  s.send({'A'},true);                       // the release of a key that was sent is sent wherever the focus is
  s.send({'a'},false);s.send({'a'},true);   // nothing has the focus: the press is dropped, so is its release
  QCOMPARE(x.events,(Events{{50,true},{38,true},{38,false},{50,false}}));
 }
 void aDroppedShiftPressIsNotHeld(){
  FakeBackend x;x.focus=false;KeySender s(x);s.send({0xffe1},false);x.focus=true;
  s.send({'A'},false);s.send({'A'},true);   // the Shift never reached the server: the key wraps its own
  QCOMPARE(x.events,(Events{{50,true},{38,true},{38,false},{50,false}}));
 }
 void theRepeatOfAHeldKeyIsOnlyPressedAgain(){
  FakeBackend x;KeySender s(x);s.send({'a'},false);s.send({'a'},false);s.send({'a'},true);
  QCOMPARE(x.events,(Events{{38,true},{38,true},{38,false}}));
 }
 void releaseAllReleasesEverythingThatIsDown(){
  FakeBackend x;{KeySender s(x);s.send({'A'},false);s.send({0xff08},false);}
  // the order of the releases is not defined: every key that was pressed is released
  QCOMPARE(x.events.size(),6);
  QMap<int,int> net;for(const auto &e:x.events)net[e.code]+=e.press?1:-1;
  QCOMPARE(net.value(50),0);QCOMPARE(net.value(38),0);QCOMPARE(net.value(22),0);
 }
};
QTEST_APPLESS_MAIN(KeySenderTest)
#include "test_keysender.moc"
