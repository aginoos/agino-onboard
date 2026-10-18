#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real Qt AT-SPI bridge; run through native-accessibility-atspi.sh."""
import datetime
import json
import os
from pathlib import Path
import time

import pyatspi
from gi.repository import GLib

EVENTS = []
CHECKS = {}


def event_received(event):
    try:
        name = event.source.name
    except Exception:
        name = "<retired>"
    EVENTS.append({"type": event.type, "name": name, "detail1": event.detail1,
                   "detail2": event.detail2})


def pump(seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        while GLib.MainContext.default().pending():
            GLib.MainContext.default().iteration(False)
        time.sleep(0.02)


def walk(accessible):
    yield accessible
    for child in accessible:
        if child is not None:
            yield from walk(child)


def check(name, condition):
    CHECKS[name] = bool(condition)
    if not condition:
        raise AssertionError(name)


def exercise():
    pyatspi.Registry.registerEventListener(
        event_received, "object:state-changed", "object:property-change:accessible-value",
        "object:property-change:accessible-name", "object:bounds-changed", "object:children-changed")
    root = None
    for _ in range(150):
        found = [obj for obj in walk(pyatspi.Registry.getDesktop(0))
                 if obj.name == "Toggle test"]
        if found:
            root = found[0].parent
            break
        pump(0.02)
    check("native_tree_discovered", root is not None)
    application_name = root.parent.name
    children = list(root)
    check("roles", [obj.getRole() for obj in children]
          == [pyatspi.ROLE_PUSH_BUTTON, pyatspi.ROLE_SLIDER, pyatspi.ROLE_PUSH_BUTTON])
    toggle, slider, candidate = children
    check("names", [obj.name for obj in children] == ["Toggle test", "Opacity test", "Candidate test"])
    root_bounds = root.queryComponent().getExtents(pyatspi.DESKTOP_COORDS)
    button_bounds = toggle.queryComponent().getExtents(pyatspi.DESKTOP_COORDS)
    check("global_rect", (button_bounds.x, button_bounds.y, button_bounds.width, button_bounds.height)
          == (root_bounds.x + 10, root_bounds.y + 10, 100, 30))
    action = toggle.queryAction()
    names = [action.getName(index).lower() for index in range(action.nActions)]
    check("press_and_toggle_exposed", "press" in names and "toggle" in names)
    check("toggle_action_accepted", action.doAction(names.index("toggle")))
    pump(0.3)
    check("toggle_checked", toggle.getState().contains(pyatspi.STATE_CHECKED))
    check("press_action_accepted", action.doAction(names.index("press")))
    pump(0.2)
    check("press_unchecked", not toggle.getState().contains(pyatspi.STATE_CHECKED))
    value = slider.queryValue()
    check("initial_opacity", value.currentValue == 80)
    value.currentValue = 85
    pump(0.3)
    check("opacity_readback", value.currentValue == 85)
    pump(5)
    check("candidate_removed", root.childCount == 2 and all(obj.name != "Candidate test" for obj in root))
    moved = toggle.queryComponent().getExtents(pyatspi.DESKTOP_COORDS)
    check("moved_rect_readback", moved.x == root_bounds.x + 15 and moved.y == root_bounds.y + 10)
    check("renamed_slider_readback", slider.name == "Renamed opacity test")
    try:
        result = candidate.queryAction().doAction(0)
    except Exception:
        rejected = True  # A retired object path may legitimately disappear.
    else:
        rejected = not result
    check("stale_action_rejected", rejected)
    pump(3)
    remaining = [app for app in pyatspi.Registry.getDesktop(0)
                 if app.name == application_name]
    check("destroyed_window_absent", not remaining or remaining[0].childCount == 0)
    check("checked_on_event", any(event["type"] == "object:state-changed:checked"
          and event["name"] == "Toggle test" and event["detail1"] == 1 for event in EVENTS))
    check("checked_off_event", any(event["type"] == "object:state-changed:checked"
          and event["name"] == "Toggle test" and event["detail1"] == 0 for event in EVENTS))
    check("opacity_value_event", any(event["type"] == "object:property-change:accessible-value"
          and event["name"] == "Opacity test" for event in EVENTS))
    check("name_change_event", any(event["type"] == "object:property-change:accessible-name"
          and event["name"] == "Renamed opacity test" for event in EVENTS))


if __name__ == "__main__":
    error = None
    try:
        exercise()
    except Exception as exc:
        error = f"{type(exc).__name__}: {exc}"
    result = {"timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "status": "PASS" if error is None else "FAIL", "checks": CHECKS,
              "error": error, "events": EVENTS, "orca_tested": False,
              "children_changed_event_received": any(event["type"].startswith("object:children-changed")
                                                      for event in EVENTS),
              "bounds_changed_event_received": any(event["type"].startswith("object:bounds-changed")
                                                    for event in EVENTS)}
    destination = Path(os.environ["AGINO_ATSPI_EVIDENCE_DIR"]) / "result.json"
    destination.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": result["status"], "checks": CHECKS, "error": error}, ensure_ascii=False), flush=True)
    raise SystemExit(0 if error is None else 1)
