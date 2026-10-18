#!/bin/bash
set -euo pipefail
ulimit -c 0
export AGINO_TEST_ALLOW_RESTART=1
export LANG=C.UTF-8
export XDG_RUNTIME_DIR="$(mktemp -d)" XDG_CONFIG_HOME="$(mktemp -d)"
chmod 700 "$XDG_RUNTIME_DIR"
trap 'rm -rf "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME"' EXIT
mkdir -p "$XDG_CONFIG_HOME/fcitx5/conf"
printf 'DetectApplication=False\n' > "$XDG_CONFIG_HOME/fcitx5/conf/waylandim.conf"
printf 'EnableVirtualKeyboard=True\n' > "$XDG_CONFIG_HOME/fcitx5/conf/virtualkeyboard.conf"
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
if [[ "${AGINO_TEST_SOGOU:-}" == 1 ]];then
 export XDG_DATA_HOME="$XDG_RUNTIME_DIR/data" XDG_CACHE_HOME="$XDG_RUNTIME_DIR/cache"
 source "$(dirname "$0")/sogou-setup.sh"
 sogou_start
fi
# Deepin's scissor and other GL effects crash with the QPainter test backend.
cat > "$XDG_CONFIG_HOME/kwinrc" <<'CONFIG'
[Plugins]
scissorEnabled=false
maximizeexEnabled=false
alttabthumbnaillistEnabled=false
multitaskviewEnabled=false
blurEnabled=false
contrastEnabled=false
blendchangesEnabled=false
startupfeedbackEnabled=false
screentransformEnabled=false
[Wayland]
VirtualKeyboardEnabled=true
CONFIG
# Drop distro file capabilities in this unprivileged nested test only; retain basename for KWin's static QPA plugin.
mkdir -p "$XDG_RUNTIME_DIR/bin"
cp /usr/bin/kwin_wayland "$XDG_RUNTIME_DIR/bin/kwin_wayland"
KWIN_COMPOSE=Q KWIN_IM_SHOW_ALWAYS=1 "$XDG_RUNTIME_DIR/bin/kwin_wayland" --socket agino-keyboard-test --xwayland --inputmethod '/usr/bin/agino-keyboard-fcitx-wayland' --width 1280 --height 800 --output-count "${AGINO_TEST_OUTPUT_COUNT:-1}" > "$XDG_RUNTIME_DIR/kwin.log" 2>&1 &
kwin_pid=$!
cleanup(){
 kill "$kwin_pid" 2>/dev/null || true
 wait "$kwin_pid" 2>/dev/null || true
 if [[ "${AGINO_TEST_SOGOU:-}" == 1 ]];then sogou_stop;fi
 # Private bus-activated helpers can finish writing sockets after KWin exits.
 for attempt in {1..4};do
  if rm -rf "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME" 2>/dev/null;then return;fi
  sleep .05
 done
 rm -rf "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME"
}
trap cleanup EXIT
for ((i=0;i<100;i++));do [[ -S "$XDG_RUNTIME_DIR/agino-keyboard-test" ]] && break;kill -0 "$kwin_pid" || { tail -n 20 "$XDG_RUNTIME_DIR/kwin.log";exit 2;};sleep .1;done
export WAYLAND_DISPLAY=agino-keyboard-test XDG_SESSION_TYPE=wayland QT_QPA_PLATFORM=wayland QT_IM_MODULE=wayland GTK_IM_MODULE=wayland GDK_BACKEND=wayland QT_QUICK_BACKEND=software
unset WAYLAND_SOCKET QT_WAYLAND_SHELL_INTEGRATION
if [[ "${AGINO_TEST_XWAYLAND:-0}" == 1 ]];then
 for ((i=0;i<100;i++));do grep -q 'X11 display: .* is xwayland' "$XDG_RUNTIME_DIR/kwin.log" && break;sleep .1;done
 display="$(sed -n 's/.*X11 display: \(.*\) is xwayland\..*/\1/p' "$XDG_RUNTIME_DIR/kwin.log" | head -1)"
 [[ -n "$display" ]] || { echo 'XWayland display was not published' >&2;exit 2; }
 export DISPLAY="$display" QT_QPA_PLATFORM=xcb QT_IM_MODULE=fcitx GTK_IM_MODULE=fcitx GDK_BACKEND=x11 XDG_SESSION_TYPE=x11
 unset WAYLAND_DISPLAY
fi
# Wait for the real frontend, not just an early addon-owned bus name.
for ((i=0;i<100;i++));do
 if busctl --user --auto-start=no call org.fcitx.Fcitx5 /org/freedesktop/portal/inputmethod org.freedesktop.DBus.Introspectable Introspect >/dev/null 2>&1;then break;fi
 sleep .1
done
busctl --user --auto-start=no call org.fcitx.Fcitx5 /org/freedesktop/portal/inputmethod org.freedesktop.DBus.Introspectable Introspect >/dev/null
if ! "$@"; then mkdir -p /tmp/agino-keyboard-wayland-failure;cp "$XDG_RUNTIME_DIR/kwin.log" /tmp/agino-keyboard-wayland-failure/kwin.log;exit 1;fi
