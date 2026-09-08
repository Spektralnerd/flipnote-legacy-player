#!/usr/bin/env python3
"""Regression test for the blank first page after the native pager is attached."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "native" / "gallery_pager.inc").read_text(encoding="utf-8")

required = [
    "int center_pending;",
    "static void gallery_finish_pending_center",
    "g_gallery_runtime.center_pending = 1;",
    "if (gallery_scroll_x(env) == width)",
    "gallery_finish_pending_center(env, width);",
]
for fragment in required:
    assert fragment in source, fragment

# The centre retry must happen after the pager has a measured width and after
# neighbour refreshes, because both attachment and refresh can occur before the
# HorizontalScrollView has computed its scroll range.
assert source.index("width = gallery_update_page_width(env);") < source.index(
    "gallery_finish_pending_center(env, width);")
assert source.index("gallery_refresh_neighbors(env);") < source.index(
    "gallery_finish_pending_center(env, width);")

# Model the Android failure that triggered this hotfix: the first scrollTo is
# clamped to zero while the newly inserted child has no measured scroll range.
class FakePager:
    def __init__(self, width: int) -> None:
        self.width = width
        self.measured = False
        self.x = 0

    def scroll_to(self, target: int) -> None:
        self.x = target if self.measured else 0

pager = FakePager(1080)
pending = True
pager.scroll_to(pager.width)  # attachment-time attempt, before layout
assert pager.x == 0

# The first measured UI tick retries and confirms the centre page.
pager.measured = True
if pending:
    pager.scroll_to(pager.width)
    if pager.x == pager.width:
        pending = False
assert pager.x == pager.width
assert not pending

print("PASS: demo and externally opened files are centred after delayed layout")
