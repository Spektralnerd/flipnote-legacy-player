#!/usr/bin/env python3
"""Regression checks for the gallery pager's drag/settle geometry."""

THRESHOLD_MIN = 48
THRESHOLD_MAX = 220


def clamp(value: int, low: int, high: int) -> int:
    return max(low, min(high, value))


def scroll_for_drag(width: int, dx: int, current: int, count: int) -> int:
    if (dx > 0 and current <= 0) or (dx < 0 and current + 1 >= count):
        dx = int(dx / 3)  # C integer division truncates toward zero.
    dx = clamp(dx, -width, width)
    return width - dx


def release_direction(width: int, dx: int, elapsed_ms: int, current: int, count: int) -> int:
    threshold = clamp(width // 5, THRESHOLD_MIN, THRESHOLD_MAX)
    direction = 0
    if dx < -threshold or (elapsed_ms < 300 and dx < -(threshold // 2)):
        direction = 1
    elif dx > threshold or (elapsed_ms < 300 and dx > threshold // 2):
        direction = -1
    if direction < 0 and current <= 0:
        direction = 0
    if direction > 0 and current + 1 >= count:
        direction = 0
    return direction


def visible_widths(width: int, scroll: int) -> tuple[int, int, int]:
    # Page intervals: previous [0,w], current [w,2w], next [2w,3w].
    viewport_left = scroll
    viewport_right = scroll + width
    result = []
    for left in (0, width, width * 2):
        right = left + width
        result.append(max(0, min(viewport_right, right) - max(viewport_left, left)))
    return tuple(result)


for width in (240, 320, 480, 921, 2048):
    # Half-swipe to the next note: exactly half current and half next visible.
    scroll = scroll_for_drag(width, -(width // 2), current=4, count=10)
    previous, current, next_ = visible_widths(width, scroll)
    assert previous == 0
    assert abs(current - width // 2) <= 1, (width, current)
    assert abs(next_ - (width - width // 2)) <= 1, (width, next_)

    # Half-swipe to the previous note: exactly half previous and half current.
    scroll = scroll_for_drag(width, width // 2, current=4, count=10)
    previous, current, next_ = visible_widths(width, scroll)
    assert next_ == 0
    assert abs(previous - width // 2) <= 1, (width, previous)
    assert abs(current - (width - width // 2)) <= 1, (width, current)

    # Slow short drags snap back; long and quick flicks commit.
    threshold = clamp(width // 5, THRESHOLD_MIN, THRESHOLD_MAX)
    assert release_direction(width, -(threshold // 3), 500, 4, 10) == 0
    assert release_direction(width, -(threshold + 1), 500, 4, 10) == 1
    assert release_direction(width, threshold + 1, 500, 4, 10) == -1
    assert release_direction(width, -(threshold // 2 + 1), 200, 4, 10) == 1
    assert release_direction(width, threshold // 2 + 1, 200, 4, 10) == -1

    # First/last item cannot be left behind; drag receives edge resistance.
    assert release_direction(width, threshold + 1, 200, 0, 10) == 0
    assert release_direction(width, -(threshold + 1), 200, 9, 10) == 0
    assert scroll_for_drag(width, width, 0, 10) == width - width // 3
    assert scroll_for_drag(width, -width, 9, 10) == width + width // 3

print('PASS: interactive half-page drag, thresholds, flicks and edge resistance')
