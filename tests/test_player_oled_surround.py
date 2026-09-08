#!/usr/bin/env python3
"""Regression check for the main player root background."""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from dex_rewriter import DexModel, Proto, validate_dex  # noqa: E402


def color_rgb_literals(path: Path) -> tuple[int, int, int]:
    data = path.read_bytes()
    validate_dex(data)
    model = DexModel(data)
    main = model.class_by_desc('Lde/offlineflipnote/legacy/MainActivity;')
    methods = [
        method for method in main.direct_methods + main.virtual_methods
        if method.key.name == 'buildInterface' and method.key.proto == Proto('V', ())
    ]
    assert len(methods) == 1 and methods[0].code is not None
    insns = methods[0].code.insns
    rgb_index = next(
        i for i, method in enumerate(model.methods)
        if method.owner == 'Landroid/graphics/Color;'
        and method.name == 'rgb'
        and method.proto == Proto('I', ('I', 'I', 'I'))
    )
    hits = []
    for i in range(len(insns) - 8):
        if ((insns[i] & 0xff), (insns[i + 2] & 0xff),
                (insns[i + 4] & 0xff), (insns[i + 6] & 0xff)) != (0x13, 0x13, 0x13, 0x71):
            continue
        if insns[i + 7] != rgb_index:
            continue
        values = tuple(insns[p + 1] for p in (i, i + 2, i + 4))
        hits.append(values)
    assert len(hits) == 1, hits
    return hits[0]


old_path = ROOT / 'build' / 'classes-v1.4.4.dex'
new_path = ROOT / 'build' / 'classes-v1.4.5.dex'
assert color_rgb_literals(old_path) == (17, 19, 24)
assert color_rgb_literals(new_path) == (0, 0, 0)
assert old_path.stat().st_size == new_path.stat().st_size
print('PASS: outer player background is RGB 0/0/0')
