#!/bin/bash
set -euo pipefail
export AGINO_TEST_ALLOW_RESTART=1
export XDG_SESSION_TYPE=x11 QT_QPA_PLATFORM=xcb
unset WAYLAND_DISPLAY WAYLAND_SOCKET QT_WAYLAND_SHELL_INTEGRATION
export LANG=C.UTF-8 QT_IM_MODULE=fcitx GTK_IM_MODULE=fcitx GDK_BACKEND=x11
export XDG_CONFIG_HOME="$(mktemp -d)"
trap 'rm -rf "$XDG_CONFIG_HOME"' EXIT
mkdir -p "$XDG_CONFIG_HOME/fcitx5/conf"
printf 'EnableVirtualKeyboard=True\n' > "$XDG_CONFIG_HOME/fcitx5/conf/virtualkeyboard.conf"
if [[ "${AGINO_TEST_HOSTED_UI:-}" == 1 ]];then
 export XDG_DATA_HOME="$XDG_CONFIG_HOME/data"
 mkdir -p "$XDG_DATA_HOME/fcitx5/addon"
 cp "$(dirname "$0")/hosted-ui.conf" "$XDG_DATA_HOME/fcitx5/addon/agino-test-hosted-ui.conf"
 printf 'EnableVirtualKeyboard=False\n' > "$XDG_CONFIG_HOME/fcitx5/conf/virtualkeyboard.conf"
fi
if [[ "${AGINO_TEST_PREEDIT_DISABLED:-}" == 1 ]];then
 printf 'PreeditMode=Do not show\n' > "$XDG_CONFIG_HOME/fcitx5/conf/pinyin.conf"
fi
cat > "$XDG_CONFIG_HOME/fcitx5/profile" <<'PROFILE'
[Groups/0]
Name=Default
Default Layout=us
DefaultIM=pinyin
[Groups/0/Items/0]
Name=keyboard-us
Layout=
[Groups/0/Items/1]
Name=pinyin
Layout=
[GroupOrder]
0=Default
PROFILE
if [[ "${AGINO_TEST_HOSTED_UI:-}" == 1 ]];then
 FCITX_ADDON_DIRS="$(dirname "$1"):/usr/lib/agino-keyboard/fcitx5:/usr/lib/x86_64-linux-gnu/fcitx5" fcitx5 --keep > "$XDG_CONFIG_HOME/fcitx.log" 2>&1 &
else
 agino-keyboard-fcitx-x11 --keep > "$XDG_CONFIG_HOME/fcitx.log" 2>&1 &
fi
fpid=$!
cleanup(){
 kill "$fpid" 2>/dev/null || true
 wait "$fpid" 2>/dev/null || true
 for attempt in {1..4};do
  if rm -rf "$XDG_CONFIG_HOME" 2>/dev/null;then return;fi
  sleep .05
 done
 rm -rf "$XDG_CONFIG_HOME"
}
trap cleanup EXIT
# Wait for the real frontend, not just an early addon-owned bus name.
for ((i=0;i<100;i++));do
 if busctl --user --auto-start=no call org.fcitx.Fcitx5 /org/freedesktop/portal/inputmethod org.freedesktop.DBus.Introspectable Introspect >/dev/null 2>&1;then break;fi
 sleep .1
done
busctl --user --auto-start=no call org.fcitx.Fcitx5 /org/freedesktop/portal/inputmethod org.freedesktop.DBus.Introspectable Introspect >/dev/null
"$@"
