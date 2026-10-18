import QtQuick
import org.deepin.dtk 1.0 as D
Item {
 id:choice
 property string text
 signal clicked()
 height:48
 Accessible.role:Accessible.Button;Accessible.name:text
 Accessible.onPressAction:if(visible&&enabled)choice.clicked()
 Rectangle {anchors.centerIn:parent;width:parent.width;height:Math.min(32,choice.height-4);radius:6;color:"transparent";border.width:1;border.color:Qt.rgba(D.DTK.palette.windowText.r,D.DTK.palette.windowText.g,D.DTK.palette.windowText.b,0.12)}
 D.Label {x:10;y:0;width:parent.width-35;height:choice.height;text:choice.text;font.pixelSize:choice.height<40?11:12;elide:Text.ElideRight;verticalAlignment:Text.AlignVCenter}
 Canvas {anchors.right:parent.right;anchors.rightMargin:10;anchors.verticalCenter:parent.verticalCenter;width:10;height:10;property color ink:D.DTK.palette.windowText;onInkChanged:requestPaint();onPaint:{let c=getContext("2d");c.clearRect(0,0,10,10);c.strokeStyle=ink;c.lineWidth=1.4;c.beginPath();c.moveTo(1,3);c.lineTo(5,7);c.lineTo(9,3);c.stroke()}}
 TapHandler {gesturePolicy:TapHandler.DragThreshold;onTapped:choice.clicked()}
}
