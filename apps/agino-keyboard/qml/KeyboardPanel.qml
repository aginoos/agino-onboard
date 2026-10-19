import QtQuick
import org.deepin.dtk 1.0 as D
import org.agino.keyboard 1.0
import "Layout.js" as Layout
Rectangle {
 id:keyboardRoot
 property var controller
 property var input
 property var preferences
 property var screenNames:[]
 property bool settingsOpen:controller.settingsOpen
 property bool functionRow:false
 property bool expanded:false
 property var dragOwner:null
 property double displayedContext:0
 property double displayedCandidate:0
 Component.onCompleted:{displayedContext=input.contextEpoch;displayedCandidate=input.candidateEpoch}
 onExpandedChanged:if(expanded){displayedContext=input.contextEpoch;displayedCandidate=input.candidateEpoch}
 onSettingsOpenChanged:if(settingsOpen){expanded=false;alternatives.visible=false}
 property int accessibleTouch:-1000000
 function nextAccessibleTouch(){return --accessibleTouch}
 property var rows:Layout.rows(functionRow)
 property int candidateHeight:candidateBar.height
 property bool transparencySupported:true
 property bool compact:preferences.compact
 property int keyHeight:compact?32:48
 property int keyGap:compact?2:6
 property int rowGap:compact?2:8
 property int keyMargin:compact?8:12
 property int keyPadding:compact?4:12
 property int minimumHeight:rows.length*keyHeight+(rows.length-1)*rowGap+keyPadding*2+candidateBar.height
 signal settingsRequested()
 signal moveRequested(real dx,real dy)
 color:D.DTK.palette.window
 radius:compact?8:12
 opacity:transparencySupported?preferences.opacityPercent/100:1
 // Bake the ink over the panel fill: Qt software replaces the outer border
 // pixels, leaving a translucent black edge when the view clears to alpha.
 border.width:1;border.color:Qt.tint(D.DTK.palette.window,Qt.rgba(D.DTK.palette.windowText.r,D.DTK.palette.windowText.g,D.DTK.palette.windowText.b,0.12))
 function allKeys(){let result=[];for(let r of rows)result=result.concat(r);return result}
 function keyItems(){let items=[];for(let i=0;i<rowRepeater.count;i++){let r=rowRepeater.itemAt(i);for(let j=0;j<r.keyRepeater.count;j++)items.push(r.keyRepeater.itemAt(j))}return items}
 function beginMove(owner){if(dragOwner&&dragOwner!==owner)return false;dragOwner=owner;controller.cancel();alternatives.visible=false;for(let key of keyItems())if(key!==owner&&key.held)key.end(key.activeId,false,0,0,true);return true}
 function endMove(owner){if(dragOwner===owner)dragOwner=null}
 function requestSettings(){controller.cancel();expanded=false;alternatives.visible=false;settingsRequested()}
 function hidePanel(){expanded=false;alternatives.visible=false;controller.closePanel()}
 function switchLanguage(){if(!controller.inputAllowed||!input.editable||input.sensitive)return;controller.cancel();let methods=input.inputMethods;let current=methods.indexOf(input.inputMethod);if(methods.length>1)input.setInputMethod(methods[(current+1)%methods.length])}
 function openAlternative(choices,x,y,w){alternatives.choices=choices;alternatives.x=Math.max(8,Math.min(x,keyboardRoot.width-choices.length*56-8));alternatives.y=Math.max(candidateBar.height,y-56);alternatives.visible=true}
 function alternativeApproach(px,py,source){let point=source.mapToItem(keyboardRoot,0,source.height);return alternatives.visible&&px>=alternatives.x&&px<alternatives.x+alternatives.width&&py>=alternatives.y&&py<=point.y}
 function alternativeContains(px,py){return alternatives.visible&&py>=alternatives.y&&py<=alternatives.y+56&&px>=alternatives.x&&px<alternatives.x+alternatives.width}
 function closeAlternative(){alternatives.visible=false}
 function finishAlternative(px,py,inside){if(inside&&alternativeContains(px,py)){let n=Math.floor((px-alternatives.x)/56);let c=alternatives.choices[n];controller.press(-500,0x1000000+c.codePointAt(0));controller.release(-500,true)}alternatives.visible=false}
 PanelDragArea {anchors.fill:parent;panel:keyboardRoot;enabled:!keyboardRoot.expanded}
 D.Control {anchors.fill:parent;palette:D.DTK.palette
 CandidateBar {
  id:candidateBar;objectName:"candidateBar";x:keyboardRoot.compact?4:8;y:0;width:parent.width-2*x;input:keyboardRoot.input;expanded:keyboardRoot.expanded;panel:keyboardRoot;visible:!keyboardRoot.settingsOpen;enabled:!keyboardRoot.settingsOpen&&(!keyboardRoot.dragOwner||keyboardRoot.dragOwner===candidateBar.settingsButton)
  onToggle:{controller.cancel();keyboardRoot.expanded=!keyboardRoot.expanded;alternatives.visible=false}

 }
 Item {
  id:keyArea;objectName:"keyArea";x:keyboardRoot.keyMargin;y:candidateBar.height+(keyboardRoot.compact?4:8);width:parent.width-2*x;height:parent.height-y-keyboardRoot.keyPadding;enabled:!keyboardRoot.expanded&&!keyboardRoot.settingsOpen;visible:!keyboardRoot.settingsOpen
  Repeater {
   id:rowRepeater;model:keyboardRoot.rows
   delegate:Item {
    id:row;required property var modelData;required property int index;property alias keyRepeater:keys
    width:keyArea.width;height:keyboardRoot.keyHeight;y:index*(height+keyboardRoot.rowGap)
    Repeater {
     id:keys;model:row.modelData
     delegate:KeyTile {
      required property var modelData;required property int index
      objectName:"key-"+modelData.name
      enabled:keyboardRoot.input.editable||actionName!==""||modelData.name==="Space"
      property real pitch:(row.width+keyboardRoot.keyGap)/15
      x:Layout.start(row.modelData,index)*pitch;width:modelData.units*pitch-keyboardRoot.keyGap;height:row.height
      label:modelData.label;statusText:modelData.name==="Space"?input.status:"";sym:modelData.sym;actionName:(modelData.name==="Language"||modelData.name==="Hide")?modelData.name:"";movable:true;controller:keyboardRoot.controller;panel:keyboardRoot
     }
    }
   }
  }
 }
 Loader {
  active:keyboardRoot.expanded;anchors.fill:keyArea
  sourceComponent:Rectangle {
  color:D.DTK.palette.window;radius:8
  ListView {
   id:flow;objectName:"candidateFlow";Accessible.role:Accessible.List;Accessible.name:qsTranslate("Keyboard","More candidates");
   Accessible.onScrollDownAction:{if(enabled){contentY=Math.min(Math.max(0,contentHeight-height),contentY+height/2);if(atYEnd&&input.hasNext)input.requestPage(true)}}
   Accessible.onScrollUpAction:if(enabled)contentY=Math.max(0,contentY-height/2);anchors.fill:parent;anchors.margins:4;clip:true;spacing:8;boundsBehavior:Flickable.StopAtBounds;maximumFlickVelocity:0
   model:CandidateRows {candidates:keyboardRoot.input.candidates;availableWidth:flow.width;font:flowFont.font}
   D.Label {id:flowFont;visible:false;font.pixelSize:18}
   delegate:Item {
    required property var entries;required property real rowHeight;width:flow.width;height:rowHeight
    Repeater {
     model:parent.entries.map(entry=>Object.assign({context:keyboardRoot.displayedContext,generation:keyboardRoot.displayedCandidate},entry))
     delegate:Item {
      id:candidate;required property var modelData;objectName:"expandedCandidate-"+modelData.index;
      Accessible.role:Accessible.Button;Accessible.name:modelData.text
      Accessible.onPressAction:if(visible&&enabled&&keyboardRoot.expanded&&controller.visible&&!keyboardRoot.dragOwner)input.selectCandidate(modelData.index,modelData.context,modelData.generation)
x:modelData.x;width:modelData.width;height:modelData.height
      property double savedContext:0;property double savedCandidate:0
      D.ControlBackground {anchors.fill:parent;color:D.DTK.palette.button;focusBorderVisible:false;radius:6}
      D.Label {anchors.fill:parent;anchors.margins:12;text:parent.modelData.text;color:D.DTK.palette.buttonText;font.pixelSize:18;wrapMode:Text.Wrap;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter}
      TapHandler {gesturePolicy:TapHandler.DragThreshold;onPressedChanged:if(pressed){candidate.savedContext=input.contextEpoch;candidate.savedCandidate=input.candidateEpoch}
      onTapped:input.selectCandidate(modelData.index,candidate.savedContext,candidate.savedCandidate)}
     }
    }
   }
   onMovementEnded:if(atYEnd&&input.hasNext)input.requestPage(true)
  }
  }
 }
 Loader {
  active:keyboardRoot.settingsOpen;enabled:!keyboardRoot.dragOwner;anchors.fill:parent;anchors.margins:1;clip:true
  sourceComponent:SettingsPanel {transparencySupported:keyboardRoot.transparencySupported;preferences:keyboardRoot.preferences;input:keyboardRoot.input;screenNames:keyboardRoot.screenNames;onFinished:controller.setSettingsOpen(false)}
 }
 Rectangle {
  id:alternatives;visible:false;z:5;property var choices:[];width:choices.length*56;height:56;radius:7;color:D.DTK.palette.highlight
  Row {Repeater {model:alternatives.choices;delegate:D.Label {required property string modelData;objectName:"alternative-"+modelData;Accessible.role:Accessible.Button;Accessible.name:modelData;
   Accessible.onPressAction:{if(alternatives.visible&&controller.visible&&controller.inputAllowed&&!keyboardRoot.dragOwner){let id=keyboardRoot.nextAccessibleTouch();controller.press(id,0x1000000+modelData.codePointAt(0));controller.release(id,true);alternatives.visible=false}}
   width:56;height:56;text:modelData;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter;color:D.DTK.palette.highlightedText;font.pixelSize:20}}}
 }
 }
 Connections {target:input;ignoreUnknownSignals:true;function onCandidateSelected(){keyboardRoot.expanded=false}function onPageFinished(accepted,context,candidate){if(!accepted){keyboardRoot.expanded=false}}function onChanged(){if(!input.editable||input.sensitive||input.contextEpoch!==keyboardRoot.displayedContext||(input.candidateEpoch!==keyboardRoot.displayedCandidate&&!input.paging)){keyboardRoot.expanded=false;alternatives.visible=false}keyboardRoot.displayedContext=input.contextEpoch;keyboardRoot.displayedCandidate=input.candidateEpoch}}
}
