#!/usr/bin/env python3
"""Regression model for the visible-thumbnail cache policy in v1.3.9."""

CACHE_LIMIT = 24
ITEM_COUNT = 320
THUMBNAIL_BYTES = 64 * 48 * 4


class Cache:
    def __init__(self) -> None:
        self.loaded = [False] * ITEM_COUNT
        self.stamp = [0] * ITEM_COUNT
        self.clock = 0
        self.count = 0

    def draw(self, first: int, last: int) -> None:
        for index in range(first, last + 1):
            if self.loaded[index]:
                self.clock += 1
                self.stamp[index] = self.clock

    def evict(self, first: int, last: int) -> bool:
        if self.count < CACHE_LIMIT:
            return True
        candidates = [
            index
            for index in range(ITEM_COUNT)
            if self.loaded[index] and not first <= index <= last
        ]
        if not candidates:
            return False
        victim = min(candidates, key=self.stamp.__getitem__)
        self.loaded[victim] = False
        self.count -= 1
        return True

    def load_one(self, first: int, last: int) -> bool:
        for index in range(first, last + 1):
            if not self.loaded[index]:
                self.evict(first, last)
                self.loaded[index] = True
                self.clock += 1
                self.stamp[index] = self.clock
                self.count += 1
                return True
        return False

    def fill_visible(self, first: int, last: int) -> None:
        for _ in range(ITEM_COUNT):
            self.draw(first, last)
            if not self.load_one(first, last):
                break
        assert all(self.loaded[index] for index in range(first, last + 1))


def main() -> None:
    cache = Cache()

    # The 921 x 2048 screenshot protects 52 items (indices 0 through 51).
    cache.fill_visible(0, 51)
    assert cache.count == 52

    # Moving one row down replaces four off-screen entries without growth.
    cache.fill_visible(4, 55)
    assert cache.count == 52
    assert not any(cache.loaded[index] for index in range(4))

    # A distant page remains stable and still uses only one screenful.
    cache.fill_visible(100, 151)
    assert cache.count == 52

    print(
        "PASS: 52 visible thumbnails stay resident; "
        f"cache={cache.count}, bytes={cache.count * THUMBNAIL_BYTES}"
    )


if __name__ == "__main__":
    main()
