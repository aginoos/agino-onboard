import QtQuick
import QtQuick.Templates as T
import org.deepin.dtk 1.0 as D
Rectangle {
 id:settings
 objectName:"settingsPanel"
 property var preferences
 property var input
 property var screenNames:[]
 property bool transparencySupported:true
 property bool compact:preferences.compact
 property int rowHeight:compact?28:48
 property int headerHeight:compact?32:48
 property color accent:D.DTK.palette.window.hslLightness<0.5?Qt.lighter(D.DTK.palette.highlight,1.6):D.DTK.palette.highlight
 signal finished()
 color:D.DTK.palette.window
 radius:settings.compact?8:12
 function inputName(id){return input.inputMethodNames&&input.inputMethodNames[id]?input.inputMethodNames[id]:id}
 property bool ready:false
 Component.onCompleted:ready=true
 onScreenNamesChanged:refreshChoices()
 Connections {target:input;function onChanged(){settings.refreshChoices()}}
 Connections {target:preferences;function onChanged(){settings.refreshChoices()}}
 function refreshChoices(){if(!ready||!picker.visible||picker.shortcut)return;updateChoices(picker.kind)}
 function updateChoices(kind){
  let options=kind==="input"?input.inputMethods.map(id=>({value:id,text:inputName(id),identity:"input:"+id})):[{value:-1,text:"跟随输入窗口",identity:"screen:follow"}].concat(screenNames.map((name,index)=>({value:index,text:name,identity:"screen:"+index+":"+name})))
  picker.selected=kind==="input"?(preferences.preferredIm||input.inputMethod):preferences.screenIndex
  if(JSON.stringify(options)!==JSON.stringify(picker.options)){let scroll=choices.contentY;picker.options=options;Qt.callLater(()=>{if(picker.visible&&!picker.shortcut)choices.contentY=Math.min(scroll,Math.max(0,choices.contentHeight-choices.height))})}
 }
 function openChoices(kind){
  picker.kind=kind;picker.shortcut=false
  picker.title=kind==="input"?"默认输入法":"显示屏幕";updateChoices(kind)
  picker.visible=true
 }
 function openShortcut(){
  picker.title="呼出快捷键";picker.shortcut=true;picker.kind="shortcut";picker.options=[]
  let parts=preferences.shortcut.split("+");picker.modifiers=parts.slice(0,-1).map(m=>m==="Meta"?"Super":m);picker.trigger=parts[parts.length-1]||"K";picker.visible=true
 }
 function selectChoice(value){if(picker.kind==="input")preferences.preferredIm=value;else preferences.screenIndex=value;picker.visible=false}
 Item {
  id:header;x:12;y:0;width:parent.width-24;height:settings.headerHeight;enabled:!picker.visible;visible:!picker.visible
  D.Label {x:4;anchors.verticalCenter:parent.verticalCenter;text:"⚙";font.pixelSize:20;opacity:0.6}
  D.Label {x:34;anchors.verticalCenter:parent.verticalCenter;text:"键盘设置";font.pixelSize:settings.compact?13:16;font.weight:Font.DemiBold}
  SettingButton {objectName:"returnKeyboard";anchors.right:parent.right;width:112;height:settings.headerHeight;text:"‹  返回键盘";font.pixelSize:settings.compact?11:13;highlighted:true;Accessible.name:"返回键盘"
   onClicked:settings.finished()
  }
 }
 Row {
  x:settings.compact?8:12;y:settings.headerHeight+(settings.compact?4:8);width:parent.width-2*x;height:(settings.compact?20:28)+5*settings.rowHeight;spacing:settings.compact?8:12;enabled:!picker.visible;visible:!picker.visible
  Rectangle {
   width:(parent.width-parent.spacing)/2;height:parent.height;radius:9;color:D.DTK.palette.base
   border.width:1;border.color:Qt.rgba(D.DTK.palette.windowText.r,D.DTK.palette.windowText.g,D.DTK.palette.windowText.b,0.08)
   D.Label {x:settings.compact?8:12;height:settings.compact?20:28;text:"输入与呼出";font.pixelSize:11;opacity:0.6;verticalAlignment:Text.AlignVCenter}
   Column {y:settings.compact?20:28;width:parent.width
    SettingRow {width:parent.width;compact:settings.compact;controlWidth:settings.compact?130:180;title:"呼出方式"
     SettingSegments {objectName:"modeSetting";anchors.fill:parent;options:["自动","手动","关闭"];currentIndex:preferences.mode;onChosen:index=>preferences.mode=index}
    }
    SettingRow {width:parent.width;compact:settings.compact;controlWidth:settings.compact?130:180;title:"默认输入法"
     SettingChoice {objectName:"inputSetting";anchors.fill:parent;text:settings.inputName(preferences.preferredIm||input.inputMethod)||"请选择";onClicked:settings.openChoices("input")}
    }
    SettingRow {width:parent.width;compact:settings.compact;title:"安全输入";subtitle:"关闭候选词与词库学习";controlWidth:52
     SettingSwitch {height:settings.rowHeight;objectName:"safeSetting";checked:preferences.safeInput;Accessible.name:"安全输入";onToggled:preferences.safeInput=checked}
    }
    SettingRow {width:parent.width;compact:settings.compact;controlWidth:settings.compact?130:180;title:"呼出快捷键"
     SettingChoice {objectName:"shortcutSetting";anchors.fill:parent;text:preferences.shortcut;onClicked:settings.openShortcut()}
    }
    SettingRow {width:parent.width;compact:settings.compact;title:"按键声音";controlWidth:52;last:true
     SettingSwitch {objectName:"soundSetting";height:settings.rowHeight;checked:preferences.keySound;Accessible.name:"按键声音";onToggled:preferences.keySound=checked}
    }
   }
  }
  Rectangle {
   width:(parent.width-parent.spacing)/2;height:parent.height;radius:9;color:D.DTK.palette.base
   border.width:1;border.color:Qt.rgba(D.DTK.palette.windowText.r,D.DTK.palette.windowText.g,D.DTK.palette.windowText.b,0.08)
   D.Label {x:settings.compact?8:12;height:settings.compact?20:28;text:"外观与显示";font.pixelSize:11;opacity:0.6;verticalAlignment:Text.AlignVCenter}
   Column {y:settings.compact?20:28;width:parent.width
    SettingRow {width:parent.width;compact:settings.compact;controlWidth:settings.compact?130:180;title:"布局模式"
     SettingSegments {objectName:"layoutSetting";anchors.fill:parent;options:["标准","紧凑"];currentIndex:preferences.compact?1:0;onChosen:index=>preferences.compact=index===1}
    }
    SettingRow {width:parent.width;compact:settings.compact;title:"显示功能键行";subtitle:"F1–F12";controlWidth:52
     SettingSwitch {height:settings.rowHeight;objectName:"functionSetting";checked:preferences.functionRow;Accessible.name:"显示功能键行";onToggled:preferences.functionRow=checked}
    }
    SettingRow {width:parent.width;compact:settings.compact;controlWidth:settings.compact?130:180;title:"显示屏幕"
     SettingChoice {objectName:"screenSetting";anchors.fill:parent;text:preferences.screenIndex<0?"跟随输入窗口":(screenNames[preferences.screenIndex]||"跟随输入窗口");onClicked:settings.openChoices("screen")}
    }
    SettingRow {width:parent.width;compact:settings.compact;controlWidth:settings.compact?130:180;title:"主题"
     SettingSegments {objectName:"themeSetting";anchors.fill:parent;options:["系统","浅色","深色"];currentIndex:preferences.theme;onChosen:index=>preferences.theme=index}
    }
    SettingRow {width:parent.width;compact:settings.compact;controlWidth:settings.compact?130:180;title:"不透明度";last:true
     T.Slider {id:opacitySlider;objectName:"opacitySetting";property bool settled:false;Component.onCompleted:settled=true;
      Accessible.onIncreaseAction:if(enabled&&visible)value=Math.min(to,value+stepSize)
      Accessible.onDecreaseAction:if(enabled&&visible)value=Math.max(from,value-stepSize)
      onValueChanged:{if(settled){if(enabled)preferences.opacityPercent=Math.round(value);else if(value!==preferences.opacityPercent)value=Qt.binding(()=>preferences.opacityPercent)}}
enabled:settings.transparencySupported;visible:enabled;width:parent.width-36;height:settings.rowHeight;from:50;to:100;stepSize:1;value:preferences.opacityPercent;focusPolicy:Qt.NoFocus;Accessible.name:"屏幕键盘不透明度";onMoved:preferences.opacityPercent=Math.round(value)
      background:Rectangle {x:6;y:(parent.height-3)/2;width:parent.width-12;height:3;radius:2;color:Qt.rgba(D.DTK.palette.windowText.r,D.DTK.palette.windowText.g,D.DTK.palette.windowText.b,0.15);Rectangle {width:parent.width*opacitySlider.visualPosition;height:3;radius:2;color:D.DTK.palette.highlight}}
      handle:Rectangle {x:6+opacitySlider.visualPosition*(opacitySlider.width-24);y:(opacitySlider.height-12)/2;width:12;height:12;radius:6;color:D.DTK.palette.highlight}
     }
     D.Label {anchors.right:parent.right;height:settings.rowHeight;text:settings.transparencySupported?preferences.opacityPercent+"%":"需要桌面合成";font.pixelSize:settings.compact?10:12;verticalAlignment:Text.AlignVCenter;opacity:0.7}
    }
   }
  }
 }
 // Pickers stay in this non-focus-stealing panel, including shortcut editing.
 // A physical-key capture field would receive keys intended for the input client.
 Rectangle {
  id:picker;objectName:"settingsPicker";anchors.fill:parent;visible:false;z:2;radius:settings.compact?8:12;color:D.DTK.palette.window
  property string kind:""
  property string title:""
  property var options:[]
  property var selected
  property bool shortcut:false
  property var modifiers:[]
  property string trigger:"K"
  MouseArea {anchors.fill:parent;acceptedButtons:Qt.AllButtons;onWheel:wheel=>wheel.accepted=true}
  Item {x:16;width:parent.width-32;height:48
   D.Label {anchors.verticalCenter:parent.verticalCenter;text:picker.title;font.pixelSize:settings.compact?13:16;font.weight:Font.DemiBold}
   SettingButton {objectName:"cancelPicker";anchors.right:parent.right;width:112;height:48;text:"‹  返回设置";highlighted:true;Accessible.name:"返回设置";onClicked:picker.visible=false}
  }
  ListView {
   id:choices;objectName:"settingsChoices";x:12;y:56;width:parent.width-24;height:parent.height-68;visible:!picker.shortcut;clip:true;model:picker.options
   boundsBehavior:Flickable.StopAtBounds;maximumFlickVelocity:0
   delegate:Item {id:option;required property var modelData;required property int index;objectName:"settingChoice-"+index;
    property string pressedIdentity:""
    Accessible.role:Accessible.RadioButton;Accessible.name:picker.kind==="input"?settings.inputName(modelData.value):modelData.text;Accessible.checked:modelData.value===picker.selected
    Accessible.onPressAction:if(visible&&enabled&&picker.visible)settings.selectChoice(modelData.value)
    Accessible.onToggleAction:if(visible&&enabled&&picker.visible)settings.selectChoice(modelData.value)
width:ListView.view.width;height:48
    Rectangle {anchors.fill:parent;anchors.margins:2;radius:7;color:D.DTK.palette.highlight;opacity:0.12;visible:modelData.value===picker.selected}
    D.Label {objectName:"settingChoiceLabel";x:16;anchors.verticalCenter:parent.verticalCenter;text:picker.kind==="input"?settings.inputName(modelData.value):modelData.text;font.pixelSize:13;color:modelData.value===picker.selected?settings.accent:D.DTK.palette.windowText}
    TapHandler {gesturePolicy:TapHandler.DragThreshold;onPressedChanged:if(pressed)option.pressedIdentity=option.modelData.identity;onTapped:if(option.pressedIdentity===option.modelData.identity)settings.selectChoice(modelData.value)}
   }
  }
  Loader {x:16;y:52;width:parent.width-32;height:parent.height-64;active:picker.visible&&picker.shortcut
  sourceComponent:Flickable {id:shortcutEditor;objectName:"shortcutEditor";anchors.fill:parent;clip:true;contentWidth:width;contentHeight:48+Math.ceil(48/shortcutGrid.columns)*46+48;boundsBehavior:Flickable.StopAtBounds;maximumFlickVelocity:0
   Row {y:0;spacing:8
    Repeater {model:["Ctrl","Alt","Shift","Super"]
     delegate:SettingButton {required property string modelData;objectName:"shortcut-"+modelData;width:88;height:44;text:modelData;checkable:true;checked:picker.modifiers.indexOf(modelData)>=0
      onClicked:{let chosen=picker.modifiers.slice();let index=chosen.indexOf(modelData);if(index<0)chosen.push(modelData);else chosen.splice(index,1);picker.modifiers=chosen}
     }
    }
   }
   Grid {id:shortcutGrid;y:48;columns:settings.compact?12:16;spacing:2
    Repeater {model:"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789".split("").concat(Array.from({length:12},(_,i)=>"F"+(i+1)))
     delegate:SettingButton {required property string modelData;objectName:"shortcut-key-"+modelData;width:(shortcutEditor.width-(shortcutGrid.columns-1)*2)/shortcutGrid.columns;height:44;text:modelData;font.pixelSize:11;checkable:true;checked:picker.trigger===modelData;onClicked:picker.trigger=modelData}
    }
   }
   D.Label {x:4;y:shortcutEditor.contentHeight-44;height:40;text:picker.modifiers.concat([picker.trigger]).join("+");font.pixelSize:13;verticalAlignment:Text.AlignVCenter}
   SettingButton {objectName:"saveShortcut";y:shortcutEditor.contentHeight-44;anchors.right:parent.right;width:112;height:44;text:"保存快捷键";enabled:picker.modifiers.length>0;onClicked:{preferences.shortcut=["Ctrl","Alt","Shift","Super"].filter(m=>picker.modifiers.indexOf(m)>=0).map(m=>m==="Super"?"Meta":m).concat([picker.trigger]).join("+");picker.visible=false}}
  }
  }
 }
}
