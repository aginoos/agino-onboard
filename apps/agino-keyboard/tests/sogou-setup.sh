#!/bin/bash
# Only sourced in an explicitly enabled disposable SDK session.
[[ "${AGINO_TEST_ALLOW_SOGOU:-}" == 1 ]] || { echo "Sogou tests require an isolated SDK" >&2; exit 2; }
export AGINO_TEST_SOGOU=1
mkdir -p "$XDG_CONFIG_HOME/fcitx5" "$XDG_DATA_HOME/fcitx5/addon" "$XDG_DATA_HOME/fcitx5/inputmethod"
cat > "$XDG_DATA_HOME/fcitx5/inputmethod/com.sogou.ime.ng.fcitx5.deepin.conf" <<'IM'
[InputMethod]
UniqueName=com.sogou.ime.ng.fcitx5.deepin
Name=Sogou IME NG
LangCode=zh_CN
Addon=com.sogou.ime.ng.fcitx5.deepin-addon
IM
cat > "$XDG_DATA_HOME/fcitx5/addon/com.sogou.ime.ng.fcitx5.deepin-addon.conf" <<'ADDON'
[Addon]
Name=Sogou IME NG
Category=InputMethod
Library=/opt/apps/com.cpis/lib/x86_64-linux-gnu/cpis-module-im-fcitx5
Type=SharedLibrary
OnDemand=True
ADDON
cat > "$XDG_CONFIG_HOME/fcitx5/profile" <<'PROFILE'
[Groups/0]
Name=Default
Default Layout=us
DefaultIM=com.sogou.ime.ng.fcitx5.deepin
[Groups/0/Items/0]
Name=keyboard-us
Layout=
[Groups/0/Items/1]
Name=com.sogou.ime.ng.fcitx5.deepin
Layout=
[GroupOrder]
0=Default
PROFILE
sogou_start(){
 # CPIS uses its own userdata path, independent of XDG and the session bus.
 # Keep learning and panel preferences in this throwaway fixture as well.
 sed "s@^path=/var/tmp/usr.data@path=$XDG_DATA_HOME/cpis@" /opt/apps/com.cpis/etc/ise.ini > "$XDG_CONFIG_HOME/ise.ini"
 sed "s@^path=/var/tmp/usr.data@path=$XDG_DATA_HOME/cpis@" /opt/apps/com.cpis/etc/isp.ini > "$XDG_CONFIG_HOME/isp.ini"
 env QT_QPA_PLATFORM=xcb /opt/apps/com.cpis/bin/cpis-engine-service --ini "$XDG_CONFIG_HOME/ise.ini" --type ini > "$XDG_CONFIG_HOME/engine.log" 2>&1 &
 sogou_engine_pid=$!
 env QT_QPA_PLATFORM=xcb /opt/apps/com.cpis/bin/cpis-panel-service --ini "$XDG_CONFIG_HOME/isp.ini" --type ini > "$XDG_CONFIG_HOME/panel.log" 2>&1 &
 sogou_panel_pid=$!
 for ((sogou_attempt=0;sogou_attempt<100;sogou_attempt++));do
  if busctl --user --auto-start=no call com.cpis.panel /com/cpis/panel org.freedesktop.DBus.Introspectable Introspect >/dev/null 2>&1;then return;fi
  sleep .05
 done
 echo "Private CPIS panel did not start" >&2;return 2
}
sogou_stop(){
 kill "$sogou_panel_pid" "$sogou_engine_pid" 2>/dev/null || true
 wait "$sogou_panel_pid" "$sogou_engine_pid" 2>/dev/null || true
}
