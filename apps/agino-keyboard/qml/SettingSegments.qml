import QtQuick
import org.deepin.dtk 1.0 as D
Item {
 id:segments
 property var options:[]
 property int currentIndex:0
 property color accent:D.DTK.palette.window.hslLightness<0.5?Qt.lighter(D.DTK.palette.highlight,1.6):D.DTK.palette.highlight
 signal chosen(int index)
 height:48
 Rectangle {anchors.centerIn:parent;width:parent.width;height:Math.min(32,segments.height-4);radius:6;color:Qt.rgba(D.DTK.palette.windowText.r,D.DTK.palette.windowText.g,D.DTK.palette.windowText.b,0.04)}
 Repeater {
  model:segments.options
  delegate:Item {
   required property string modelData
   required property int index
   objectName:segments.objectName+"-"+index
   x:index*width;width:segments.width/segments.options.length;height:segments.height
   Accessible.role:Accessible.RadioButton;Accessible.name:modelData;Accessible.checked:index===segments.currentIndex
   Accessible.onPressAction:if(visible&&enabled)segments.chosen(index)
   Accessible.onToggleAction:if(visible&&enabled)segments.chosen(index)
   Rectangle {anchors.centerIn:parent;width:parent.width-4;height:Math.min(28,segments.height-6);radius:5;color:D.DTK.palette.highlight;opacity:0.12;visible:index===segments.currentIndex}
   D.Label {anchors.centerIn:parent;text:modelData;font.pixelSize:segments.height<40?11:12;color:index===segments.currentIndex?segments.accent:D.DTK.palette.windowText}
   TapHandler {gesturePolicy:TapHandler.DragThreshold;onTapped:segments.chosen(index)}
  }
 }
}
