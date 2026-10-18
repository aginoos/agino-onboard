import QtQuick
import org.deepin.dtk 1.0 as D
Item {
 id:row
 property string title
 property string subtitle:""
 property bool last:false
 property real controlWidth:180
 property bool compact:false
 default property alias controls:accessory.data
 height:row.compact?28:48
 D.Label {x:row.compact?8:12;y:!row.compact&&row.subtitle?7:0;width:parent.width-row.controlWidth-(row.compact?20:30);height:!row.compact&&row.subtitle?20:row.height;text:row.title;font.pixelSize:row.compact?11:13;verticalAlignment:Text.AlignVCenter;elide:Text.ElideRight}
 D.Label {x:12;y:27;width:parent.width-row.controlWidth-30;height:16;text:row.subtitle;visible:text!==""&&!row.compact;font.pixelSize:10;opacity:0.55;elide:Text.ElideRight}
 Item {id:accessory;anchors.right:parent.right;anchors.rightMargin:row.compact?8:12;width:row.controlWidth;height:row.height}
 Rectangle {visible:!row.last;x:12;y:parent.height-1;width:parent.width-24;height:1;color:D.DTK.palette.windowText;opacity:0.07}
}
