#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Optional real AT-SPI integration test. Installs/downloads nothing.
set -euo pipefail
skip(){ echo "SKIP: $*"; exit 77; }
[[ $# == 1 ]] || { echo "usage: $0 /absolute/path/test-native-accessible" >&2; exit 2; }
fixture=$(readlink -f "$1")
[[ -x "$fixture" ]] || { echo "Missing accessibility fixture: $fixture" >&2; exit 2; }
script=$(readlink -f "$0")
tests=$(dirname "$script")
for tool in python3 dbus-daemon dbus-run-session xvfb-run; do command -v "$tool" >/dev/null || skip "missing $tool"; done
prefix=${AGINO_ATSPI_TEST_PREFIX:-}
registry=""
for path in "$prefix/usr/libexec/at-spi2-registryd" "$prefix/usr/lib/at-spi2-core/at-spi2-registryd"; do
 if [[ -x "$path" ]]; then registry=$path; break; fi
done
[[ -n "$registry" ]] || skip "at-spi2-registryd unavailable (or set AGINO_ATSPI_TEST_PREFIX to an extracted SDK test prefix)"
if [[ -n "$prefix" ]]; then
 export PYTHONPATH="$prefix/usr/lib/python3/dist-packages${PYTHONPATH:+:$PYTHONPATH}"
 for directory in "$prefix"/usr/lib/*/girepository-1.0; do
  [[ -d "$directory" ]] && export GI_TYPELIB_PATH="$directory${GI_TYPELIB_PATH:+:$GI_TYPELIB_PATH}"
 done
 for directory in "$prefix"/usr/lib/*; do
  [[ -d "$directory" && -e "$directory/libatspi.so.0" ]] && export LD_LIBRARY_PATH="$directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
 done
fi
python3 -c 'import pyatspi; from gi.repository import GLib' >/dev/null 2>&1 || skip "Python pyatspi/GI bindings unavailable"
if [[ ${AGINO_ATSPI_PRIVATE:-0} != 1 ]]; then
 exec xvfb-run -a "$tests/private-bus-session.sh" env AGINO_ATSPI_PRIVATE=1 "$script" "$fixture"
fi
export QT_QPA_PLATFORM=xcb QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1
export AGINO_ATSPI_EVIDENCE_DIR=${AGINO_ATSPI_EVIDENCE_DIR:-"$XDG_RUNTIME_DIR/atspi-evidence"}
mkdir -p "$AGINO_ATSPI_EVIDENCE_DIR"
printf '%s\n' '{"status":"RUNNING","orca_tested":false}' >"$AGINO_ATSPI_EVIDENCE_DIR/result.json"
mapfile -t bus < <(dbus-daemon --session --fork --print-address --print-pid)
[[ ${#bus[@]} == 2 ]] || { echo 'Failed to start private accessibility bus' >&2; exit 1; }
export AT_SPI_BUS_ADDRESS="${bus[0]}"
"$registry" >"$AGINO_ATSPI_EVIDENCE_DIR/registry.log" 2>&1 & registry_pid=$!
app_pid=""
cleanup(){
 [[ -z "$app_pid" ]] || kill "$app_pid" 2>/dev/null || true
 kill "$registry_pid" "${bus[1]}" 2>/dev/null || true
}
trap cleanup EXIT
# Exercise the typed payload/no-op/reentrant-destruction assertions while the
# native platform bridge is actually active, before exporting the live fixture.
unit_status=0
"$fixture" >"$AGINO_ATSPI_EVIDENCE_DIR/unit.log" 2>&1 || unit_status=$?
if [[ "$unit_status" != 0 ]]; then
 cat "$AGINO_ATSPI_EVIDENCE_DIR/unit.log"
 printf '%s\n' '{"status":"FAIL","phase":"active Qt notifier unit test","orca_tested":false}' >"$AGINO_ATSPI_EVIDENCE_DIR/result.json"
 exit "$unit_status"
fi
"$fixture" --atspi-fixture >"$AGINO_ATSPI_EVIDENCE_DIR/fixture.log" 2>&1 & app_pid=$!
status=0
python3 "$tests/native-accessibility-atspi.py" >"$AGINO_ATSPI_EVIDENCE_DIR/inspection.log" 2>&1 || status=$?
cat "$AGINO_ATSPI_EVIDENCE_DIR/inspection.log"
if [[ "$status" == 0 ]]; then wait "$app_pid" || status=$?; fi
python3 - "$AGINO_ATSPI_EVIDENCE_DIR" "$status" <<'PY'
import json, pathlib, sys
root=pathlib.Path(sys.argv[1]);result=root/'result.json'
if result.exists():
 data=json.loads(result.read_text())
 data['fixture_exit_status']=int(sys.argv[2])
 data['qt_children_changed_signature_warning']='invalid signature' in (root/'inspection.log').read_text()
 if sys.argv[2]!='0':data['status']='FAIL'
 result.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
PY
exit "$status"
