import QtQuick
import QtQuick.Window
import org.deepin.dtk 1.0 as D
Item {
 id:bar
 property var input
 property var panel
 property alias settingsButton:settings
 property bool expanded:false
 property bool hasCandidates:!input.sensitive&&input.candidates.length>0
 property var entries:[]
 property var candidateTexts:input.candidates
 property double candidateContext:input.contextEpoch
 property double candidateGeneration:input.candidateEpoch
 property double entriesContext:-1
 property double entriesGeneration:-1
 property string entriesSignature:""
 property int entriesRevision:0
 property bool entriesReady:false
 Component.onCompleted:{entriesReady=true;refreshEntries()}
 onCandidateTextsChanged:refreshEntries()
 onCandidateContextChanged:refreshEntries()
 onCandidateGenerationChanged:refreshEntries()
 onHasCandidatesChanged:refreshEntries()
 function refreshEntries(){if(!entriesReady)return;let words=input.sensitive?[]:input.candidates;let signature=JSON.stringify(words);if(entriesContext===candidateContext&&entriesGeneration===candidateGeneration&&signature===entriesSignature)return;let scroll=list.contentX;let revision=++entriesRevision;entriesContext=candidateContext;entriesGeneration=candidateGeneration;entriesSignature=signature;entries=words.map(text=>({text:text,context:candidateContext,generation:candidateGeneration}));Qt.callLater(()=>{if(revision===entriesRevision&&!list.moving)list.contentX=Math.min(scroll,Math.max(0,list.contentWidth-list.width))})}
 height:bar.panel.compact?32:48
 property int iconSlotWidth:bar.panel.compact?24:32
 signal toggle()
 Image {
  id:applicationIcon;objectName:"applicationIcon";x:4;anchors.verticalCenter:parent.verticalCenter;width:bar.panel.compact?18:24;height:width
  property int rasterSize:Math.round(width*Screen.devicePixelRatio)
  source:rasterSize>=1&&rasterSize<=192?"image://keyboard-icon/"+rasterSize:Qt.resolvedUrl("../data/agino-keyboard.svg");sourceSize.width:rasterSize>=1&&rasterSize<=192?rasterSize:width;sourceSize.height:sourceSize.width;Accessible.name:"屏幕键盘"
 }
 ListView {
  id:list;Accessible.role:Accessible.List;Accessible.name:"候选词";
  Accessible.onScrollLeftAction:{if(enabled){contentX=Math.max(0,contentX-width/2);if(atXBeginning&&input.hasPrev)input.requestPage(false)}}
  Accessible.onScrollRightAction:{if(enabled){contentX=Math.min(Math.max(0,contentWidth-width),contentX+width/2);if(atXEnd&&input.hasNext)input.requestPage(true)}}
  visible:bar.hasCandidates;enabled:!bar.panel.dragOwner;x:bar.iconSlotWidth+8;y:0;width:Math.max(0,(bar.hasCandidates?expand.x:statusLabel.x)-x-8);height:bar.height;orientation:ListView.Horizontal;spacing:bar.panel.compact?6:8;clip:true;model:bar.entries
  boundsBehavior:Flickable.StopAtBounds;maximumFlickVelocity:0
  delegate:Item {
   id:word;required property var modelData;required property int index
   objectName:"candidate-"+index
   width:Math.max(bar.height,label.implicitWidth+(bar.panel.compact?20:28));height:bar.height
   property double savedContext:0;property double savedCandidate:0
   Accessible.role:Accessible.Button;Accessible.name:modelData.text
   Accessible.ignored:!bar.hasCandidates||word.x+word.width<=list.contentX||word.x>=list.contentX+list.width
   Accessible.onPressAction:if(visible&&enabled&&bar.hasCandidates&&!bar.panel.dragOwner&&bar.panel.controller.visible)input.selectCandidate(index,modelData.context,modelData.generation)
   D.Label {id:label;anchors.centerIn:parent;text:word.modelData.text;color:D.DTK.palette.windowText;font.pixelSize:bar.panel.compact?13:16}
   TapHandler {gesturePolicy:TapHandler.DragThreshold;onPressedChanged:if(pressed){word.savedContext=input.contextEpoch;word.savedCandidate=input.candidateEpoch}
      onTapped:input.selectCandidate(index,word.savedContext,word.savedCandidate)}
  }
  onMovementEnded:{if(atXEnd&&input.hasNext)input.requestPage(true);else if(atXBeginning&&input.hasPrev)input.requestPage(false)}
 }
 D.Label {
  id:statusLabel;objectName:"inputMethodStatus";visible:!bar.hasCandidates;anchors.right:settings.left;anchors.rightMargin:6;y:0;width:visible?Math.min(implicitWidth,bar.width*0.35):0;height:bar.height
  text:bar.panel.compact?(input.sensitive?"锁":(input.inputLanguageCode||"—")):input.inputMethodStatus;horizontalAlignment:Text.AlignRight;font.pixelSize:bar.panel.compact?13:14;opacity:0.75;elide:Text.ElideRight;verticalAlignment:Text.AlignVCenter
 }
 KeyTile {
  id:settings;objectName:"settingsButton";visible:!bar.hasCandidates;enabled:visible&&(!bar.panel.dragOwner||bar.panel.dragOwner===settings)
  anchors.right:parent.right;y:0;width:bar.height;height:bar.height;toolbar:true;iconScale:bar.panel.compact?0.58:0.75;iconStrokeWidth:1.2;label:"设置";sym:0;actionName:"Settings";movable:true;controller:bar.panel.controller;panel:bar.panel
 }
 D.ToolButton {
  id:expand;objectName:"expandButton";visible:bar.hasCandidates;enabled:!bar.panel.dragOwner;anchors.right:parent.right;y:0;width:bar.height;height:bar.height;focusPolicy:Qt.NoFocus;checked:bar.expanded
  Accessible.name:bar.expanded?"收起候选词":"展开候选词"
  contentItem:Item {
   Item {objectName:"expandIcon";anchors.centerIn:parent;width:14;height:14
    Canvas {anchors.fill:parent;property color ink:D.DTK.palette.windowText;onInkChanged:requestPaint()
     onPaint:{let c=getContext("2d");c.clearRect(0,0,width,height);c.strokeStyle=ink;c.lineWidth=2;c.lineCap="round";c.beginPath();c.moveTo(3,5);c.lineTo(7,9);c.lineTo(11,5);c.stroke()}
    }
   }
  }
  onClicked:bar.toggle()
 }
}
