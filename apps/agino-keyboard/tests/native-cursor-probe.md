# 历史记录：0.1.19–0.1.20轻量光标验证

0.1.21已按用户要求撤销该优化并删除相关源码/探针。本文保留为历史证据；运行下列旧命令需使用Git提交62606600或0.1.20完整源码，不能对当前版本执行。当前版本使用Qt标准光标。

# Selective native Wayland arrow validation

`NativeCursor` is a Wayland-only optimization. It reads the same Qt platform
cursor theme/size hints, resolves the Qt arrow alias order across the selected
theme and its inheritance, and loads only a static arrow image through optional
`libXcursor.so.1`. Production linking does not require libXcursor; unavailable
libraries, malformed/unreadable themes or unsupported geometry retain the
window's original Qt cursor. No system theme settings are changed.

The resolver follows the pinned Deepin Wayland search order: explicit
`XCURSOR_PATH`, otherwise `XDG_DATA_HOME/icons`, the Xcursor library path and
the additional Deepin Wayland locations `~/.cursors`,
`/usr/share/cursors/xorg-x11`, `/usr/share/icons`. An empty explicit path or
ambiguous theme name falls back to Qt. This runtime path compatibility must
be checked when changing the Wayland packaging. The code intentionally does
not use `XcursorLibraryLoadImages` independently for each alias: that API
implicitly falls back to the default theme too early and can select a
different arrow from an alias-only selected theme.

The helper installs a static bitmap before exposure and matches Qt's rounded
hotspots and integer bitmap scale. Animated arrows retain Qt's original path,
including elapsed-time frame selection and compositor frame-callback gating;
there is no replacement animation timer. The theme monitor stops while hidden. It rechecks platform
hints on theme/style, enter and screen/DPR events plus a visible-only 1-second
cached-hint monitor. An unchanged hint does not reset the cursor or repaint.

Optimization requires a verified compositor version supporting buffer scale
and uniform integer output DPR. A server cursor-shape manager retains Qt’s
server-side cursor path, which already avoids an atlas and may use a different
compositor theme. Mixed-DPI, fractional/ambiguous screen scales
or older compositor protocols retain normal Qt cursor behavior. This avoids
incorrect assumptions about cursor-surface entered outputs when a window
straddles screens. These are optimization limits, not missing input features.

`nativecursor_test.cpp` uses temporary synthetic cursor files to verify exact
pixels, odd hotspot rounding, animated-theme fallback, alias priority, inheritance/cycle handling,
size fallback, restoring the original cursor, mixed/fractional scale guards,
hidden monitor cancellation and child-object teardown. It requires Qt Test,
Qt GuiPrivate, wayland-client and Xcursor development files for fixture
writing; the production helper uses only dynamic optional Xcursor loading.

`native-cursor-probe.cpp` is a separate white QWindow fixture. The Python
runner places the real pointer in it and reads the displayed outer Xvfb cursor
using XFixes, with a protocol pointer-enter assertion. X11 screenshots omit that hardware cursor and are therefore not
accepted as cursor-pixel evidence. The runner asserts identical pixels and
hotspots, an actual bitmap override, nonempty captures and absence of a full
Wayland cursor atlas in the selective process.

With the probe built against `nativecursor.cpp`, run:

```sh
env TMPDIR=/tmp xvfb-run -a -s '-screen 0 2560x1600x24' \
 apps/agino-keyboard/tests/private-bus-session.sh env AGINO_MEMORY_SCALE=2 \
 apps/agino-keyboard/tests/wayland-memory-session.sh \
 python3 apps/agino-keyboard/tests/native-cursor-probe.py \
 --binary /tmp/agino-native-cursor-probe \
 --output /tmp/agino-host-wayland-memory/cursor-helper-2x
```

Use scale 1 for the separate 1× check. Python uses the standard library and the
existing memory-runner helpers; runtime tools are the existing nested-session
dependencies plus X11/XFixes libraries. No downloads or package installations
are performed. Structured results and the small captured cursor PNGs are
preserved under `out/components/agino-keyboard-validation/native/wayland`.

The bitmap audit follows Qt 6.8's [themed cursor update and scale selection](https://github.com/qt/qtwayland/blob/v6.8.0/src/client/qwaylandinputdevice.cpp),
[bitmap upload path](https://github.com/qt/qtwayland/blob/v6.8.0/src/client/qwaylandcursor.cpp)
and [shared-memory format mapping](https://github.com/qt/qtwayland/blob/v6.8.0/src/shared/qwaylandsharedmemoryformathelper_p.h).
Hotspots use QPoint division (rounding), while bitmap DPR remains an exact
integer. Premultiplied ARGB bytes map to Wayland ARGB8888. Indivisible buffer
dimensions retain normal Qt handling. The tests check a translucent pixel,
odd hotspot rounding and a nonsquare bitmap's physical/logical dimensions.
