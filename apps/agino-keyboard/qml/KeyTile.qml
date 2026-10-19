import QtQuick
import org.deepin.dtk 1.0 as D
import "Layout.js" as Layout
Item {
 id:key
 property var controller
 property string label
 property int sym
 property string actionName:""
 property bool movable:false
 property bool moving:false
 property point originGlobal
 property point previousGlobal
 property bool toolbar:false
 property real iconScale:compact?0.75:1
 property real iconStrokeWidth:1.6
 property bool compact:panel&&panel.compact
 property bool lockedShift:(sym===0xffe1||sym===0xffe2)&&Boolean(controller.modifiers&2)
 property bool shiftActive:(sym===0xffe1||sym===0xffe2)&&Boolean(controller.modifiers&3)
 property string displayLabel:lockedShift?"↑ Shift":statusText!==""?statusText:(secondary&&controller.modifiers&3?secondary:label)
 property int secondaryFontSize:compact?10:12
 property real secondaryOpacity:enabled?0.9:0.4
 property var panel
 property bool held:false
 property int activeId:-1
 property bool alternate:false
 property string statusText:""
 opacity:enabled?1:0.7
 property string secondary: {
  let plain="`1234567890-=[]\\;',./",shift="~!@#$%^&*()_+{}|:\"<>?";let n=plain.indexOf(String.fromCharCode(sym));return sym<128&&n>=0?shift[n]:""
 }
 Accessible.role:Accessible.Button
 Accessible.name:actionName==="Language"?qsTranslate("Keyboard","Switch input method"):actionName==="Settings"?qsTranslate("Keyboard","Settings"):actionName==="Hide"?qsTranslate("Keyboard","Hide screen keyboard"):sym===0xff08?qsTranslate("Keyboard","Backspace"):lockedShift?qsTranslate("Keyboard","Shift (Caps Lock)"):label
 Accessible.ignored:panel&&(panel.settingsOpen||panel.expanded)
 Accessible.checkable:sym===0xffe1||sym===0xffe2
 Accessible.checked:shiftActive
 Accessible.pressed:held||shiftActive
 Accessible.onPressAction:activateAccessible()
 Accessible.onToggleAction:activateAccessible()
 function activateAccessible(){if(!controller.visible||!visible||!enabled||panel.dragOwner)return;if(actionName==="Language")panel.switchLanguage();else if(actionName==="Settings")panel.requestSettings();else if(actionName==="Hide")panel.hidePanel();else{let id=panel.nextAccessibleTouch();controller.press(id,sym);controller.release(id,true)}}
 D.ControlBackground {anchors.fill:parent;color:D.DTK.palette.button;focusBorderVisible:false;radius:key.compact?3:7;visible:!key.toolbar&&!held&&!shiftActive}
 Rectangle {anchors.fill:parent;radius:key.compact?3:7;visible:held||shiftActive;opacity:key.toolbar?0.12:1;color:D.DTK.palette.highlight}
 // KeyTile owns accessibility; these labels only paint text.
 Text {anchors.centerIn:parent;visible:actionName!=="Language"&&actionName!=="Hide"&&actionName!=="Settings"&&sym!==0xff08;text:key.displayLabel;opacity:key.enabled?1:0.4;font.pixelSize:label.length>3?(key.compact?12:14):(key.compact?14:18);color:held||shiftActive?D.DTK.palette.highlightedText:D.DTK.palette.buttonText}
 Text {visible:secondary!=="";anchors.top:parent.top;anchors.right:parent.right;anchors.margins:key.compact?3:5;text:secondary;color:held||shiftActive?D.DTK.palette.highlightedText:D.DTK.palette.buttonText;font.pixelSize:key.secondaryFontSize;opacity:key.secondaryOpacity}
 // Ordinary letter keys do not need a Canvas or a 2D drawing context.
 Loader {
  anchors.centerIn:parent;width:24;height:24;scale:key.iconScale;active:key.actionName==="Language"||key.actionName==="Hide"||key.actionName==="Settings"||key.sym===0xff08
  sourceComponent:Canvas {
  width:24;height:24
  property color ink:key.toolbar?(key.held?D.DTK.palette.highlight:D.DTK.palette.windowText):(key.held?D.DTK.palette.highlightedText:D.DTK.palette.buttonText)
  onInkChanged:requestPaint()
  onPaint:{let c=getContext("2d");c.clearRect(0,0,width,height);c.strokeStyle=ink;c.lineWidth=key.iconStrokeWidth;c.lineCap="round";c.beginPath();
   if(key.actionName==="Language"){c.arc(12,12,10,0,2*Math.PI);c.moveTo(2,12);c.lineTo(22,12);c.moveTo(4,7);c.lineTo(20,7);c.moveTo(4,17);c.lineTo(20,17);c.moveTo(12,2);c.bezierCurveTo(5,7,5,17,12,22);c.moveTo(12,2);c.bezierCurveTo(19,7,19,17,12,22)}
   else if(key.actionName==="Settings"){for(let n=0;n<32;n++){let angle=(n-0.5)*Math.PI/16;let radius=(n%4===1||n%4===2)?10:8;let x=12+radius*Math.cos(angle),y=12+radius*Math.sin(angle);if(n===0)c.moveTo(x,y);else c.lineTo(x,y)}c.closePath();c.moveTo(16,12);c.arc(12,12,4,0,Math.PI*2)}
   else if(key.sym===0xff08){c.lineJoin="round";c.moveTo(8,5);c.lineTo(22,5);c.lineTo(22,19);c.lineTo(8,19);c.lineTo(2,12);c.closePath();c.moveTo(11,9);c.lineTo(17,15);c.moveTo(17,9);c.lineTo(11,15)}
   else{c.rect(2,2,20,12);for(let y=5;y<=11;y+=3)for(let x=5;x<=19;x+=3){c.moveTo(x,y);c.lineTo(x+0.5,y)}c.moveTo(8,19);c.lineTo(12,22);c.lineTo(16,19)}c.stroke()
  }
 }
 }
 function start(id,px,py){if(held||panel.dragOwner)return;activeId=id;held=true;alternate=false;moving=false;originGlobal=mapToGlobal(px,py);previousGlobal=originGlobal;if(actionName===""){controller.press(id,sym);holdTimer.restart()}}
 function move(id,px,py){if(!movable||!held||id!==activeId)return;let current=mapToGlobal(px,py);if(alternate){let point=mapToItem(panel,px,py);if(panel.alternativeApproach(point.x,point.y,key)||Math.hypot(current.x-originGlobal.x,current.y-originGlobal.y)<16)return;alternate=false;panel.closeAlternative()};if(!moving&&Math.hypot(current.x-originGlobal.x,current.y-originGlobal.y)>=16){if(!panel.beginMove(key))return;moving=true;holdTimer.stop()};if(moving){panel.moveRequested(current.x-previousGlobal.x,current.y-previousGlobal.y);previousGlobal=current}}
 function end(id,inside,px,py,canceled){holdTimer.stop();if(id!==activeId)return;if(moving){panel.endMove(key)}else if(alternate){let p=key.mapToItem(panel,px,py);panel.finishAlternative(p.x,p.y,!canceled)}else if(actionName!==""){if(inside&&!canceled){if(actionName==="Language")panel.switchLanguage();else if(actionName==="Settings")panel.requestSettings();else panel.hidePanel()}}else controller.release(id,inside&&!canceled);held=false;activeId=-1;moving=false}

 Timer {id:holdTimer;interval:600;onTriggered:{let choices=Layout.alternatives(sym);if(choices.length&&held){controller.release(activeId,false);alternate=true;let p=key.mapToItem(panel,0,0);panel.openAlternative(choices,p.x,p.y,key.width)}}}
 MultiPointTouchArea {
  anchors.fill:parent;mouseEnabled:false;minimumTouchPoints:1;maximumTouchPoints:10
  onPressed:points=>{for(let p of points)key.start(p.pointId,p.x,p.y)}
  onUpdated:points=>{for(let p of points)key.move(p.pointId,p.x,p.y)}
  onReleased:points=>{for(let p of points)key.end(p.pointId,p.x>=0&&p.x<width&&p.y>=0&&p.y<height,p.x,p.y)}
  onCanceled:points=>{for(let p of points)key.end(p.pointId,false,p.x,p.y,true)}
 }
 MouseArea {
  anchors.fill:parent;acceptedButtons:Qt.LeftButton
  onPressed:mouse=>{if(mouse.source!==Qt.MouseEventNotSynthesized){mouse.accepted=false;return}key.start(-1,mouse.x,mouse.y)}
  onPositionChanged:mouse=>{if(pressed)key.move(-1,mouse.x,mouse.y)}
  onReleased:mouse=>{key.end(-1,containsMouse,mouse.x,mouse.y)}
  onCanceled:key.end(-1,false,0,0,true)
 }
 onEnabledChanged:if(!enabled&&held)end(activeId,false,0,0,true)
 Component.onDestruction:{if(moving)panel.endMove(key);if(held&&actionName==="")controller.release(activeId,false)}
}
