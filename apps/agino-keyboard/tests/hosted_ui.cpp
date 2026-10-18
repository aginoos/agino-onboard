// SPDX-License-Identifier: GPL-3.0-or-later
// Private-session fixture: emulate CPIS owning its UI on Fcitx's main bus.
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx-utils/dbus/bus.h>
class HostedUi final : public fcitx::AddonInstance {
public:
 explicit HostedUi(fcitx::AddonManager *manager){
  auto addon=manager->addon("dbus",true);
  if(addon)addon->callWithSignature<fcitx::dbus::Bus *()>("bus")->requestName("org.fcitx.Fcitx5.VirtualKeyboard", fcitx::dbus::RequestNameFlag::Queue);
 }
};
class Factory final : public fcitx::AddonFactory {
 fcitx::AddonInstance *create(fcitx::AddonManager *manager)override{return new HostedUi(manager);}
};
FCITX_ADDON_FACTORY(Factory)
