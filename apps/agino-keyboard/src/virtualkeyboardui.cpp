#include "virtualkeyboardui.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include "peercheck.h"
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
// The caller must own org.fcitx.Fcitx5 and run Fcitx5 itself (SEC-10); one verdict per unique name (a round trip to the bus daemon).
bool VirtualKeyboardUi::trusted()const{
 if(!calledFromDBus())return false;
 auto owner=connection().interface()->serviceOwner("org.fcitx.Fcitx5");
 const QString sender=message().service();
 if(!owner.isValid()||sender!=owner.value())return false;
 const auto known=verdicts_.constFind(sender);if(known!=verdicts_.cend())return known.value();
 const bool ok=PeerCheck::peerIs(connection(),sender,{QStringLiteral("/usr/bin/fcitx5")});verdicts_.insert(sender,ok);
 if(!ok)qWarning("Ignoring the bus connection %s: it owns org.fcitx.Fcitx5 but is not /usr/bin/fcitx5",qPrintable(sender));
 return ok;
}
void VirtualKeyboardUi::ShowVirtualKeyboard(){if(trusted())emit showRequested();}
void VirtualKeyboardUi::HideVirtualKeyboard(){if(trusted())emit hideRequested();}

void VirtualKeyboardUi::NotifyIMListChanged(){if(trusted())emit inputMethodsChanged();}
