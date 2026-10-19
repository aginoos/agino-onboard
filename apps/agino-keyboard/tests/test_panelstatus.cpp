#include <QtTest>
#include "panelstatus.h"
// The typed state of the keyboard window: the D-Bus keys of Status() and the handoff to the next window (--resume-json).
static PanelStatus sample(){
 PanelStatus s;s.capsLock=true;s.modifiers=2;s.shortcutError=QStringLiteral("busy");s.inputMethodName=QStringLiteral("Pinyin");s.inputMethodStatus=QStringLiteral("拼");s.inputLanguageCode=QStringLiteral("zh");
 s.methodsCount=3;s.epoch=7;s.generation=9;s.connected=true;s.editable=true;s.sensitive=false;s.expanded=true;s.transparencySupported=true;
 s.panelX=-12;s.panelY=34;s.panelWidth=934;s.panelHeight=344;s.visible=true;s.settingsVisible=true;s.panelLoaded=true;s.viewCreated=true;
 s.showSerial=5;s.firstFrameSerial=4;s.firstFrameMs=12.5;s.graphicsApi=1;s.renderer=QStringLiteral("qtgui-raster");s.platform=QStringLiteral("xcb");s.automatic=true;s.desktop=QStringLiteral("ok");
 s.floating=true;s.floatX=10.25;s.floatY=-3.5;s.outputName=QStringLiteral("HDMI-1");
 s.resuming=true;s.applyPreferred=true;s.sourceKind=2;s.sourceX=100.5;s.sourceY=200.25;
 return s;
}
class PanelStatusTest:public QObject {
 Q_OBJECT
private slots:
 void defaults(){
  const PanelStatus s;
  QVERIFY(!s.visible&&!s.settingsVisible&&!s.resuming&&!s.applyPreferred);QCOMPARE(s.firstFrameMs,-1.);QCOMPARE(s.showSerial,quint64(0));QVERIFY(s.inputMethodName.isEmpty());
  QCOMPARE(PanelStatus::fromVariantMap({}).toVariantMap(true),s.toVariantMap(true));
 }
 void statusKeysAndTypes(){
  // Exactly the keys (and D-Bus types) the controller and tests/process-lifecycle.py read; the handoff keys are not part of Status().
  const QVariantMap map=sample().toVariantMap();
  const QStringList keys{"capsLock","modifiers","shortcutError","inputMethodName","inputMethodStatus","inputLanguageCode","methodsCount","epoch","generation","connected","editable","sensitive","expanded","transparencySupported",
   "panelX","panelY","panelWidth","panelHeight","visible","settingsVisible","panelLoaded","viewCreated","showSerial","firstFrameSerial","firstFrameMs","graphicsApi","renderer","platform","automatic","desktop","floating","floatX","floatY","outputName"};
  QCOMPARE(QStringList(map.keys()),[&]{auto sorted=keys;sorted.sort();return sorted;}());
  QCOMPARE(map.value("epoch").typeId(),int(QMetaType::ULongLong));QCOMPARE(map.value("modifiers").typeId(),int(QMetaType::UInt));QCOMPARE(map.value("methodsCount").typeId(),int(QMetaType::LongLong));
  QCOMPARE(map.value("panelX").typeId(),int(QMetaType::Int));QCOMPARE(map.value("firstFrameMs").typeId(),int(QMetaType::Double));QCOMPARE(map.value("visible").typeId(),int(QMetaType::Bool));
  QVERIFY(!map.contains("resuming")&&!map.contains("sourceKind"));
  const QVariantMap handoff=sample().toVariantMap(true);
  QCOMPARE(handoff.size(),map.size()+5);QVERIFY(handoff.value("resuming").toBool());QCOMPARE(handoff.value("sourceKind").toInt(),2);
 }
 void mapRoundTrip(){
  const PanelStatus s=sample();
  const PanelStatus back=PanelStatus::fromVariantMap(s.toVariantMap(true));
  QCOMPARE(back.toVariantMap(true),s.toVariantMap(true));
  QCOMPARE(back.epoch,quint64(7));QCOMPARE(back.panelX,-12);QCOMPARE(back.firstFrameMs,12.5);QCOMPARE(back.outputName,QStringLiteral("HDMI-1"));QVERIFY(back.resuming&&back.applyPreferred);
  // Without the handoff flag the handoff keys stay at their defaults.
  const PanelStatus plain=PanelStatus::fromVariantMap(s.toVariantMap());
  QVERIFY(!plain.resuming&&!plain.applyPreferred);QCOMPARE(plain.sourceKind,0);QVERIFY(plain.visible&&plain.capsLock);
 }
 void jsonRoundTrip(){
  const PanelStatus s=sample();
  const QByteArray json=s.toJson();
  QVERIFY(!json.contains('\n'));QVERIFY(json.contains("\"floatX\""));
  const PanelStatus back=PanelStatus::fromJson(json);
  QCOMPARE(back.toVariantMap(true),s.toVariantMap(true));
 }
 void missingKeysKeepDefaults(){
  const PanelStatus s=PanelStatus::fromVariantMap({{"visible",true},{"showSerial",QVariant::fromValue(quint64(11))}});
  QVERIFY(s.visible);QCOMPARE(s.showSerial,quint64(11));QVERIFY(!s.settingsVisible);QCOMPARE(s.firstFrameMs,-1.);QCOMPARE(s.panelWidth,0);
  const PanelStatus fromJson=PanelStatus::fromJson(R"({"capsLock":true,"floating":true,"floatX":5})");
  QVERIFY(fromJson.capsLock&&fromJson.floating);QCOMPARE(fromJson.floatX,5.);QCOMPARE(fromJson.floatY,0.);QVERIFY(!fromJson.resuming);
 }
 void wrongTypesAreIgnored(){
  const PanelStatus s=PanelStatus::fromVariantMap({{"visible","yes"},{"capsLock",1},{"panelX","wide"},{"panelY",QVariant::fromValue(quint64(1)<<40)},{"showSerial",-1},{"epoch",1.5e30},{"modifiers",-4},
   {"firstFrameMs","fast"},{"inputMethodName",5},{"outputName",QVariantList{1,2}},{"floatX",QVariantMap{}},{"resuming",QStringLiteral("false")},{"sourceKind",QVariant()},{"methodsCount",true}});
  QVERIFY(!s.visible);QVERIFY(!s.capsLock);QCOMPARE(s.panelX,0);QCOMPARE(s.panelY,0);QCOMPARE(s.showSerial,quint64(0));QCOMPARE(s.epoch,quint64(0));QCOMPARE(s.modifiers,quint32(0));
  QCOMPARE(s.firstFrameMs,-1.);QVERIFY(s.inputMethodName.isEmpty());QVERIFY(s.outputName.isEmpty());QCOMPARE(s.floatX,0.);QVERIFY(!s.resuming);QCOMPARE(s.sourceKind,0);QCOMPARE(s.methodsCount,qint64(0));
  // Numbers of another integer width are accepted (a value from D-Bus or JSON need not have the type of the field).
  const PanelStatus wide=PanelStatus::fromVariantMap({{"epoch",3},{"panelX",QVariant::fromValue(qint64(-9))},{"floatY",4},{"showSerial",2.0}});
  QCOMPARE(wide.epoch,quint64(3));QCOMPARE(wide.panelX,-9);QCOMPARE(wide.floatY,4.);QCOMPARE(wide.showSerial,quint64(2));
 }
 void badJsonGivesDefaults(){
  const QVariantMap defaults=PanelStatus().toVariantMap(true);
  for(const char *text:{"","not json","[1,2]","42","null","{\"visible\":"})QCOMPARE(PanelStatus::fromJson(text).toVariantMap(true),defaults);
  QCOMPARE(PanelStatus::fromJson(R"({"visible":"x","panelX":{"a":1}})").toVariantMap(true),defaults);
 }
};
QTEST_APPLESS_MAIN(PanelStatusTest)
#include "test_panelstatus.moc"
