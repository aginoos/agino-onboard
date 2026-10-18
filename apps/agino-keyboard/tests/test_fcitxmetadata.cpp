#include <QtTest>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusVirtualObject>
#include "fcitxclient.h"

struct MethodInfo {QString id,name,nativeName,icon,label,language;bool configurable;};
Q_DECLARE_METATYPE(MethodInfo)
QDBusArgument &operator<<(QDBusArgument &argument,const MethodInfo &entry){argument.beginStructure();argument<<entry.id<<entry.name<<entry.nativeName<<entry.icon<<entry.label<<entry.language<<entry.configurable;argument.endStructure();return argument;}
const QDBusArgument &operator>>(const QDBusArgument &argument,MethodInfo &entry){argument.beginStructure();argument>>entry.id>>entry.name>>entry.nativeName>>entry.icon>>entry.label>>entry.language>>entry.configurable;argument.endStructure();return argument;}

// A complete wire fixture isolates failures in the otherwise real DBus client.
class MetadataService : public QDBusVirtualObject {
public:
 QString current="pinyin",nativeName="拼音";
 int delay=0,snapshotFailures=0;
 bool words=false,holdPages=false;
 quint64 currentContext=1;
 QList<QDBusMessage> pages;
 QString failAction;
 bool malformed=false;
 bool extended=false;
 int calls=0;
 int failures=1;
 QString introspect(const QString &)const override{return {};}
 QVariantList snapshot()const{return {QVariant::fromValue(currentContext),QVariant::fromValue(quint64(1)),true,false,QString(),current,words?QStringList{"你"}:QStringList(),false,words,QString("unknown"),0,0};}
 bool handleMessage(const QDBusMessage &message,const QDBusConnection &bus)override{
  if(message.path()=="/controller"&&message.member()=="AvailableInputMethods"){
   if(++calls<=failures){
    bus.send(malformed?message.createReply(QVariantList{QString("invalid metadata")}):message.createErrorReply("org.freedesktop.DBus.Error.Failed","temporary Controller failure"));return true;
   }
   QDBusArgument array;array.beginArray(qMetaTypeId<MethodInfo>());
   array<<MethodInfo{"pinyin","Pinyin",nativeName,"fcitx-pinyin","拼","zh_CN",true}
        <<MethodInfo{"keyboard-us","Keyboard - English (US)","","input-keyboard","en","en",true};
   if(extended)array<<MethodInfo{"keyboard-jp","Japanese","","input-keyboard","ja","ja",true}<<MethodInfo{"keyboard-de","German","","input-keyboard","de","de",true}<<MethodInfo{"unknown","Unspecified","","input-keyboard","","",true};
   array.endArray();auto reply=message.createReply(QVariantList{QVariant::fromValue(array)});if(delay)QTimer::singleShot(delay,[bus,reply]{bus.send(reply);});else bus.send(reply);return true;
  }
  if(message.path()=="/org/agino/Keyboard1"){
   if(message.member()=="GetSnapshot"){if(snapshotFailures-->0)bus.send(message.createErrorReply("org.freedesktop.DBus.Error.Failed","temporary snapshot error"));else bus.send(message.createReply(snapshot()));return true;}
   if(message.member()==failAction){bus.send(message.createErrorReply("org.freedesktop.DBus.Error.Failed","temporary action error"));return true;}
   if(message.member()=="RequestPage"){if(holdPages)pages.append(message);else bus.send(message.createReply(QVariantList{true}));return true;}
   if(message.member()=="InputMethods"){bus.send(message.createReply(QVariantList{extended?QStringList{"keyboard-us","pinyin","keyboard-jp","keyboard-de","unknown"}:QStringList{"keyboard-us","pinyin"}}));return true;}
  }
  return false;
 }
 void changeInputMethod(const QString &method){current=method;auto signal=QDBusMessage::createSignal("/org/agino/Keyboard1","org.agino.Keyboard1","SnapshotChanged");signal.setArguments(snapshot());QDBusConnection("metadata-fixture").send(signal);}
};

class MetadataTest : public QObject {
 Q_OBJECT
 MetadataService service;
private slots:
 void initTestCase(){
  qDBusRegisterMetaType<MethodInfo>();auto bus=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"metadata-fixture");
  QVERIFY(bus.registerService("org.agino.Keyboard1"));QVERIFY(bus.registerService("org.fcitx.Fcitx5"));
  QVERIFY(bus.registerVirtualObject("/org/agino/Keyboard1",&service));QVERIFY(bus.registerVirtualObject("/controller",&service));
 }
 void init(){QDBusConnection("metadata-fixture").registerService("org.fcitx.Fcitx5");service.delay=0;service.snapshotFailures=0;service.nativeName="拼音";service.words=false;service.failAction.clear();service.extended=false;service.holdPages=false;service.pages.clear();service.currentContext=1;}
 void transientMetadataFailureRecovers_data(){QTest::addColumn<bool>("malformed");QTest::newRow("Controller error")<<false;QTest::newRow("malformed reply")<<true;}
 void transientMetadataFailureRecovers(){
  QFETCH(bool,malformed);service.malformed=malformed;service.calls=0;service.failures=1;service.current="pinyin";
  FcitxClient client;QTRY_VERIFY(client.editable());
  // No policy change, Settings call or new snapshot should be needed to recover.
  QTRY_VERIFY_WITH_TIMEOUT(client.inputMethodStatus()==QString::fromUtf8("中文：拼音"),3000);
  QVERIFY(service.calls>=2);QCOMPARE(client.inputMethodNames().size(),2);
  QCOMPARE(client.property("inputLanguageCode").toString(),QString("CN"));
  service.changeInputMethod("keyboard-us");QTRY_COMPARE(client.inputMethodStatus(),QString::fromUtf8("英文：Keyboard - English (US)"));QCOMPARE(client.property("inputLanguageCode").toString(),QString("EN"));
 }
 void persistentFailureStopsRetryingAndManualRefreshCanRecover(){
  service.malformed=false;service.calls=0;service.failures=1000;service.current="pinyin";
  FcitxClient client;QTRY_VERIFY(client.editable());QTRY_COMPARE_WITH_TIMEOUT(service.calls,4,5000);
  QTest::qWait(1000);QCOMPARE(service.calls,4);QCOMPARE(client.inputMethodStatus(),QString::fromUtf8("输入法状态暂不可用"));
  service.failures=service.calls+1;client.refreshInputMethodNames();QTRY_COMPARE(client.inputMethodStatus(),QString::fromUtf8("中文：拼音"));QVERIFY(service.calls>=6);service.extended=true;client.refreshInputMethodNames();QTRY_COMPARE(client.inputMethods().size(),5);QTRY_COMPARE(client.inputMethodNames().size(),5);
 }
 void languageAbbreviationsFollowMetadata(){
  service.extended=true;service.malformed=false;service.calls=0;service.failures=0;service.current="keyboard-jp";
  FcitxClient client;QTRY_COMPARE(client.inputLanguageCode(),QString("JA"));service.changeInputMethod("keyboard-de");QTRY_COMPARE(client.inputLanguageCode(),QString("DE"));service.changeInputMethod("unknown");QTRY_VERIFY(client.inputLanguageCode().isEmpty());service.extended=false;
 }
 void controllerOwnerLossClearsNamesAndRecovers(){service.calls=0;service.failures=0;service.current="pinyin";service.nativeName="拼音";FcitxClient client;QTRY_COMPARE(client.inputMethodNames().value("pinyin").toString(),QString("拼音"));auto bus=QDBusConnection("metadata-fixture");QVERIFY(bus.unregisterService("org.fcitx.Fcitx5"));QTRY_VERIFY(client.inputMethodNames().isEmpty());QVERIFY(client.connected());service.nativeName="新拼音";QVERIFY(bus.registerService("org.fcitx.Fcitx5"));QTRY_COMPARE(client.inputMethodNames().value("pinyin").toString(),QString("新拼音"));service.nativeName="拼音";}
 void delayedOldMetadataCannotOverwriteNewOwner(){service.calls=0;service.failures=0;service.nativeName="STALE";service.delay=600;FcitxClient client;QTRY_COMPARE(service.calls,1);auto bus=QDBusConnection("metadata-fixture");QVERIFY(bus.unregisterService("org.fcitx.Fcitx5"));QTest::qWait(30);service.delay=0;service.nativeName="新拼音";QVERIFY(bus.registerService("org.fcitx.Fcitx5"));QTRY_COMPARE(client.inputMethodNames().value("pinyin").toString(),QString("新拼音"));QTest::qWait(700);QCOMPARE(client.inputMethodNames().value("pinyin").toString(),QString("新拼音"));service.nativeName="拼音";}
 void sameOwnerSnapshotFailureRecovers(){service.calls=0;service.failures=0;service.snapshotFailures=1;FcitxClient client;QTRY_VERIFY_WITH_TIMEOUT(client.editable(),3500);}
 void selectAndPageTransportErrorsInvalidateBackend(){service.calls=0;service.failures=0;service.words=true;for(auto action:{QString("SelectCandidate"),QString("RequestPage")}){service.failAction=action;FcitxClient client;QTRY_VERIFY(client.editable());if(action=="SelectCandidate")client.selectCandidate(0,client.contextEpoch(),client.candidateEpoch());else client.requestPage(true);QTRY_VERIFY(!client.connected());}service.words=false;service.failAction.clear();}
 void changedContextReleasesPagingAndOldReplyCannotReleaseNewRequest(){
  service.calls=0;service.failures=0;service.words=true;service.holdPages=true;
  FcitxClient client;QTRY_VERIFY(client.editable());client.requestPage(true);QTRY_COMPARE(service.pages.size(),1);QVERIFY(client.paging());
  ++service.currentContext;service.changeInputMethod("pinyin");QTRY_COMPARE(client.contextEpoch(),service.currentContext);QVERIFY(!client.paging());
  client.requestPage(true);QTRY_COMPARE(service.pages.size(),2);QVERIFY(client.paging());
  auto bus=QDBusConnection("metadata-fixture");bus.send(service.pages[0].createReply(QVariantList{true}));QTest::qWait(50);QVERIFY(client.paging());
  bus.send(service.pages[1].createReply(QVariantList{true}));QTRY_VERIFY(!client.paging());
 }
 void changedMethodListRejectsInFlightOldMetadata(){
  service.calls=0;service.failures=0;service.delay=500;FcitxClient client;QTRY_COMPARE(service.calls,1);
  service.extended=true;service.delay=0;client.refresh();QTRY_COMPARE(client.inputMethods().size(),5);QTRY_COMPARE(client.inputMethodNames().size(),5);
  QTest::qWait(600);QCOMPARE(client.inputMethodNames().size(),5);
 }
 void cleanupTestCase(){auto bus=QDBusConnection("metadata-fixture");bus.unregisterObject("/controller");bus.unregisterObject("/org/agino/Keyboard1");bus.unregisterService("org.fcitx.Fcitx5");bus.unregisterService("org.agino.Keyboard1");}
};
QTEST_GUILESS_MAIN(MetadataTest)
#include "test_fcitxmetadata.moc"
