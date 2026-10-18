#!/bin/bash
# All activation environment roots are private BEFORE the bus daemon starts.
set -euo pipefail
owned_root=$(mktemp -d)
export XDG_RUNTIME_DIR="$owned_root/runtime" XDG_CONFIG_HOME="$owned_root/config" XDG_DATA_HOME="$owned_root/data" XDG_CACHE_HOME="$owned_root/cache" LANG=C.UTF-8
mkdir -m700 "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
cleanup(){
 for attempt in {1..20};do
  if rm -rf "$owned_root" 2>/dev/null;then return;fi
  sleep .05
 done
 rm -rf "$owned_root"
}
trap cleanup EXIT
dbus-run-session -- "$@"
