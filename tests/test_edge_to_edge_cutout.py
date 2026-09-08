#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "native" / "gallery_pager.inc").read_text()

for name, value in (
    ("GALLERY_CUTOUT_MODE_SHORT_EDGES", 1),
    ("GALLERY_CUTOUT_MODE_ALWAYS", 3),
):
    match = re.search(rf"#define\s+{name}\s+(0x[0-9A-Fa-f]+|\d+)", source)
    assert match, name
    assert int(match.group(1), 0) == value

for token in (
    '"getAttributes"',
    '"setAttributes"',
    '"layoutInDisplayCutoutMode"',
    '"setDecorFitsSystemWindows"',
    '"setFitInsetsTypes"',
    '"setFitInsetsSides"',
):
    assert token in source, token

# Android 9/10 can use short-edges; Android 11+ can request all edges.
assert "sdk >= 30 ? GALLERY_CUTOUT_MODE_ALWAYS" in source
assert ": GALLERY_CUTOUT_MODE_SHORT_EDGES" in source

# Edge-to-edge must be requested before the original player view is reparented
# and before its width is used for the gallery pages.
apply = source.index("static int gallery_apply_immersive")
cutout = source.index('"layoutInDisplayCutoutMode"', apply)
decor = source.index('"setDecorFitsSystemWindows"', apply)
fullscreen = source.index("GALLERY_WINDOW_FLAG_FULLSCREEN", cutout)
assert cutout < fullscreen
assert decor < fullscreen

tick = source.index("void Java_de_offlineflipnote_legacy_MainActivity_galleryTick")
maintain = source.index("gallery_maintain_immersive(env, activity);", tick)
attach = source.index("gallery_attach(env, activity)", tick)
assert maintain < attach

print("PASS: display cutout and system-bar insets are disabled before player measurement")
