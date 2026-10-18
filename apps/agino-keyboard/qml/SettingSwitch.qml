import QtQuick
import QtQuick.Templates as T
import org.deepin.dtk 1.0 as D
T.Switch {
 id:control
 width:52;height:48;focusPolicy:Qt.NoFocus
 contentItem:Item {}
 indicator:Rectangle {
  anchors.centerIn:parent;width:36;height:20;radius:10
  color:control.checked?D.DTK.palette.highlight:Qt.rgba(D.DTK.palette.windowText.r,D.DTK.palette.windowText.g,D.DTK.palette.windowText.b,0.22)
  Rectangle {x:control.checked?18:2;y:2;width:16;height:16;radius:8;color:"white"}
 }
 background:Item {}
}
