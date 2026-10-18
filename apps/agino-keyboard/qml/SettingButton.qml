import QtQuick
import QtQuick.Templates as T
import org.deepin.dtk 1.0 as D
T.Button {
 id:button
 focusPolicy:Qt.NoFocus
 font.pixelSize:13;font.underline:false
 property color accent:D.DTK.palette.window.hslLightness<0.5?Qt.lighter(D.DTK.palette.highlight,1.6):D.DTK.palette.highlight
 opacity:enabled?1:0.45
 background:Item {Rectangle {
  anchors.centerIn:parent;width:parent.width;height:button.highlighted?32:parent.height;radius:6;color:button.highlighted?"transparent":D.DTK.palette.button
  border.width:button.highlighted?0:1;border.color:Qt.rgba(D.DTK.palette.windowText.r,D.DTK.palette.windowText.g,D.DTK.palette.windowText.b,0.08)
  Rectangle {anchors.fill:parent;radius:6;color:button.accent;opacity:button.down?0.2:0.12;visible:button.down||button.checked||button.highlighted}
 }
 }
 contentItem:D.Label {text:button.text;font:button.font;color:button.checked||button.highlighted?button.accent:D.DTK.palette.windowText;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter}
}
