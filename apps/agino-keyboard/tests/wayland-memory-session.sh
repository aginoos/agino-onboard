#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Start KWin with an unscaled host environment and true compositor output scale.
# KWin windowed --width/--height are logical dimensions, multiplied by --scale.
set -euo pipefail
scale=${AGINO_MEMORY_SCALE:-1}
[[ "$scale" == 1 || "$scale" == 2 ]] || { echo 'AGINO_MEMORY_SCALE must be 1 or 2' >&2;exit 2; }
tests=$(cd "$(dirname "$0")" && pwd)
unset QT_SCALE_FACTOR AGINO_KEYBOARD_SCALE
if [[ "$scale" == 1 ]];then
 exec "$tests/nested-session.sh" env QT_SCALE_FACTOR=1 AGINO_KEYBOARD_SCALE=1 "$@"
fi
# Keep frozen upstream nested-session.sh unchanged. Its sibling helper paths
# remain available through links inside this private temporary directory.
private=$(mktemp -d /tmp/agino-wayland-memory-session.XXXXXX)
trap 'rm -f "$private/nested-session.sh" "$private/sogou-setup.sh";rmdir "$private"' EXIT
pattern='--width 1280 --height 800'
[[ $(grep -Fo -- "$pattern" "$tests/nested-session.sh" | wc -l) == 1 ]] || { echo 'Expected exactly one KWin logical geometry pattern' >&2; exit 2; }
sed 's/--width 1280 --height 800/--width 1280 --height 800 --scale 2/' "$tests/nested-session.sh" >"$private/nested-session.sh"
ln -s "$tests/sogou-setup.sh" "$private/sogou-setup.sh"
chmod +x "$private/nested-session.sh"
"$private/nested-session.sh" env QT_SCALE_FACTOR=1 AGINO_KEYBOARD_SCALE=1 "$@"
