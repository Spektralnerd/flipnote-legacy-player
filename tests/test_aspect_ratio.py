#!/usr/bin/env python3
"""Regression checks for the 4:3 browser-thumbnail layout."""
from __future__ import annotations


def layout(width: int, height: int) -> tuple[int, int, int]:
    del height
    if width >= 900:
        cols = max(4, min(10, width // 210))
    else:
        cols = 4 if width >= 720 else 3 if width >= 470 else 2
    margin = 4 if width < 360 else 6
    gap = 4 if width < 360 else 6
    card_w = (width - 2 * margin - gap * (cols - 1)) // cols
    thumb_w = card_w - 8
    thumb_h = max(1, thumb_w * 48 // 64)
    return cols, thumb_w, thumb_h


CASES = [
    (320, 480, 2),
    (480, 800, 3),
    (720, 1280, 4),
    (921, 2048, 4),
    (1280, 720, 6),
    (1848, 841, 8),
    (2048, 921, 9),
    (2560, 1440, 10),
]

for width, height, expected_cols in CASES:
    cols, thumb_w, thumb_h = layout(width, height)
    assert cols == expected_cols, (width, cols, expected_cols)
    # Integer rounding may differ by less than one output pixel.
    assert abs(thumb_w * 3 - thumb_h * 4) <= 3, (width, thumb_w, thumb_h)
    assert thumb_w > 0 and thumb_h > 0

print("PASS: 4:3 thumbnail layout on portrait, landscape and legacy displays")
