#!/bin/bash
set -euo pipefail
ulimit -c 0
export LANG=C.UTF-8
export XDG_RUNTIME_DIR="$(mktemp -d)" XDG_CONFIG_HOME="$(mktemp -d)"
chmod 700 "$XDG_RUNTIME_DIR"
cat > "$XDG_CONFIG_HOME/kwinrc" <<'CONFIG'
[Plugins]
scissorEnabled=false
blurEnabled=false
contrastEnabled=false
startupfeedbackEnabled=false
[Compositing]
Enabled=true
CONFIG
kwin_x11 --replace > "$XDG_RUNTIME_DIR/kwin.log" 2>&1 &
wm_pid=$!
cleanup(){ kill "$wm_pid" 2>/dev/null || true; wait "$wm_pid" 2>/dev/null || true; rm -rf "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME"; }
trap cleanup EXIT
for ((i=0;i<100;i++));do
 if xprop -root _NET_SUPPORTING_WM_CHECK | grep -q 'window id';then break;fi
 kill -0 "$wm_pid" || { cat "$XDG_RUNTIME_DIR/kwin.log";exit 2; }
 sleep .1
done
export AGINO_TEST_NATIVE_SHADOW=1 QT_QPA_PLATFORM='dxcb;xcb' QT_QUICK_BACKEND=software
if ! "$@";then cat "$XDG_RUNTIME_DIR/kwin.log";exit 1;fi
