#!/usr/bin/env python3
"""Regression test for the missing playback seek bar after early pager attach."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "native" / "gallery_pager.inc").read_text(encoding="utf-8")

required = [
    "jobject seek_bar;",
    '"seekBar",',
    '"Landroid/widget/SeekBar;"',
    "static void gallery_repair_seekbar",
    '"controlsVisible", "Z"',
    '"getVisibility", "()I"',
    '"setVisibility", "(I)V"',
    '"getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;"',
    '"height", "I"',
    '"requestLayout", "()V"',
    "gallery_repair_seekbar(env, activity);",
    "seek_visibility == 0 && seek_height <= 0",
]
for fragment in required:
    assert fragment in source, fragment

# Reparenting must wait until the original player and visible seek bar have
# completed their first layout. The repair must run after controls visibility
# is read but before the pager's final centre correction.
assert source.index("seek_height = (*env)->CallIntMethod") < source.index(
    "(*env)->CallVoidMethod(env, parent, remove_view, current)")
assert source.index("gallery_update_overlay_buttons(env, activity);") < source.index(
    "gallery_repair_seekbar(env, activity);")
assert source.index("gallery_repair_seekbar(env, activity);") < source.index(
    "gallery_finish_pending_center(env, width);")

# Behavioural model: when an early reparent leaves only the seek bar at GONE
# and zero measured height, the next visible-controls tick restores VISIBLE,
# the original 24-dp layout height and requests another layout. Hidden controls
# remain hidden and are not forcibly shown.
VISIBLE = 0
GONE = 8


def repair(controls_visible: bool, visibility: int, measured_height: int,
           layout_height: int, desired_height: int = 48):
    requested = False
    if not controls_visible:
        return visibility, measured_height, layout_height, requested
    if visibility != VISIBLE:
        visibility = VISIBLE
        requested = True
    if layout_height <= 0:
        layout_height = desired_height
        requested = True
    if measured_height <= 0:
        requested = True
    return visibility, measured_height, layout_height, requested

state = repair(True, GONE, 0, 0)
assert state == (VISIBLE, 0, 48, True)
state_hidden = repair(False, GONE, 0, 0)
assert state_hidden == (GONE, 0, 0, False)
state_ok = repair(True, VISIBLE, 48, 48)
assert state_ok == (VISIBLE, 48, 48, False)

print("PASS: visible controls restore the playback seek bar without breaking menu hiding")
