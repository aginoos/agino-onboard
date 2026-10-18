import QtQuick
Item {
 id:drag
 property var panel
 property bool held:false
 property bool moving:false
 property int activeId:-1
 property point originGlobal
 property point previousGlobal
 function start(id,x,y){if(held||panel.dragOwner)return;held=true;activeId=id;originGlobal=mapToGlobal(x,y);previousGlobal=originGlobal}
 function move(id,x,y){if(!held||id!==activeId)return;let point=mapToGlobal(x,y);if(!moving&&Math.hypot(point.x-originGlobal.x,point.y-originGlobal.y)>=16){if(!panel.beginMove(drag))return;moving=true}if(moving){panel.moveRequested(point.x-previousGlobal.x,point.y-previousGlobal.y);previousGlobal=point}}
 function end(id){if(id!==activeId)return;panel.endMove(drag);held=false;moving=false;activeId=-1}
 MultiPointTouchArea {
  anchors.fill:parent;mouseEnabled:false;minimumTouchPoints:1;maximumTouchPoints:10
  onPressed:points=>{for(let point of points)drag.start(point.pointId,point.x,point.y)}
  onUpdated:points=>{for(let point of points)drag.move(point.pointId,point.x,point.y)}
  onReleased:points=>{for(let point of points)drag.end(point.pointId)}
  onCanceled:points=>{for(let point of points)drag.end(point.pointId)}
 }
 MouseArea {
  anchors.fill:parent;acceptedButtons:Qt.LeftButton;cursorShape:pressed?Qt.ClosedHandCursor:Qt.OpenHandCursor
  onPressed:mouse=>{if(mouse.source!==Qt.MouseEventNotSynthesized){mouse.accepted=false;return}drag.start(-1,mouse.x,mouse.y)}
  onPositionChanged:mouse=>{if(pressed)drag.move(-1,mouse.x,mouse.y)}
  onReleased:drag.end(-1)
  onCanceled:drag.end(-1)
 }
 onEnabledChanged:if(!enabled&&held)end(activeId)
}
