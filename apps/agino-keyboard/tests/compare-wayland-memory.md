# Private Wayland memory comparison

`compare-wayland-memory.py` imports the existing full-application process and
input helpers from `compare-renderer-memory.py`. It starts each application
sequentially inside the same disposable KWin/Fcitx/GTK session and sums every
application-owned PID, identified by PID and start time. Shared test services
are excluded. Every sample rejects Swap, SwapPss or VmSwap above zero. Samples
include USS/PSS/RSS, geometry, renderer, frame timing and optional per-mapping
PSS/RSS/private bytes, device/inode identity and loaded platform libraries.

The full run types English with actual outer-X11 pointer events delivered into
KWin, creates real Pinyin candidates and selects one into the GTK fixture. It
also samples expanded candidates, settings and unloaded/hidden states. Active
samples follow input; idle samples wait 1.5 seconds. 0.1.21 uses the standard Qt cursor, so a theme atlas is expected when Qt
needs it. `--verify-no-cursor-atlas` is a legacy assertion for 0.1.19/0.1.20
and must not be used for current default-cursor acceptance.

`--plain-only` explicitly
skips input checks and is diagnostic only. A successful `result` means the
functional assertions passed; it does **not** assert a relative memory target.

Example, from the repository root (paths must name compatible local builds):

```sh
env TMPDIR=/tmp xvfb-run -a -s '-screen 0 2560x1600x24' \
 apps/agino-keyboard/tests/private-bus-session.sh env AGINO_MEMORY_SCALE=2 \
 apps/agino-keyboard/tests/wayland-memory-session.sh \
 python3 apps/agino-keyboard/tests/compare-wayland-memory.py \
 --qt /tmp/agino-native-validation/qt-native/usr/bin/agino-keyboard \
 --slint /tmp/agino-memory017-validation/slint/usr/bin/agino-keyboard-slint \
 --gtk /tmp/agino-memory017-validation/test-gtk-target \
 --output /tmp/agino-host-wayland-memory/formal-2x/result.json \
 --order slint,qt,qt,slint --cycles 3 \
 --runtime-label host-shared-DDE
```

Dependencies are the existing nested-session dependencies: Xvfb, xauth,
xdotool, xwininfo, DBus, KWin Wayland/Xwayland, matching Fcitx5 Agino addon,
Pinyin and the GTK fixture. The scripts install nothing. They use private
XDG directories, session bus, runtime/socket and a capability-free private
KWin copy. They never restart the desktop KWin/Fcitx or edit user settings.

Set `AGINO_MEMORY_SCALE=1` for 1×. KWin's windowed width/height options are
logical: width 1280 height 800 scale 2 produces a physical 2560×1600 host window.
Both client scale overrides remain 1; forcing Qt scale 2 against compositor 1
is an invalid cross-renderer baseline. `--qt-env WAYLAND_DEBUG=client` and
`--slint-env WAYLAND_DEBUG=client` save protocol logs and a small geometry
summary. Logs can alter performance; use them for diagnostics and leave them
off for final latency work.

Only approved per-client diagnostic environment keys are accepted, including
`QT_WAYLAND_CLIENT_BUFFER_INTEGRATION=shm` and `QT_WAYLAND_DECORATION=none`.
They are experiment inputs, not recommended production settings. The explicit
`--x11-baseline` option can use the existing `private-session.sh` wrapper to
control for SDK shared-library denominators. Do not compare an SDK PSS total
against a host DDE total as if they used identical mapped-library sharing.

The evidence under `out/components/agino-keyboard-validation/native/wayland`
labels pre-final-build runs as diagnostic. It preserves the unsuccessful
client-forced 2× SDK run as an invalid baseline. In the valid host 2× trace,
both implementations allocated two 1868×688 buffers with stride 7472; Qt
reported renderer `qtgui-raster`, Slint's physical geometry 1868×688. Equal
backing-store bytes do not imply equal total process PSS. Qt also mapped
cursor-theme shared-memory pages after the first pointer interaction.


Final host acceptance used the frozen native worker SHA256
`949d1ad6776bcd7965b98440580acfcbea0bf84524fc92a0dbb93402e0ed8690`.
The `formal-{1x,2x}-949d.json` evidence each contains 136 samples and 24 real
input assertions from four cohorts, with three complete input cycles per cohort.
`formal-summary-949d.json` compares the first cycle separately from later cycles
and the aggregate; every state's USS/PSS median is below the Slint reference at
both output scales. No sample has swap, and no Qt sample maps the full cursor
atlas. The narrowest PSS margins are first-cycle active English: 729.5 KiB at
1× and 669 KiB at 2×. RSS remains substantially higher and does not pass the
relative RSS goal. `formal-1x-active-qt-maps.json` records a bounded per-file and
category breakdown explaining shared mapped pages without copying full smaps.

These results apply to the tested host runtime and uniform output scales 1/2.
Cursor fallback conditions preserve Qt's original behavior and can retain the
larger atlas; they have no blanket memory guarantee. Earlier SDK and pre-final
host JSON files remain diagnostic and are not substituted for the final host
cohorts. Formal runs leave protocol debugging off; true 2× is checked by the
2560×1600 compositor host window and 1868×688 Slint physical keyboard geometry,
with native logical geometry 934×344. Earlier diagnostic protocol traces record
the matching buffer scale and stride.
