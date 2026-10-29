// SPDX-License-Identifier: GPL-3.0-or-later
// Integration tests: real NativePanel/QWindow events, real FcitxClient over a
// private session bus, no real input method, desktop or user settings.
#include <QtTest>
#include <QDBusVirtualObject>
#include <QDBusConnection>
#include <QMouseEvent>
#include <QDBusArgument>
#include <QDBusMetaType>
#include "native/nativepanel.h"

struct NativeMethodInfo { QString id,name,nativeName,icon,label,language; bool configurable; };
Q_DECLARE_METATYPE(NativeMethodInfo)
QDBusArgument &operator<<(QDBusArgument &a,const NativeMethodInfo &m){a.beginStructure();a<<m.id<<m.name<<m.nativeName<<m.icon<<m.label<<m.language<<m.configurable;a.endStructure();return a;}
const QDBusArgument &operator>>(const QDBusArgument &a,NativeMethodInfo &m){a.beginStructure();a>>m.id>>m.name>>m.nativeName>>m.icon>>m.label>>m.language>>m.configurable;a.endStructure();return a;}

class NativePanelService final : public QDBusVirtualObject {
public:
 quint64 context=1,generation=1;
 QStringList words;
 int pages=0,selections=0;
 bool holdPages=false;
 QList<QDBusMessage> pendingPages;
 QList<QVariantList> sentKeys;
 QString introspect(const QString &)const override{return {};}
 QVariantList snapshot()const{return {QVariant::fromValue(context),QVariant::fromValue(generation),true,false,QString(),QStringLiteral("keyboard-us"),words,false,!words.isEmpty(),QString(),0,0};}
 bool handleMessage(const QDBusMessage &m,const QDBusConnection &bus)override{
  if(m.member()=="AvailableInputMethods"){
   QDBusArgument methods;methods.beginArray(qMetaTypeId<NativeMethodInfo>());methods<<NativeMethodInfo{"keyboard-us","Keyboard - English (US)",{},"input-keyboard","en","en",true};methods.endArray();
   bus.send(m.createReply(QVariantList{QVariant::fromValue(methods)}));
  }else if(m.member()=="GetSnapshot")bus.send(m.createReply(snapshot()));
  else if(m.member()=="InputMethods")bus.send(m.createReply(QVariantList{QStringList{"keyboard-us"}}));
  else if(m.member()=="SelectCandidate"){++selections;bus.send(m.createReply(QVariantList{true}));}
  else if(m.member()=="RequestPage"){++pages;if(holdPages)pendingPages.append(m);else bus.send(m.createReply(QVariantList{true}));}
  else if(m.member()=="SendKey"){sentKeys.append(m.arguments());bus.send(m.createReply());}
  else bus.send(m.createReply());
  return true;
 }
 void publish(){auto m=QDBusMessage::createSignal("/org/agino/Keyboard1","org.agino.Keyboard1","SnapshotChanged");m.setArguments(snapshot());QDBusConnection("native-panel-fixture").send(m);}
 void finishPage(bool accepted){Q_ASSERT(!pendingPages.isEmpty());QDBusConnection("native-panel-fixture").send(pendingPages.takeFirst().createReply(QVariantList{accepted}));}
};

class NativePanelInteractionTest final : public QObject {
 Q_OBJECT
 NativePanelService service;
 QPointingDevice *device=nullptr;
 struct Harness {
  Preferences prefs;FcitxClient client;KeyboardState state;std::unique_ptr<NativePanel> panel;
  Harness(){QObject::connect(&client,&FcitxClient::changed,&state,[this]{state.setContext(client.contextEpoch(),client.editable()&&!state.settingsOpen(),client.sensitive());});QObject::connect(&state,&KeyboardState::keyRequested,&client,[this](KeySpec key,bool release){client.sendKey(key,release,client.contextEpoch());});}
 };
 bool prepare(Harness &h){
  if(!QTest::qWaitFor([&]{return h.client.editable();},2500))return false;
  h.panel=std::make_unique<NativePanel>(h.prefs,h.client,h.state);
  h.panel->resize(934,344);h.panel->show();h.state.manualShow();QCoreApplication::processEvents();h.panel->grabWindow();
  return !h.panel->accessibleControls().isEmpty();
 }
 static NativeControl control(NativePanel &panel,const QString &action,quint32 sym=0){
  for(const auto &c:panel.accessibleControls())if((!action.isEmpty()&&c.action==action)||(sym&&c.sym==sym))return c;
  return {};
 }
 static QAccessibleInterface *accessible(NativePanel &panel,const QString &id){
  const auto controls=panel.accessibleControls();auto *root=QAccessible::queryAccessibleInterface(&panel);
  for(int index=0;index<controls.size();++index)if(controls[index].id==id)return root->child(index);
  return nullptr;
 }
 static void mouse(NativePanel &panel,QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons){
  QMouseEvent event(type,point,point,panel.mapToGlobal(point),button,buttons,Qt::NoModifier);QCoreApplication::sendEvent(&panel,&event);
 }
private slots:
 void initTestCase(){
  qDBusRegisterMetaType<NativeMethodInfo>();
  auto bus=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"native-panel-fixture");
  QVERIFY(bus.registerService("org.agino.Keyboard1"));QVERIFY(bus.registerVirtualObject("/org/agino/Keyboard1",&service));
  QVERIFY(bus.registerService("org.fcitx.Fcitx5"));QVERIFY(bus.registerVirtualObject("/controller",&service));
  device=QTest::createTouchDevice();
 }
 void init(){QSettings().clear();++service.context;service.generation=1;service.words.clear();service.pages=service.selections=0;service.holdPages=false;service.pendingPages.clear();service.sentKeys.clear();}
 void dragOwnerCannotBeStolenAndPeersCannotCommit(){
  Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;
  const QPoint a=control(panel,{},'a').rect.center().toPoint(),e=control(panel,{},'e').rect.center().toPoint();QVERIFY(!a.isNull()&&!e.isNull());
  QSignalSpy moves(&panel,&NativePanel::moveRequested),keys(&h.state,&KeyboardState::keyRequested);
  auto touch=QTest::touchEvent(&panel,device,false);touch.press(1,a,&panel).press(2,e,&panel).commit();
  touch.move(1,a+QPoint(20,0),&panel).stationary(2).commit();QCOMPARE(moves.size(),1);
  touch.stationary(1).move(2,e+QPoint(40,0),&panel).commit();QCOMPARE(moves.size(),1);
  touch.release(1,a+QPoint(20,0),&panel).stationary(2).commit();
  touch.release(2,e,&panel).commit();QCOMPARE(keys.size(),0);
 }
 void staleCandidateGestureCannotPageNewContext(){
  service.words={QStringLiteral("候选")};Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;
  const auto word=control(panel,"candidate");QVERIFY(!word.id.isEmpty());auto *iface=accessible(panel,word.id);QVERIFY(iface);const auto accessibleId=QAccessible::uniqueId(iface);
  const QPoint point=word.rect.center().toPoint();auto touch=QTest::touchEvent(&panel,device,false);
  touch.press(3,point,&panel).commit();touch.move(3,point-QPoint(20,0),&panel).commit();
  ++service.context;service.publish();QTRY_COMPARE(h.client.contextEpoch(),service.context);
  if(auto *old=QAccessible::accessibleInterface(accessibleId))old->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
  touch.release(3,point-QPoint(20,0),&panel).commit();QTest::qWait(30);
  QCOMPARE(service.pages,0);QCOMPARE(service.selections,0);
 }
 void candidateGenerationRejectsOldAccessibleAction(){
  service.words={QStringLiteral("原候选")};Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;
  const auto word=control(panel,"candidate");auto *iface=accessible(panel,word.id);QVERIFY(iface);const auto id=QAccessible::uniqueId(iface);
  ++service.generation;service.words={QStringLiteral("新候选")};service.publish();QTRY_COMPARE(h.client.candidateEpoch(),service.generation);
  if(auto *old=QAccessible::accessibleInterface(id))old->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
  QTest::qWait(30);QCOMPARE(service.selections,0);
  panel.grabWindow();const auto fresh=control(panel,"candidate");QVERIFY(fresh.id!=word.id);QVERIFY(panel.invokeAccessible(fresh.id,QAccessibleActionInterface::pressAction()));QTRY_COMPARE(service.selections,1);
 }
 void expandedPagingKeepsSuccessfulPageAndClosesRejectedPage(){
  service.words={QStringLiteral("第一页")};service.holdPages=true;Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;
  panel.setExpanded(true);panel.grabWindow();QVERIFY(panel.expanded());
  auto flow=control(panel,"scroll/expanded");QVERIFY(!flow.id.isEmpty());
  QVERIFY(panel.invokeAccessible(flow.id,QAccessibleActionInterface::scrollDownAction()));QTRY_COMPARE(service.pages,1);QVERIFY(h.client.paging());
  ++service.generation;service.words={QStringLiteral("第二页")};service.publish();QTRY_COMPARE(h.client.candidateEpoch(),service.generation);
  QVERIFY(panel.expanded());service.finishPage(true);QTRY_VERIFY(!h.client.paging());QVERIFY(panel.expanded());
  panel.grabWindow();flow=control(panel,"scroll/expanded");QVERIFY(!flow.id.isEmpty());
  QVERIFY(panel.invokeAccessible(flow.id,QAccessibleActionInterface::scrollDownAction()));QTRY_COMPARE(service.pages,2);
  service.finishPage(false);QTRY_VERIFY(!h.client.paging());QVERIFY(!panel.expanded());
 }
 void repeatedSliderActionsUseCurrentValueAndExpireWithView(){
  Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;h.state.setSettingsOpen(true);h.prefs.setOpacityPercent(80);panel.grabWindow();
  const auto slider=control(panel,"settings/opacity");QVERIFY(!slider.id.isEmpty());auto *iface=accessible(panel,slider.id);QVERIFY(iface&&iface->valueInterface());const auto accessibleId=QAccessible::uniqueId(iface);
  iface->actionInterface()->doAction(QAccessibleActionInterface::decreaseAction());iface->actionInterface()->doAction(QAccessibleActionInterface::decreaseAction());QCOMPARE(h.prefs.opacityPercent(),78);
  QCOMPARE(iface->valueInterface()->currentValue().toInt(),78);
  h.state.setSettingsOpen(false);
  if(auto *retained=QAccessible::accessibleInterface(accessibleId))retained->actionInterface()->doAction(QAccessibleActionInterface::increaseAction());
  QVERIFY(!panel.invokeAccessible(slider.id,QStringLiteral("setValue:90")));QCOMPARE(h.prefs.opacityPercent(),78);
  h.state.setSettingsOpen(true);panel.grabWindow();const auto reopened=control(panel,"settings/opacity");QVERIFY(!reopened.id.isEmpty());panel.setTransparencySupported(false);
  QVERIFY(!panel.invokeAccessible(reopened.id,QStringLiteral("setValue:90")));QCOMPARE(h.prefs.opacityPercent(),78);
 }
 void sliderPreferenceReentryCanCancelContact(){
  Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;h.state.setSettingsOpen(true);panel.grabWindow();const auto slider=control(panel,"settings/opacity");QVERIFY(!slider.id.isEmpty());
  bool armed=false;connect(&h.prefs,&Preferences::changed,&h.state,[&]{if(armed)h.state.dismiss();});
  const QPointF start=slider.rect.center();mouse(panel,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
  armed=true;mouse(panel,QEvent::MouseMove,start-QPointF(20,0),Qt::NoButton,Qt::LeftButton);
  QVERIFY(!h.state.visible());mouse(panel,QEvent::MouseButtonRelease,start,Qt::LeftButton,Qt::NoButton);
  QVERIFY(h.prefs.opacityPercent()>=50&&h.prefs.opacityPercent()<=100);
 }
 // Moving the window changes nothing inside it: once a drag has started (one repaint shows the keys released), the
 // following moves repaint nothing, so the X server and the compositor do not receive the whole window again per event.
 void movingWindowDoesNotRepaint(){
  Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;const auto key=control(panel,{},'a');QVERIFY(!key.id.isEmpty());
  struct Updates:QObject{int count=0;bool eventFilter(QObject*,QEvent *e)override{if(e->type()==QEvent::UpdateRequest)++count;return false;}}updates;
  panel.installEventFilter(&updates);
  QSignalSpy moves(&panel,&NativePanel::moveRequested);
  connect(&panel,&NativePanel::moveRequested,&panel,[&](double dx,double dy){panel.setPosition(panel.position()+QPoint(qRound(dx),qRound(dy)));});
  const QPointF start=key.rect.center();QPointF pointer=panel.mapToGlobal(start);
  auto move=[&](QPointF step){pointer+=step;const QPointF local=panel.mapFromGlobal(pointer);QMouseEvent e(QEvent::MouseMove,local,local,pointer,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(&panel,&e);};
  mouse(panel,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
  move({20,0});QCOMPARE(moves.size(),1);QTRY_VERIFY(updates.count>0);QTest::qWait(50);
  updates.count=0;
  for(int i=0;i<30;++i){move({2,1});QTest::qWait(2);}
  QTest::qWait(50);QCOMPARE(moves.size(),31);QCOMPARE(updates.count,0);
  const QPointF local=panel.mapFromGlobal(pointer);mouse(panel,QEvent::MouseButtonRelease,local,Qt::LeftButton,Qt::NoButton);
  QTRY_VERIFY(updates.count>0);
 }
 void moveSignalReentryCanCancelContact(){
  Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;const auto key=control(panel,{},'a');QVERIFY(!key.id.isEmpty());
  connect(&panel,&NativePanel::moveRequested,&h.state,[&](double,double){h.state.dismiss();});
  const QPointF start=key.rect.center();mouse(panel,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
  mouse(panel,QEvent::MouseMove,start+QPointF(20,0),Qt::NoButton,Qt::LeftButton);
  QVERIFY(!h.state.visible());mouse(panel,QEvent::MouseButtonRelease,start+QPointF(20,0),Qt::LeftButton,Qt::NoButton);
 }
 void releasingSecondFingerKeepsFirstLongPressDeadline(){
  Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;const QPoint a=control(panel,{},'a').rect.center().toPoint(),e=control(panel,{},'e').rect.center().toPoint();
  auto touch=QTest::touchEvent(&panel,device,false);touch.press(4,a,&panel).commit();QTest::qWait(100);
  touch.stationary(4).press(5,e,&panel).commit();touch.stationary(4).release(5,e,&panel).commit();QTest::qWait(650);panel.grabWindow();
  QVERIFY(!control(panel,"alternative").id.isEmpty());touch.release(4,a,&panel).commit();
 }
 void layoutInvalidationCannotHitPreviousGeometry_data(){
  QTest::addColumn<bool>("compact");QTest::newRow("compact")<<true;QTest::newRow("function-row")<<false;
 }
 void layoutInvalidationCannotHitPreviousGeometry(){
  QFETCH(bool,compact);Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;
  const auto key=control(panel,{},'a');QVERIFY(!key.id.isEmpty());QSignalSpy keys(&h.state,&KeyboardState::keyRequested);
  if(compact)h.prefs.setCompact(true);else h.prefs.setFunctionRow(true);
  mouse(panel,QEvent::MouseButtonPress,key.rect.center(),Qt::LeftButton,Qt::LeftButton);
  mouse(panel,QEvent::MouseButtonRelease,key.rect.center(),Qt::LeftButton,Qt::NoButton);
  QCOMPARE(keys.size(),0);
 }
 void coveredPhysicalKeyCannotStartBeforeExpandedRepaint(){
  service.words={QStringLiteral("候选")};Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;
  const auto key=control(panel,{},'a');QVERIFY(!key.id.isEmpty());
  QSignalSpy keys(&h.state,&KeyboardState::keyRequested);
  panel.setExpanded(true);
  mouse(panel,QEvent::MouseButtonPress,key.rect.center(),Qt::LeftButton,Qt::LeftButton);
  mouse(panel,QEvent::MouseButtonRelease,key.rect.center(),Qt::LeftButton,Qt::NoButton);
  QCOMPARE(keys.size(),0);
 }
 void alternativePopupExpiresAcrossEditableContexts_data(){
  QTest::addColumn<bool>("contextChanges");
  QTest::newRow("editable-focus-change")<<true;
  QTest::newRow("candidate-generation-change")<<false;
 }
 void alternativePopupExpiresAcrossEditableContexts(){
  QFETCH(bool,contextChanges);
  Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;
  const QPoint a=control(panel,{},'a').rect.center().toPoint();
  mouse(panel,QEvent::MouseButtonPress,a,Qt::LeftButton,Qt::LeftButton);
  QTest::qWait(650);panel.grabWindow();
  const auto old=control(panel,"alternative");QVERIFY(!old.id.isEmpty());
  if(contextChanges)++service.context;else ++service.generation;service.publish();
  QTRY_COMPARE(h.client.contextEpoch(),service.context);QTRY_COMPARE(h.client.candidateEpoch(),service.generation);
  panel.grabWindow();QVERIFY(control(panel,"alternative").id.isEmpty());
  QVERIFY(!panel.invokeAccessible(old.id,QAccessibleActionInterface::pressAction()));
  mouse(panel,QEvent::MouseButtonRelease,old.rect.center(),Qt::LeftButton,Qt::NoButton);
  QTest::qWait(30);QCOMPARE(service.sentKeys.size(),0);
 }
 void longPressApproachCorridorDoesNotMoveWindow(){
  Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;const auto key=control(panel,{},'a');QVERIFY(!key.id.isEmpty());
  const QPoint start=key.rect.center().toPoint();QSignalSpy moves(&panel,&NativePanel::moveRequested),keys(&h.state,&KeyboardState::keyRequested);
  auto touch=QTest::touchEvent(&panel,device,false);touch.press(6,start,&panel).commit();QTest::qWait(650);panel.grabWindow();
  const auto alternative=control(panel,"alternative");QVERIFY(!alternative.id.isEmpty());
  // The QML implementation permits travel from the source key through the
  // corridor below its popup, even after the16px window-drag threshold.
  const QPoint corridor(start.x(),qRound(key.rect.top()+4));
  touch.move(6,corridor,&panel).commit();QCOMPARE(moves.size(),0);
  panel.grabWindow();QVERIFY(!control(panel,"alternative").id.isEmpty());
  touch.move(6,alternative.rect.center().toPoint(),&panel).commit();touch.release(6,alternative.rect.center().toPoint(),&panel).commit();
  QCOMPARE(keys.size(),2);const auto cp=alternative.value.toString().toUcs4();QVERIFY(!cp.isEmpty());QCOMPARE(qvariant_cast<KeySpec>(keys[0][0]).sym,quint32(0x1000000+cp[0]));
 }
 void dismissedAlternativeCannotBeInvokedBeforeRepaint(){
  Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;const auto key=control(panel,{},'a');QVERIFY(!key.id.isEmpty());
  const QPointF start=key.rect.center();QSignalSpy keys(&h.state,&KeyboardState::keyRequested);
  mouse(panel,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);QTest::qWait(650);panel.grabWindow();
  const auto alternative=control(panel,"alternative");QVERIFY(!alternative.id.isEmpty());
  // Release outside the popup: it disappears synchronously, before the next
  // frame refreshes the painted control metadata.
  mouse(panel,QEvent::MouseButtonRelease,start,Qt::LeftButton,Qt::NoButton);
  QVERIFY(!panel.invokeAccessible(alternative.id,QAccessibleActionInterface::pressAction()));QCOMPARE(keys.size(),0);
 }
 void layoutChangeCancelsPressedSettingGeometry(){
  Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;h.state.setSettingsOpen(true);panel.grabWindow();
  const auto sound=control(panel,"soundSetting");QVERIFY(!sound.id.isEmpty());QVERIFY(!h.prefs.keySound());
  mouse(panel,QEvent::MouseButtonPress,sound.rect.center(),Qt::LeftButton,Qt::LeftButton);
  h.prefs.setCompact(true);panel.resize(524,208);QCoreApplication::processEvents();panel.grabWindow();
  const auto relocated=control(panel,"soundSetting");QVERIFY(!relocated.id.isEmpty());QVERIFY(!relocated.rect.contains(sound.rect.center()));
  mouse(panel,QEvent::MouseButtonRelease,sound.rect.center(),Qt::LeftButton,Qt::NoButton);
  QVERIFY(!h.prefs.keySound());
 }
 void shorterExpandedPageIsVisibleOnItsFirstFrame(){
  for(int n=0;n<100;++n)service.words.append(QStringLiteral("候选词%1").arg(n));service.holdPages=true;
  Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;panel.setExpanded(true);panel.grabWindow();
  for(int n=0;n<20&&!h.client.paging();++n){const auto flow=control(panel,"scroll/expanded");QVERIFY(!flow.id.isEmpty());QVERIFY(panel.invokeAccessible(flow.id,QAccessibleActionInterface::scrollDownAction()));panel.grabWindow();}
  QTRY_COMPARE(service.pages,1);QVERIFY(h.client.paging());
  bool rendered=false,visible=false;
  connect(&h.client,&FcitxClient::changed,&panel,[&]{if(h.client.candidateEpoch()!=2||rendered)return;panel.grabWindow();rendered=true;for(const auto &c:panel.accessibleControls())if(c.action=="candidate"&&c.name==QStringLiteral("末页候选")&&c.enabled)visible=true;});
  service.generation=2;service.words={QStringLiteral("末页候选")};service.publish();QTRY_VERIFY(rendered);QVERIFY(panel.expanded());QVERIFY(visible);
  service.finishPage(true);QTRY_VERIFY(!h.client.paging());
 }
 void idleRetainsPixelsRowsAndInput(){
  service.words={QStringLiteral("中文候选"),QStringLiteral("مرحبا"),QStringLiteral("Emoji🙂"),QStringLiteral("Latin café")};
  Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;QTRY_VERIFY(!h.client.inputMethodNames().isEmpty());
  panel.setExpanded(true);const QImage before=panel.grabWindow();QVERIFY(!before.isNull());
  // Pausing longer than the former cleanup interval must preserve pixels,
  // candidate rows, regular key input, long press and settings actions.
  QTest::qWait(1300);const QImage after=panel.grabWindow();QCOMPARE(after,before);
  QVERIFY(!control(panel,"candidate").id.isEmpty());
  panel.setExpanded(false);panel.grabWindow();QSignalSpy keys(&h.state,&KeyboardState::keyRequested);
  const auto key=control(panel,{},'a');QVERIFY(!key.id.isEmpty());
  mouse(panel,QEvent::MouseButtonPress,key.rect.center(),Qt::LeftButton,Qt::LeftButton);mouse(panel,QEvent::MouseButtonRelease,key.rect.center(),Qt::LeftButton,Qt::NoButton);
  QCOMPARE(keys.size(),2);QCOMPARE(qvariant_cast<KeySpec>(keys[0][0]).sym,quint32('a'));keys.clear();
  QTRY_COMPARE(service.sentKeys.size(),2);QCOMPARE(service.sentKeys[0][0].toUInt(),quint32('a'));QCOMPARE(service.sentKeys[0][3].toBool(),false);QCOMPARE(service.sentKeys[1][3].toBool(),true);
  mouse(panel,QEvent::MouseButtonPress,key.rect.center(),Qt::LeftButton,Qt::LeftButton);QTest::qWait(650);panel.grabWindow();
  const auto alternative=control(panel,"alternative");QVERIFY(!alternative.id.isEmpty());
  mouse(panel,QEvent::MouseButtonRelease,alternative.rect.center(),Qt::LeftButton,Qt::NoButton);QCOMPARE(keys.size(),2);
  const auto cp=alternative.value.toString().toUcs4();QVERIFY(!cp.isEmpty());QCOMPARE(qvariant_cast<KeySpec>(keys[0][0]).sym,quint32(0x1000000+cp[0]));
  QTRY_COMPARE(service.sentKeys.size(),4);QCOMPARE(service.sentKeys[2][0].toUInt(),quint32(0x1000000+cp[0]));QCOMPARE(service.sentKeys[3][3].toBool(),true);
  h.state.setSettingsOpen(true);h.prefs.setOpacityPercent(80);const QImage settingsBefore=panel.grabWindow();
  QTest::qWait(1300);QCOMPARE(panel.grabWindow(),settingsBefore);
  const auto slider=control(panel,"settings/opacity");QVERIFY(!slider.id.isEmpty());QVERIFY(panel.invokeAccessible(slider.id,QAccessibleActionInterface::decreaseAction()));QCOMPARE(h.prefs.opacityPercent(),79);
 }
 void unseenCandidatesCannotBePagedPastBeforeLayout(){
  for(int i=0;i<100;++i)service.words.append(QStringLiteral("尚未浏览的长候选词%1").arg(i));
  Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;
  for(bool expanded:{false,true}){
   panel.setExpanded(expanded);panel.grabWindow();const auto list=control(panel,expanded?"scroll/expanded":"scroll/candidates");QVERIFY(!list.id.isEmpty());
   for(int step=0;step<100;++step)QVERIFY(panel.invokeAccessible(list.id,expanded?QAccessibleActionInterface::scrollDownAction():QAccessibleActionInterface::scrollRightAction()));
   QTest::qWait(20);QCOMPARE(service.pages,0);
   // Real paints must eventually make every row available and allow paging.
   for(int step=0;step<100&&service.pages==0;++step){panel.grabWindow();QVERIFY(panel.invokeAccessible(list.id,expanded?QAccessibleActionInterface::scrollDownAction():QAccessibleActionInterface::scrollRightAction()));QCoreApplication::processEvents();}
   QTRY_VERIFY(service.pages>0);QTRY_VERIFY(!h.client.paging());service.pages=0;
  }
 }
 void expandedHeaderSwipeKeepsItsOwnHorizontalScroll(){
  for(int i=0;i<120;++i)service.words.append(QStringLiteral("候选词%1").arg(i));
  Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;panel.setExpanded(true);const auto before=panel.grabWindow();
  const QPoint start(400,24);auto touch=QTest::touchEvent(&panel,device,false);touch.press(7,start,&panel).commit();touch.move(7,start-QPoint(120,0),&panel).commit();touch.release(7,start-QPoint(120,0),&panel).commit();const auto after=panel.grabWindow();
  QVERIFY(before.copy(40,0,800,48)!=after.copy(40,0,800,48));QCOMPARE(before.copy(12,56,910,276),after.copy(12,56,910,276));QCOMPARE(service.selections,0);
 }
 void wheelOverKeysCannotScrollCandidateHeader(){
  for(int i=0;i<60;++i)service.words.append(QStringLiteral("候选%1").arg(i));Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;const auto before=panel.grabWindow();
  const QPointF pos(400,150);QWheelEvent wheel(pos,panel.mapToGlobal(pos),{},QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(&panel,&wheel);QCOMPARE(panel.grabWindow().copy(40,0,800,48),before.copy(40,0,800,48));
 }
 void expandedHeaderWheelDoesNotScrollExpandedRows(){
  for(int i=0;i<120;++i)service.words.append(QStringLiteral("候选词%1").arg(i));Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;panel.setExpanded(true);const auto before=panel.grabWindow();
  const QPointF pos(400,24);QWheelEvent wheel(pos,panel.mapToGlobal(pos),{},QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(&panel,&wheel);const auto after=panel.grabWindow();QVERIFY(before.copy(40,0,800,48)!=after.copy(40,0,800,48));QCOMPARE(before.copy(12,56,910,276),after.copy(12,56,910,276));
 }
 void viewSwitchExpiresHiddenAccessibleControlsImmediately(){
  service.words={QStringLiteral("候选词")};Harness h;QVERIFY(prepare(h));auto &panel=*h.panel;const auto letter=control(panel,{},'a');QVERIFY(!letter.id.isEmpty());panel.setExpanded(true);QVERIFY(!panel.invokeAccessible(letter.id,QAccessibleActionInterface::pressAction()));
  panel.grabWindow();NativeControl expandedWord;for(const auto &c:panel.accessibleControls())if(c.id.startsWith("expandedCandidate-")){expandedWord=c;break;}QVERIFY(!expandedWord.id.isEmpty());panel.setExpanded(false);QVERIFY(!panel.invokeAccessible(expandedWord.id,QAccessibleActionInterface::pressAction()));QTest::qWait(20);QCOMPARE(service.selections,0);
 }
 void cleanupTestCase(){delete device;device=nullptr;auto bus=QDBusConnection("native-panel-fixture");bus.unregisterObject("/controller");bus.unregisterService("org.fcitx.Fcitx5");bus.unregisterObject("/org/agino/Keyboard1");bus.unregisterService("org.agino.Keyboard1");}
};
QTEST_MAIN(NativePanelInteractionTest)
#include "test_nativepanel_interaction.moc"
