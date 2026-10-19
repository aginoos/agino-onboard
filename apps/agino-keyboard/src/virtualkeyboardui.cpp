#include "virtualkeyboardui.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusReply>
#include "peercheck.h"
#include "log.h"
bool VirtualKeyboardUi::start(){
 auto bus=QDBusConnection::sessionBus();
 if(!bus.registerObject("/org/fcitx/virtualkeyboard/impanel",this,QDBusConnection::ExportAllSlots)){qCCritical(lcFcitx())<<"Cannot register /org/fcitx/virtualkeyboard/impanel on the session bus:"<<bus.lastError().name();return false;}
 if(bus.registerService("org.fcitx.Fcitx5.VirtualKeyboard")){ownsInterface_=true;qCInfo(lcFcitx())<<"Registered org.fcitx.Fcitx5.VirtualKeyboard";return true;}
 bus.unregisterObject("/org/fcitx/virtualkeyboard/impanel");
 // Deepin's Sogou CPIS module hosts this interface on Fcitx's own connection.
 // Our input/candidate bridge is independent, so leave that interface alone.
 // A separate keyboard still conflicts; never replace its service.
 auto owner=bus.interface()->serviceOwner("org.fcitx.Fcitx5.VirtualKeyboard");
 auto backend=bus.interface()->serviceOwner("org.fcitx.Fcitx5");
 const bool shared=owner.isValid()&&backend.isValid()&&!owner.value().isEmpty()&&owner.value()==backend.value();
 if(shared)qCInfo(lcFcitx())<<"org.fcitx.Fcitx5.VirtualKeyboard is hosted by Fcitx5 itself (Sogou CPIS), not registered";
 else qCWarning(lcFcitx())<<"org.fcitx.Fcitx5.VirtualKeyboard is owned by another connection:"<<(owner.isValid()?owner.value():QStringLiteral("unknown"));
 return shared;
}
// The caller must own org.fcitx.Fcitx5 and run Fcitx5 itself (SEC-10); one verdict per unique name (a round trip to the bus daemon).
bool VirtualKeyboardUi::trusted()const{
 if(!calledFromDBus())return false;
 auto owner=connection().interface()->serviceOwner("org.fcitx.Fcitx5");
 const QString sender=message().service();
 if(!owner.isValid()||sender!=owner.value())return false;
 const auto known=verdicts_.constFind(sender);if(known!=verdicts_.cend())return known.value();
 const int verdict=PeerCheck::check(connection(),sender,{QStringLiteral("/usr/bin/fcitx5")});
 if(verdict<0)return false;   // not found: not remembered
 const bool ok=verdict>0;verdicts_.insert(sender,ok);
 if(!ok)qCWarning(lcFcitx(),"Ignoring the bus connection %s: it owns org.fcitx.Fcitx5 but is not /usr/bin/fcitx5",qPrintable(sender));
 return ok;
}
void VirtualKeyboardUi::ShowVirtualKeyboard(){if(trusted())emit showRequested();}
void VirtualKeyboardUi::HideVirtualKeyboard(){if(trusted())emit hideRequested();}

void VirtualKeyboardUi::NotifyIMListChanged(){if(trusted())emit inputMethodsChanged();}
