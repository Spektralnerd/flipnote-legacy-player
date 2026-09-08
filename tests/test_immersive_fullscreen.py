#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "native" / "gallery_pager.inc").read_text()

constants = {}
for name in (
    "GALLERY_UI_FLAG_LOW_PROFILE",
    "GALLERY_UI_FLAG_HIDE_NAVIGATION",
    "GALLERY_UI_FLAG_FULLSCREEN",
    "GALLERY_UI_FLAG_LAYOUT_STABLE",
    "GALLERY_UI_FLAG_LAYOUT_HIDE_NAVIGATION",
    "GALLERY_UI_FLAG_LAYOUT_FULLSCREEN",
    "GALLERY_UI_FLAG_IMMERSIVE_STICKY",
):
    match = re.search(rf"#define\s+{name}\s+(0x[0-9A-Fa-f]+|\d+)", source)
    assert match, name
    constants[name] = int(match.group(1), 0)


def flags_for_sdk(sdk: int) -> int:
    c = constants
    if sdk >= 19:
        return (c["GALLERY_UI_FLAG_HIDE_NAVIGATION"]
                | c["GALLERY_UI_FLAG_FULLSCREEN"]
                | c["GALLERY_UI_FLAG_LAYOUT_STABLE"]
                | c["GALLERY_UI_FLAG_LAYOUT_HIDE_NAVIGATION"]
                | c["GALLERY_UI_FLAG_LAYOUT_FULLSCREEN"]
                | c["GALLERY_UI_FLAG_IMMERSIVE_STICKY"])
    if sdk >= 16:
        return (c["GALLERY_UI_FLAG_HIDE_NAVIGATION"]
                | c["GALLERY_UI_FLAG_FULLSCREEN"]
                | c["GALLERY_UI_FLAG_LAYOUT_STABLE"]
                | c["GALLERY_UI_FLAG_LAYOUT_HIDE_NAVIGATION"]
                | c["GALLERY_UI_FLAG_LAYOUT_FULLSCREEN"])
    if sdk >= 14:
        return c["GALLERY_UI_FLAG_LOW_PROFILE"] | c["GALLERY_UI_FLAG_HIDE_NAVIGATION"]
    if sdk >= 11:
        return c["GALLERY_UI_FLAG_LOW_PROFILE"]
    return 0

assert flags_for_sdk(9) == 0
assert flags_for_sdk(11) == 1
assert flags_for_sdk(14) == 3
assert flags_for_sdk(16) == 1798
assert flags_for_sdk(19) == 5894
assert flags_for_sdk(36) == 5894

for token in (
    '"setSystemUiVisibility"',
    '"getInsetsController"',
    '"android/view/WindowInsets$Type"',
    '"systemBars"',
    '"setSystemBarsBehavior"',
    '"hide"',
    "GALLERY_SYSTEM_BARS_BEHAVIOR_TRANSIENT",
    '"setNavigationBarContrastEnforced"',
):
    assert token in source, token

assert "if (sdk >= 30)" in source
assert "if (sdk >= 14 && sdk < 19" in source
assert "gallery_activity_has_window_focus" in source

# Immersive mode must be requested before gallery attachment waits for the
# first measured layout, otherwise the gesture bar could remain visible for a
# full layout cycle and the pager would be measured against the wrong height.
tick = source.index("void Java_de_offlineflipnote_legacy_MainActivity_galleryTick")
maintain = source.index("gallery_maintain_immersive(env, activity);", tick)
attach = source.index("gallery_attach(env, activity)", tick)
assert maintain < attach

print("PASS: API 9 fullscreen fallback, API 14 navigation hiding, API 19 sticky immersive and API 30 WindowInsetsController")
