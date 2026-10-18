import org.deepin.dtk 1.0 as D
import QtQuick
import QtTest
import org.agino.keyboard 1.0
import "../../qml"
TestCase {
 id:test;visible:true;name:"KeyboardLayout";when:windowShown
 width:980;height:430
 KeyboardState {id:state}
 Preferences {id:prefs}
 SignalSpy {id:keys;target:state;signalName:"keyRequested"}
 SignalSpy {id:settings;target:panel;signalName:"settingsRequested"}
 SignalSpy {id:moves;target:panel;signalName:"moveRequested"}
 QtObject {id:backend;property bool editable:true;property string preedit:"";property var candidates:[];property double contextEpoch:1;property double candidateEpoch:1;property bool sensitive:false;property string status:"";property string inputMethodStatus:"中文：拼音";property string inputLanguageCode:"CN";property string inputMethod:"pinyin";property var inputMethods:["keyboard-us","pinyin"];property var inputMethodNames:({});property bool hasNext:false;property bool hasPrev:false;property bool paging:false;property int selectedIndex:-1;property string switchedTo:"";signal candidateSelected();signal changed();function selectCandidate(i,e,c){if(e!==contextEpoch||c!==candidateEpoch||sensitive||!editable)return;selectedIndex=i;candidateSelected()}function requestPage(next){}function setInputMethod(n){switchedTo=n;}}
 KeyboardPanel {id:panel;width:parent.width;height:430+(panel.functionRow?56:0);controller:state;input:backend;preferences:prefs;screenNames:["Display 1","Display 2"];onSettingsRequested:state.setSettingsOpen(true)}
 function init(){state.closePanel();state.setSettingsOpen(false);prefs.mode=0;prefs.functionRow=false;prefs.compact=false;prefs.opacityPercent=100;prefs.keySound=false;prefs.screenIndex=-1;prefs.theme=0;prefs.safeInput=false;prefs.shortcut="Ctrl+Alt+K";prefs.preferredIm="";panel.transparencySupported=true;panel.width=980;panel.height=Qt.binding(()=>430+(panel.functionRow?56:0));state.cancel();state.setContext(1,true,false);if(state.modifiers&2){state.press(-700,0xffe5);state.release(-700,true)}keys.clear();settings.clear();moves.clear();backend.selectedIndex=-1;backend.switchedTo="";backend.sensitive=false;backend.editable=true;panel.functionRow=false;backend.preedit="";backend.inputLanguageCode="CN";backend.inputMethodStatus="中文：拼音";backend.inputMethods=["keyboard-us","pinyin"];backend.inputMethodNames=({});backend.candidates=[];panel.expanded=false;wait(30)}

 function test_densitySettingsAndFixedKeySizes(){
  panel.requestSettings();wait(30);let overlay=findChild(panel,"settingsPanel");
  verify(findChild(overlay,"layoutSetting"),"Standard / compact selector missing");
  verify(!findChild(overlay,"heightSetting"),"Height ratio must be removed");
  mouseClick(findChild(overlay,"layoutSetting-1"));compare(prefs.compact,true);
  panel.width=524;panel.height=Qt.binding(()=>panel.minimumHeight);wait(30);
  compare(panel.height,208);compare(findChild(panel,"key-Q").height,32);compare(findChild(panel,"key-Q").width,32);
  let q=findChild(panel,"key-Q"),w=findChild(panel,"key-W");compare(w.x-q.x-q.width,2);
  for(let name of ["modeSetting","inputSetting","safeSetting","shortcutSetting","soundSetting","layoutSetting","functionSetting","screenSetting","themeSetting","opacitySetting"]){
   let c=findChild(overlay,name);verify(c,name);let point=c.mapToItem(overlay,0,0);verify(point.x>=0&&point.y>=0&&point.x+c.width<=overlay.width&&point.y+c.height<=overlay.height,name+" clipped");
  }
  verify(findChild(overlay,"modeSetting").mapToItem(overlay,0,0).x<260);
  verify(findChild(overlay,"layoutSetting").mapToItem(overlay,0,0).x>260,"Compact settings must use two columns");
  compare(prefs.keySound,false);mouseClick(findChild(overlay,"soundSetting"));compare(prefs.keySound,true);
  let slider=findChild(overlay,"opacitySetting");mouseClick(slider,slider.width/2,slider.height/2);verify(prefs.opacityPercent<100);verify(panel.opacity<1);
  prefs.reload();compare(prefs.compact,true);compare(prefs.keySound,true);
  mouseClick(findChild(overlay,"returnKeyboard"));wait(30);
  let h=panel.height;backend.candidates=["你","你好世界"];wait(30);compare(panel.height,h);compare(q.height,32);compare(findChild(panel,"candidateBar").height,32);
  let first=findChild(panel,"candidate-0"),second=findChild(panel,"candidate-1");verify(second.width>first.width);mouseClick(first);compare(backend.selectedIndex,0);
  backend.candidates=[];backend.changed();wait(30);mouseClick(q);compare(keys.count,2);
  panel.functionRow=true;wait(30);compare(panel.height,242);compare(findChild(panel,"key-Q").height,32);
  prefs.compact=false;panel.width=934;wait(30);compare(panel.height,400);compare(findChild(panel,"key-Q").height,48);
  panel.functionRow=false;wait(30);compare(panel.height,344);compare(findChild(panel,"key-Q").height,48);
 }
 function test_compactShortcutEditorScrollsWithoutResizing(){
  prefs.compact=true;panel.width=524;panel.height=208;panel.requestSettings();wait(30);let overlay=findChild(panel,"settingsPanel");
  mouseClick(findChild(overlay,"shortcutSetting"));wait(30);let editor=findChild(overlay,"shortcutEditor");verify(editor.contentHeight>editor.height);
  editor.contentY=editor.contentHeight-editor.height;wait(30);mouseClick(findChild(overlay,"shortcut-key-F6"));mouseClick(findChild(overlay,"saveShortcut"));compare(prefs.shortcut,"Ctrl+Alt+F6");compare(panel.width,524);compare(panel.height,208);compare(keys.count,0);
 }
 function test_compactTouchDragDoesNotType(){
  prefs.compact=true;panel.width=524;panel.height=208;wait(30);let key=findChild(panel,"key-Q"),touches=touchEvent(panel);
  touches.press(0,key,16,16).commit();touches.move(0,key,40,-4).commit();touches.release(0,key,40,-4).commit();wait(30);verify(moves.count>0);compare(keys.count,0);
  touches.press(0,key,16,16).commit();touches.release(0,key,16,16).commit();wait(30);compare(keys.count,2);
 }
 function test_transparencyUnavailableKeepsPanelOpaque(){
  prefs.opacityPercent=55;wait(30);compare(panel.opacity,0.55);panel.transparencySupported=false;wait(30);compare(panel.opacity,1);
  panel.requestSettings();wait(30);let slider=findChild(panel,"opacitySetting");verify(!slider.enabled);compare(prefs.opacityPercent,55);
  panel.transparencySupported=true;wait(30);verify(slider.enabled);compare(panel.opacity,0.55);
 }
 function test_candidateToolbarIconAndRightAlignedLanguage(){
  let bar=findChild(panel,"candidateBar"),icon=findChild(bar,"applicationIcon"),status=findChild(bar,"inputMethodStatus"),gear=findChild(bar,"settingsButton");
  verify(icon,"Application icon must appear at left");tryCompare(icon,"status",Image.Ready);verify(icon.x<32);compare(status.horizontalAlignment,Text.AlignRight);verify(status.x>bar.width/2);verify(status.x+status.width<=gear.x);
  backend.candidates=["你","你好世界"];wait(30);verify(icon.visible);verify(!status.visible);let first=findChild(bar,"candidate-0"),last=findChild(bar,"candidate-1");verify(first.mapToItem(bar,0,0).x>=icon.x+icon.width);verify(last.width>first.width);
  prefs.compact=true;panel.width=524;panel.height=208;wait(30);compare(status.text,"CN");backend.inputLanguageCode="EN";backend.inputMethodStatus="英文：英语（美国）";wait(30);compare(status.text,"EN");verify(status.x+status.width<=findChild(bar,"expandButton").x);compare(panel.height,208);
  backend.candidates=[];backend.changed();wait(30);verify(gear.visible);compare(status.text,"EN");verify(gear.iconScale<0.75);verify(gear.iconStrokeWidth<1.6);
 }
 function test_slintLatestCandidateStatusAndShiftLock(){
  let bar=findChild(panel,"candidateBar"),status=findChild(bar,"inputMethodStatus");backend.candidates=["你","你好"];wait(30);verify(!status.visible);compare(status.width,0);
  let shift=findChild(panel,"key-Shift-L"),digit=findChild(panel,"key-1");mouseClick(shift);wait(30);verify(shift.shiftActive);compare(shift.displayLabel,"Shift");compare(digit.displayLabel,"!");compare(digit.secondaryFontSize,12);compare(digit.secondaryOpacity,0.9);
  mouseClick(digit);wait(30);compare(digit.displayLabel,"1");mouseClick(shift);mouseClick(shift);wait(30);compare(shift.displayLabel,"↑ Shift");compare(digit.displayLabel,"!");verify(shift.lockedShift);mouseClick(digit);compare(digit.displayLabel,"!");
  prefs.compact=true;wait(30);compare(digit.secondaryFontSize,10);backend.candidates=[];wait(30);verify(status.visible);verify(status.width>0);
 }
 function test_slintOpacityMinimum(){prefs.opacityPercent=30;wait(30);compare(prefs.opacityPercent,50);compare(panel.opacity,0.5);panel.requestSettings();wait(30);compare(findChild(panel,"opacitySetting").from,50);}
 function test_slintAccessibleActionsAndPolicy(){state.setSettingsOpen(true);state.setSettingsOpen(false);let key=findChild(panel,"key-A");key.Accessible.pressAction();compare(keys.count,2);let gear=findChild(panel,"settingsButton");gear.Accessible.pressAction();verify(state.settingsOpen);compare(keys.count,2);let mode=findChild(panel,"modeSetting-1");mode.Accessible.pressAction();compare(prefs.mode,1);mouseClick(findChild(panel,"returnKeyboard"));wait(30);backend.candidates=["你","你好"];wait(30);let word=findChild(panel,"candidate-0");word.Accessible.pressAction();compare(backend.selectedIndex,0);backend.candidates=[];state.closePanel();keys.clear();key.Accessible.pressAction();compare(keys.count,0);}
 function test_slintOpenPickerUpdatesStableChoices(){panel.requestSettings();wait(30);let overlay=findChild(panel,"settingsPanel");mouseClick(findChild(overlay,"inputSetting"));backend.inputMethods=["pinyin","keyboard-us","keyboard-jp"];backend.inputMethodNames=({"keyboard-jp":"日语"});backend.changed();wait(30);let last=findChild(overlay,"settingChoice-2");verify(last);compare(findChild(last,"settingChoiceLabel").text,"日语");backend.inputMethods=["pinyin"];backend.changed();wait(30);verify(!findChild(overlay,"settingChoice-2"));mouseClick(findChild(overlay,"cancelPicker"));mouseClick(findChild(overlay,"screenSetting"));panel.screenNames=["A","B","C"];wait(30);verify(findChild(overlay,"settingChoice-3"));panel.screenNames=["测试屏幕 A","测试屏幕 B"];}
 function test_slintLanguageRejectsSensitiveAndNoTarget(){let globe=findChild(panel,"key-Language");backend.sensitive=true;mouseClick(globe);compare(backend.switchedTo,"");backend.sensitive=false;backend.editable=false;mouseClick(globe);compare(backend.switchedTo,"");backend.editable=true;mouseClick(globe);compare(backend.switchedTo,"keyboard-us");}
 function test_readerCandidateIdentityRefreshes(){state.setSettingsOpen(true);state.setSettingsOpen(false);backend.candidates=["你","你好"];wait(30);backend.candidateEpoch++;backend.changed();wait(30);findChild(panel,"candidate-0").Accessible.pressAction();compare(backend.selectedIndex,0);backend.selectedIndex=-1;panel.expanded=true;wait(30);backend.paging=true;backend.candidateEpoch++;backend.changed();wait(30);let word=findChild(panel,"expandedCandidate-0");verify(word);word.Accessible.pressAction();compare(backend.selectedIndex,0);backend.paging=false;}
 function test_readerOpacityChangesStoredValue(){prefs.opacityPercent=50;panel.requestSettings();wait(30);let slider=findChild(panel,"opacitySetting");slider.Accessible.increaseAction();compare(prefs.opacityPercent,51);slider.value=72;compare(prefs.opacityPercent,72);panel.transparencySupported=false;wait(30);slider.value=60;compare(prefs.opacityPercent,72);compare(slider.value,72);}
 function count(label){return panel.allKeys().filter(k=>k.label===label).length}
 function test_settingsEntryUsesCandidateRowActionSlot(){
  let bar=findChild(panel,"candidateBar"),gear=findChild(panel,"settingsButton");verify(gear,"Settings action must be in the candidate row");verify(gear.visible);compare(gear.parent,bar);verify(!findChild(panel,"key-Settings"));
  let center=gear.mapToItem(panel,gear.width/2,gear.height/2);compare(center.y,24);verify(center.x>panel.width-48);
  backend.candidates=["你","你好"];wait(30);verify(!gear.visible);let expand=findChild(panel,"expandButton");verify(expand.visible);compare(expand.mapToItem(panel,expand.width/2,expand.height/2).x,center.x);compare(expand.mapToItem(panel,0,expand.height/2).y,center.y);mouseClick(expand);verify(panel.expanded);compare(settings.count,0);
  backend.candidates=[];backend.changed();wait(30);verify(gear.visible);verify(!expand.visible);mouseClick(gear);verify(state.settingsOpen);compare(settings.count,1);compare(keys.count,0);
 }
 function test_leftShiftUsesRemovedSettingsKeySpace(){
  let left=findChild(panel,"key-Shift-L"),right=findChild(panel,"key-Shift-R"),area=findChild(panel,"keyArea");verify(!findChild(panel,"key-Settings"));compare(left.width,right.width);verify(left.width>findChild(panel,"key-Z").width*2);compare(Math.round(right.x+right.width),Math.round(area.width));
 }
 function test_settingsCoversEntirePanelWithoutChangingGeometry(){
  for(let width of [828,934]){panel.width=width;panel.height=344;let before={w:panel.width,h:panel.height};panel.requestSettings();wait(30);let overlay=findChild(panel,"settingsPanel");verify(overlay);let origin=overlay.mapToItem(panel,0,0);verify(origin.x<=1&&origin.y<=1,"Settings must cover the candidate row too");compare(Math.round(origin.x+overlay.width),width-1);compare(Math.round(origin.y+overlay.height),343);verify(!findChild(panel,"candidateBar").visible);verify(!findChild(panel,"keyArea").visible);compare(panel.width,before.w);compare(panel.height,before.h);mouseClick(findChild(overlay,"returnKeyboard"));wait(30);verify(findChild(panel,"candidateBar").visible);verify(findChild(panel,"keyArea").visible);compare(panel.width,before.w);compare(panel.height,before.h);}
 }
 function test_settingsReplaceKeysAndPreserveComposition(){
  backend.preedit="ni hao";backend.candidates=["你好","你"];wait(30);
  verify(!findChild(panel,"settingsPanel"));panel.requestSettings();wait(30);
  let overlay=findChild(panel,"settingsPanel");verify(overlay,"Settings must open inside the keyboard");
  verify(!findChild(panel,"keyArea").enabled);compare(keys.count,0);compare(backend.preedit,"ni hao");
  verify(!findChild(panel,"candidateBar").visible);verify(!findChild(panel,"candidateBar").enabled);
  mouseClick(findChild(overlay,"returnKeyboard"));wait(30);verify(!findChild(panel,"settingsPanel"));
  compare(backend.preedit,"ni hao");compare(backend.candidates.length,2);mouseClick(findChild(panel,"key-A"));compare(keys.count,2);
 }
 function test_allSettingsFitMinimumPanelAndPersist(){
  panel.width=828;panel.height=344;panel.requestSettings();wait(30);
  let overlay=findChild(panel,"settingsPanel");verify(overlay);
  for(let name of ["modeSetting","inputSetting","safeSetting","shortcutSetting","functionSetting","layoutSetting","screenSetting","themeSetting","opacitySetting","soundSetting"]){
   let control=findChild(overlay,name);verify(control&&control.visible,name);
   let point=control.mapToItem(overlay,0,0);verify(point.x>=0&&point.y>=0,name);
   verify(point.x+control.width<=overlay.width&&point.y+control.height<=overlay.height,name+" clipped");compare(control.height,48);
  }
  mouseClick(findChild(overlay,"functionSetting"));compare(prefs.functionRow,true);
  mouseClick(findChild(overlay,"safeSetting"));compare(prefs.safeInput,true);
  mouseClick(findChild(overlay,"modeSetting-2"));compare(prefs.mode,2);verify(findChild(panel,"settingsPanel"));
  mouseClick(findChild(overlay,"themeSetting-2"));compare(prefs.theme,2);
  verify(!findChild(overlay,"heightSetting"));
  mouseClick(findChild(overlay,"screenSetting"));wait(30);mouseClick(findChild(overlay,"settingChoice-2"));compare(prefs.screenIndex,1);
  mouseClick(findChild(overlay,"inputSetting"));wait(30);mouseClick(findChild(overlay,"settingChoice-1"));compare(prefs.preferredIm,"pinyin");
  mouseClick(findChild(overlay,"shortcutSetting"));wait(30);mouseClick(findChild(overlay,"shortcut-Shift"));mouseClick(findChild(overlay,"shortcut-key-F6"));mouseClick(findChild(overlay,"saveShortcut"));compare(prefs.shortcut,"Ctrl+Alt+Shift+F6");
  mouseClick(findChild(overlay,"returnKeyboard"));wait(30);verify(!findChild(panel,"settingsPanel"));panel.requestSettings();wait(30);compare(prefs.shortcut,"Ctrl+Alt+Shift+F6");compare(prefs.screenIndex,1);
 }
 function test_settingsCloseCandidateOverlayAndUnloadOnHide(){
  backend.candidates=["你"];panel.expanded=true;wait(30);verify(findChild(panel,"candidateFlow"));
  panel.requestSettings();wait(30);verify(!panel.expanded);verify(!findChild(panel,"candidateFlow"));verify(findChild(panel,"settingsPanel"));
  state.closePanel();wait(30);verify(!findChild(panel,"settingsPanel"));
 }
 function test_openInputPickerUpdatesFriendlyNames(){
  panel.requestSettings();wait(30);let overlay=findChild(panel,"settingsPanel");mouseClick(findChild(overlay,"inputSetting"));wait(30);
  let label=findChild(findChild(overlay,"settingChoice-1"),"settingChoiceLabel");compare(label.text,"pinyin");
  backend.inputMethodNames={"pinyin":"拼音"};wait(30);compare(label.text,"拼音");
 }
 function test_pickerBlankSpaceCannotActivateCoveredSettings(){
  panel.width=828;panel.height=344;panel.requestSettings();wait(30);
  let overlay=findChild(panel,"settingsPanel");mouseClick(findChild(overlay,"shortcutSetting"));wait(30);
  mouseClick(findChild(overlay,"shortcut-key-F6"));mouseClick(overlay,350,240);
  mouseClick(findChild(overlay,"saveShortcut"));compare(prefs.shortcut,"Ctrl+Alt+F6");compare(prefs.safeInput,false);
 }
 function test_touchCanOpenEditAndReturnWithoutTyping(){
  function tap(item){let touches=touchEvent(panel);touches.press(0,item,item.width/2,item.height/2).commit();touches.release(0,item,item.width/2,item.height/2).commit();wait(30)}
  tap(findChild(panel,"settingsButton"));let overlay=findChild(panel,"settingsPanel");verify(overlay);
  tap(findChild(overlay,"modeSetting-1"));compare(prefs.mode,1);
  tap(findChild(overlay,"safeSetting"));compare(prefs.safeInput,true);
  tap(findChild(overlay,"inputSetting"));tap(findChild(overlay,"settingChoice-1"));compare(prefs.preferredIm,"pinyin");
  tap(findChild(overlay,"shortcutSetting"));wait(30);tap(findChild(overlay,"shortcut-key-F6"));tap(findChild(overlay,"saveShortcut"));compare(prefs.shortcut,"Ctrl+Alt+F6");
  tap(findChild(overlay,"returnKeyboard"));verify(!findChild(panel,"settingsPanel"));compare(keys.count,0);
 }
 function test_shortcutControlsExistOnlyWhileEditingShortcut(){
  panel.requestSettings();wait(30);let overlay=findChild(panel,"settingsPanel");verify(!findChild(overlay,"shortcut-key-F6"));
  mouseClick(findChild(overlay,"shortcutSetting"));wait(30);verify(findChild(overlay,"shortcut-key-F6"));
  mouseClick(findChild(overlay,"cancelPicker"));wait(30);verify(!findChild(overlay,"shortcut-key-F6"));
 }
 function test_layout(){compare(panel.rows.length,5);compare(count("Esc"),1);compare(count("Del"),1);compare(count("Caps"),0);compare(count("Shift"),2);verify(panel.rows[0][0].label==="Esc");compare(panel.rows[1][panel.rows[1].length-1].label,"Del");for(let k of panel.keyItems()){verify(k.width>=48,k.label+" width");verify(k.height>=48,k.label+" height")};compare(count("↑"),0);compare(count("↓"),0);compare(count("←"),0);compare(count("→"),0);let globe=findChild(panel,"key-Language"),gear=findChild(panel,"settingsButton"),hide=findChild(panel,"key-Hide");verify(globe);verify(gear);verify(hide);verify(globe.mapToItem(panel,0,0).x<30);compare(hide.mapToItem(panel,0,0).y,findChild(panel,"key-Space").mapToItem(panel,0,0).y);verify(hide.mapToItem(panel,hide.width,0).x>900);verify(findChild(panel,"candidateBar").visible);compare(findChild(panel,"keyArea").y,56)}
 function test_candidateRowStaysVisibleAndShowsCurrentInputMethod(){
  let bar=findChild(panel,"candidateBar"),action=findChild(panel,"expandButton");verify(bar.visible);verify(!action.visible);
  let status=findChild(bar,"inputMethodStatus");verify(status&&status.visible);compare(status.text,"中文：拼音");
  backend.inputMethodStatus="英文：英语（美国）";wait(30);compare(status.text,"英文：英语（美国）");
  backend.preedit="n";wait(30);verify(bar.visible);verify(status.visible);
  backend.candidates=["你","你好"];wait(60);verify(bar.visible);verify(!status.visible);verify(action.visible);
  let first=findChild(bar,"candidate-0"),icon=findChild(panel,"expandIcon");
  compare(first.mapToItem(panel,0,first.height/2).y,icon.mapToItem(panel,0,icon.height/2).y);
  mouseClick(action);verify(panel.expanded);mouseClick(action);verify(!panel.expanded);
  mouseClick(first);compare(backend.selectedIndex,0);
  backend.preedit="";backend.candidates=[];backend.changed();wait(60);verify(bar.visible);verify(status.visible);verify(!action.visible);
  mouseClick(findChild(panel,"settingsButton"));compare(settings.count,1);compare(keys.count,0);
  mouseClick(findChild(panel,"returnKeyboard"));wait(30);mouseClick(findChild(panel,"key-Language"));compare(backend.switchedTo,"keyboard-us");compare(keys.count,0);
 }
 function test_controlsWorkWithoutInputTarget(){backend.editable=false;state.setContext(1,false,false);wait(30);verify(!findChild(panel,"key-A").enabled);verify(findChild(panel,"settingsButton").enabled);verify(findChild(panel,"key-Hide").enabled);let space=findChild(panel,"key-Space");mouseDrag(space,space.width/2,space.height/2,30,15);verify(moves.count>0);compare(keys.count,0);mouseClick(findChild(panel,"settingsButton"));compare(settings.count,1)}
 function test_mouseCanDragLetterAndActionKeysWithoutTyping(){
  let wasVisible=state.visible;
  for(let name of ["A","Backspace","Language","Hide"]){
   let key=findChild(panel,"key-"+name);moves.clear();mouseDrag(key,key.width/2,key.height/2,-24,-20);verify(moves.count>0,name+" did not move the keyboard");compare(keys.count,0);compare(settings.count,0);compare(backend.switchedTo,"");compare(state.visible,wasVisible);
  }
  let gear=findChild(panel,"settingsButton");moves.clear();mouseDrag(gear,24,24,-24,-20);verify(moves.count>0);compare(settings.count,0);compare(keys.count,0);
  mouseClick(findChild(panel,"key-A"));compare(keys.count,2);
 }
 function test_touchCanDragLetterAndStillTapToType(){
  let key=findChild(panel,"key-A"),touches=touchEvent(panel),x=key.width/2,y=key.height/2;
  touches.press(0,key,x,y).commit();touches.move(0,key,x+24,y-20).commit();touches.release(0,key,x+24,y-20).commit();wait(30);verify(moves.count>0);compare(keys.count,0);
  touches.press(0,key,x,y).commit();touches.release(0,key,x,y).commit();wait(30);compare(keys.count,2);
 }
 function test_touchCanDragSettingsActionThenTapToOpen(){
  let gear=findChild(panel,"settingsButton"),touches=touchEvent(panel);touches.press(0,gear,24,24).commit();touches.move(0,gear,0,4).commit();touches.release(0,gear,0,4).commit();wait(30);verify(moves.count>0);compare(settings.count,0);compare(keys.count,0);
  touches.press(0,gear,24,24).commit();touches.release(0,gear,24,24).commit();wait(30);verify(state.settingsOpen);compare(settings.count,1);compare(keys.count,0);
 }
 function test_emptyCandidateRowCanMoveWithoutInput(){
  let bar=findChild(panel,"candidateBar");mouseDrag(bar,200,24,30,-25);verify(moves.count>0);compare(keys.count,0);
 }
 function test_secondFingerCannotSelectOrExpandCandidatesDuringWindowDrag(){
  backend.candidates=["你","你好"];wait(60);let a=findChild(panel,"key-A"),word=findChild(panel,"candidate-0"),expand=findChild(panel,"expandButton"),touches=touchEvent(panel);
  touches.press(0,a,a.width/2,a.height/2).commit();touches.move(0,a,a.width/2+24,a.height/2-20).commit();wait(30);verify(moves.count>0);
  touches.stationary(0).press(1,word,word.width/2,24).commit();touches.stationary(0).release(1,word,word.width/2,24).commit();wait(30);compare(backend.selectedIndex,-1);
  touches.stationary(0).press(1,expand,24,24).commit();touches.stationary(0).release(1,expand,24,24).commit();wait(30);verify(!panel.expanded);
  touches.release(0,a,a.width/2+24,a.height/2-20).commit();wait(30);compare(keys.count,0);mouseClick(word);compare(backend.selectedIndex,0);
 }
 function test_candidateSwipeKeepsWindowStill(){
  backend.candidates=Array.from({length:40},(_,i)=>"连续候选"+i);wait(60);
  let bar=findChild(panel,"candidateBar");mouseDrag(bar,200,24,-80,0);compare(moves.count,0);compare(keys.count,0);compare(backend.selectedIndex,-1);
  let list=bar.children.filter(child=>child.contentX!==undefined)[0];verify(list.contentX>0);
 }
 function test_longHoldCanDragAwayFromAlternatives(){let e=findChild(panel,"key-E");mousePress(e,e.width/2,e.height/2);wait(650);mouseMove(e,-70,e.height+30);mouseRelease(e,-70,e.height+30);verify(moves.count>0);compare(keys.count,0)}
 function test_smallFingerMotionStillTypes(){let a=findChild(panel,"key-A");mouseDrag(a,a.width/2,a.height/2,3,2);compare(moves.count,0);compare(keys.count,2)}
 function test_spaceDragMovesWithoutTyping(){let space=findChild(panel,"key-Space");mouseDrag(space,space.width/2,space.height/2,30,15);verify(moves.count>0);compare(keys.count,0);mouseClick(space);compare(keys.count,2)}
 function test_functionRow(){panel.functionRow=true;wait(20);compare(panel.rows.length,6);compare(count("Esc"),1);compare(count("Del"),1);for(let n=1;n<=12;n++)compare(count("F"+n),1);compare(panel.rows[1][0].label,"`")}
 function test_candidateGeometryStable(){
  let key=findChild(panel,"key-A"),area=findChild(panel,"keyArea");
  let before={x:key.x,y:area.y,w:key.width,h:key.height,height:panel.height};
  backend.preedit="ni hao";backend.candidates=["你好","你","拟好"];wait(60);
  compare(key.height,before.h);compare(key.width,before.w);compare(area.y,before.y);compare(panel.height,before.height);
  backend.preedit="";backend.candidates=[];wait(60);compare(key.height,before.h);compare(area.y,before.y);
  backend.sensitive=true;wait(30);compare(key.height,before.h);compare(area.y,before.y);backend.sensitive=false;
 }
 function test_variableCandidates(){backend.preedit="shu";backend.candidates=["输","输入","输入法","输入一段文字"];wait(60);let bar=findChild(panel,"candidateBar");verify(bar.visible);let widths=[];for(let n=0;n<4;n++)widths.push(findChild(bar,"candidate-"+n).width);verify(widths[0]<widths[1]);verify(widths[1]<widths[2]);verify(widths[2]<widths[3]);let one=findChild(bar,"candidate-0"),two=findChild(bar,"candidate-1");compare(Math.round(two.x-one.x-one.width),8);let h=panel.height;mouseClick(findChild(panel,"expandButton"));verify(panel.expanded);compare(panel.height,h);verify(!findChild(panel,"keyArea").enabled);backend.candidateSelected();verify(!panel.expanded);mouseClick(findChild(panel,"expandButton"));verify(panel.expanded);mouseClick(findChild(panel,"expandButton"));verify(!panel.expanded)}
 function test_candidateOverlayAllocatedOnlyWhenOpen(){
  backend.preedit="shu";backend.candidates=["输","输入","输入法"];wait(60);
  verify(!findChild(panel,"candidateFlow"));
  mouseClick(findChild(panel,"expandButton"));wait(60);
  let flow=findChild(panel,"candidateFlow");verify(flow);verify(flow.count>0);
  backend.candidateSelected();wait(60);verify(!findChild(panel,"candidateFlow"));
  backend.candidates=["新候选词"];mouseClick(findChild(panel,"expandButton"));wait(60);
  flow=findChild(panel,"candidateFlow");verify(flow);compare(flow.model.candidates[0],"新候选词");
 }
 function test_dragCancel(){let a=findChild(panel,"key-A");mousePress(a,a.width/2,a.height/2);compare(keys.count,0);mouseMove(a,-20,-20);mouseRelease(a,-20,-20);compare(keys.count,0);mouseClick(a);compare(keys.count,2)}
 function test_longPressAlternative(){let e=findChild(panel,"key-E");mousePress(e,e.width/2,e.height/2);wait(650);compare(keys.count,0);mouseMove(e,e.width/2,e.height/2-18);mouseMove(e,e.width/2,1);mouseMove(e,28,-28);mouseRelease(e,28,-28);compare(keys.count,2);compare(moves.count,0)}
 function test_pagingKeepsOverlay(){backend.preedit="a";backend.candidates=["啊"];panel.expanded=true;backend.changed();verify(panel.expanded);backend.paging=true;backend.candidateEpoch++;backend.changed();verify(panel.expanded);backend.paging=false;backend.changed();verify(panel.expanded)}
 function test_clearOverlay(){backend.preedit="a";backend.candidates=["啊"];panel.expanded=true;backend.contextEpoch++;backend.changed();verify(!panel.expanded)}
}
