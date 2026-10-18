#!/bin/bash
set -euo pipefail
export AGINO_TEST_ALLOW_RESTART=1
sogou_workdir="$(mktemp -d /tmp/agino-sogou-session.XXXXXX)"
export XDG_CONFIG_HOME="$sogou_workdir/config" XDG_DATA_HOME="$sogou_workdir/data" XDG_CACHE_HOME="$sogou_workdir/cache"
export XDG_SESSION_TYPE=x11 QT_QPA_PLATFORM=xcb QT_IM_MODULE=fcitx GTK_IM_MODULE=fcitx GDK_BACKEND=x11 LANG=C.UTF-8
unset WAYLAND_DISPLAY WAYLAND_SOCKET
source "$(dirname "$0")/sogou-setup.sh"
sogou_start
agino-keyboard-fcitx-x11 --keep > "$XDG_CONFIG_HOME/fcitx.log" 2>&1 &
sogou_fcitx_pid=$!
sogou_cleanup(){
 local status=$?
 if ((status));then mkdir -p /tmp/agino-keyboard-sogou-failure;cp "$XDG_CONFIG_HOME/"*.log /tmp/agino-keyboard-sogou-failure/;fi
 kill "$sogou_fcitx_pid" 2>/dev/null || true
 wait "$sogou_fcitx_pid" 2>/dev/null || true
 sogou_stop
 rm -rf "$sogou_workdir"
}
trap sogou_cleanup EXIT
"$@"
