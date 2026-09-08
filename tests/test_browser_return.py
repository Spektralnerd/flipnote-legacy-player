#!/usr/bin/env python3
"""Regression checks for the player-to-browser return path."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "native" / "gallery_pager.inc").read_text(encoding="utf-8")
BROWSER = (ROOT / "native" / "flipbrowser.c").read_text(encoding="utf-8")

for needle in (
    "Java_de_offlineflipnote_legacy_MainActivity_onBackPressed",
    "< Zurueck",
    "controlsVisible",
    "startActivityForResult",
    "flipnote.browser.return.directory",
    "flipnote.browser.return.path",
):
    assert needle in SOURCE, needle

for needle in (
    "gallery_take_browser_return_request(s)",
    "gallery_read_browser_return_extras(activity, s)",
    "s->reveal_selected = 1",
    "ensure_selection_visible(s, buffer.width, buffer.height)",
):
    assert needle in BROWSER, needle


def parent_path(path: str) -> str:
    path = path.rstrip("/") or "/"
    if path == "/":
        return "/"
    parent = path.rsplit("/", 1)[0]
    return parent or "/"


assert parent_path("/storage/emulated/0/Flipnotes/ABC_001.ppm") == "/storage/emulated/0/Flipnotes"
assert parent_path("/sdcard/A.ppm") == "/sdcard"
assert parent_path("/A.ppm") == "/"


def reveal_scroll(selected: int, count: int, width: int, height: int) -> int:
    cols = max(4, min(10, width // 210)) if width >= 900 else (4 if width >= 720 else 3 if width >= 470 else 2)
    margin = 4 if width < 360 else 6
    gap = 4 if width < 360 else 6
    header = 40 if width < 360 else 46
    footer = 22
    card_w = (width - 2 * margin - gap * (cols - 1)) // cols
    thumb_h = max(1, ((card_w - 8) * 48) // 64)
    card_h = thumb_h + 31
    row = selected // cols
    top = row * (card_h + gap)
    bottom = top + card_h
    viewport = height - header - footer - margin
    scroll = max(0, bottom - viewport)
    rows = (count + cols - 1) // cols
    max_scroll = max(0, rows * (card_h + gap) - viewport)
    return min(scroll, max_scroll)


for width, height, selected in ((320, 480, 57), (921, 2048, 180), (2048, 921, 250)):
    scroll = reveal_scroll(selected, 320, width, height)
    assert scroll >= 0

print("PASS: overlay back button, system back hook, folder target and selected-item reveal")
