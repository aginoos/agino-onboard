#include "virtualkeyboardui.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
bool VirtualKeyboardUi::start(){
 auto bus=QDBusConnection::sessionBus();
 if(!bus.registerObject("/org/fcitx/virtualkeyboard/impanel",this,QDBusConnection::ExportAllSlots))return false;
 if(bus.registerService("org.fcitx.Fcitx5.VirtualKeyboard")){ownsInterface_=true;return true;}
 bus.unregisterObject("/org/fcitx/virtualkeyboard/impanel");
 // Deepin's Sogou CPIS module hosts this interface on Fcitx's own connection.
 // Our input/candidate bridge is independent, so leave that interface alone.
 // A separate keyboard still conflicts; never replace its service.
 auto owner=bus.interface()->serviceOwner("org.fcitx.Fcitx5.VirtualKeyboard");
 auto backend=bus.interface()->serviceOwner("org.fcitx.Fcitx5");
 return owner.isValid()&&backend.isValid()&&!owner.value().isEmpty()&&owner.value()==backend.value();
}
bool VirtualKeyboardUi::trusted()const{if(!calledFromDBus())return false;auto owner=connection().interface()->serviceOwner("org.fcitx.Fcitx5");return owner.isValid()&&message().service()==owner.value();}
void VirtualKeyboardUi::ShowVirtualKeyboard(){if(trusted())emit showRequested();}
void VirtualKeyboardUi::HideVirtualKeyboard(){if(trusted())emit hideRequested();}

void VirtualKeyboardUi::NotifyIMListChanged(){if(trusted())emit inputMethodsChanged();}
