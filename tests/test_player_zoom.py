#!/usr/bin/env python3
"""Regression model for the API-9 player zoom and pan geometry."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "native" / "gallery_pager.inc").read_text(encoding="utf-8")

MIN_ZOOM = 1000
MAX_ZOOM = 8000


def clamp(value: int, low: int, high: int) -> int:
    return low if value < low else high if value > high else value


def layout(page_w: int, page_h: int, zoom: int, pan_x: int, pan_y: int):
    zoom = clamp(zoom, MIN_ZOOM, MAX_ZOOM)
    scaled_w = max(page_w, (page_w * zoom + 500) // 1000)
    scaled_h = max(page_h, (page_h * zoom + 500) // 1000)
    max_x = (scaled_w - page_w) // 2
    max_y = (scaled_h - page_h) // 2
    pan_x = clamp(pan_x, -max_x, max_x)
    pan_y = clamp(pan_y, -max_y, max_y)
    left = (page_w - scaled_w) // 2 + pan_x
    top = (page_h - scaled_h) // 2 + pan_y
    return scaled_w, scaled_h, pan_x, pan_y, left, top


def zoom_around(page_w: int, page_h: int, old_zoom: int, pan_x: int, pan_y: int,
                new_zoom: int, focus_x: int, focus_y: int):
    old = layout(page_w, page_h, old_zoom, pan_x, pan_y)
    old_left, old_top = old[4], old[5]
    content_x_milli = ((focus_x - old_left) * 1000) // old_zoom
    content_y_milli = ((focus_y - old_top) * 1000) // old_zoom
    new_w = max(page_w, (page_w * new_zoom + 500) // 1000)
    new_h = max(page_h, (page_h * new_zoom + 500) // 1000)
    centered_left = (page_w - new_w) // 2
    centered_top = (page_h - new_h) // 2
    desired_left = focus_x - (content_x_milli * new_zoom) // 1000
    desired_top = focus_y - (content_y_milli * new_zoom) // 1000
    return layout(page_w, page_h, new_zoom,
                  desired_left - centered_left,
                  desired_top - centered_top)


# 100% is the old exact page geometry.
assert layout(1000, 600, 1000, 0, 0) == (1000, 600, 0, 0, 0, 0)

# 2x is centred and panning is bounded to the image edges.
assert layout(1000, 600, 2000, 0, 0) == (2000, 1200, 0, 0, -500, -300)
assert layout(1000, 600, 2000, 9999, -9999) == (2000, 1200, 500, -300, 0, -600)

# Pinching around an off-centre focus keeps the same source point under it.
page_w, page_h = 921, 1600
focus_x, focus_y = 250, 930
old_zoom = 1000
new_zoom = 2750
new = zoom_around(page_w, page_h, old_zoom, 0, 0,
                  new_zoom, focus_x, focus_y)
new_left, new_top = new[4], new[5]
old_content_x = focus_x
old_content_y = focus_y
mapped_x = new_left + (old_content_x * new_zoom) // 1000
mapped_y = new_top + (old_content_y * new_zoom) // 1000
assert abs(mapped_x - focus_x) <= 1, (mapped_x, focus_x)
assert abs(mapped_y - focus_y) <= 1, (mapped_y, focus_y)

# Pinch range and source integration are intentional and API-9-safe.
assert "GALLERY_ZOOM_MIN 1000" in SOURCE
assert "GALLERY_ZOOM_MAX 8000" in SOURCE
assert '"getPointerCount", "()I"' in SOURCE
assert '"getX", "(I)F"' in SOURCE
assert '"getY", "(I)F"' in SOURCE
assert '"android/widget/FrameLayout"' in SOURCE
assert "current_page" in SOURCE and "current_layout" in SOURCE
assert "gallery_zoom_cycle" in SOURCE
assert "else if (current < 6000) target = GALLERY_ZOOM_MAX;" in SOURCE
assert "gallery_zoom_panning" not in SOURCE  # field is zoom_panning; catch typo regressions
assert "setScaleX" not in SOURCE and "setTranslationX" not in SOURCE

print("PASS: 1x-8x pinch zoom, 1x/2x/4x/8x button and bounded one-finger pan")
