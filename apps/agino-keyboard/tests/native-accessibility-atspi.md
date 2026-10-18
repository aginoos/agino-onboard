# Native Qt accessibility integration test

`native-accessibility-atspi.sh /absolute/path/test-native-accessible` starts a private
Xvfb display, private configuration roots, a session bus and a separate AT-SPI bus.
It runs the real Qt accessibility bridge against the C++ adapter fixture using
`--atspi-fixture`; Python pyatspi acts as the assistive client. It does not run Orca
or test spoken announcements. No packages are installed or downloaded by the runner.

Dependencies: Python pyatspi/GI with the Atspi typelib, `at-spi2-registryd`,
`dbus-daemon`, `dbus-run-session`, `xvfb-run` and the Qt xcb platform plugin.
Missing dependencies return **77** so CTest can use `SKIP_RETURN_CODE 77`.
The launcher deliberately starts its own accessibility bus and registry rather
than activating a host desktop service. For an SDK with pre-extracted test
packages, set `AGINO_ATSPI_TEST_PREFIX` to their filesystem root. No vendor package
or binary is included in this repository.

To retain the small logs and JSON report outside the disposable runtime directory:

```sh
AGINO_ATSPI_EVIDENCE_DIR=/absolute/path/evidence \
  tests/native-accessibility-atspi.sh /absolute/path/test-native-accessible
```

The test requires native application/window/child discovery, names/roles/global
rectangles, real Press and Toggle actions, checked-state events, slider readback
and a value event, name-change delivery, updated moved geometry, candidate removal,
rejection of retired actions and removal of a destroyed window from the app tree.
`result.json` records every assertion and received event. Event subscribers request
`object:property-change:accessible-value` explicitly: the Qt 6.8 bridge does not
handle a broad property-change subscription consistently for value updates.

The QtGui-only C++ executable also checks the inactive notifier path without any
AT-SPI dependencies. With an active xcb bridge, its update-handler tests check
correct `QAccessibleStateChangeEvent` and `QAccessibleValueChangeEvent` payloads,
name/location events, silent unchanged paints, identity-only reorder and safe
window deletion during event delivery. Run that executable without the fixture
flag under an accessible display to exercise these assertions.

## Verified SDK dependency caveats

On SDK Qt **6.8.0**, the strict functional assertions above pass, while two Linux
bridge limitations remain visible in the JSON rather than being hidden:

- `ObjectReorder` emits `ChildrenChanged` with a raw string argument. Current
  libatspi rejects its `siis(so)` signature. This is the Qt backend's
  `childrenChanged()` implementation passing `childPath` rather than a DBus
  variant at [upstream source line 1335](https://github.com/qt/qtbase/blob/v6.8.0/src/gui/accessible/linux/atspiadaptor.cpp#L1335).
  The report records both the warning and whether a children-changed event was
  received. Direct tree removal and retired-action rejection remain mandatory.
- The same backend explicitly ignores `QAccessible::LocationChanged`
  ([upstream source line 1299](https://github.com/qt/qtbase/blob/v6.8.0/src/gui/accessible/linux/atspiadaptor.cpp#L1299)).
  The C++ test verifies that the adapter emits this notification correctly, the
  live test requires correct moved-rectangle readback, and
  `bounds_changed_event_received` records actual bridge delivery.

These fields make a future Qt upgrade's bridge fixes observable. They do not
claim full Orca validation or silently replace native Qt with custom DBus code.
For instrumented SDK xcb runs, `ASAN_OPTIONS=detect_leaks=1:handle_segv=0` avoids
observed recursive fatal-signal handling while retaining ASAN/UBSAN memory checks;
the separate NativePanel regression suite uses `-nocrashhandler` for its QtTest executable.
