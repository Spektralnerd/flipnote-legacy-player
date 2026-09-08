#!/usr/bin/env python3
"""Regression checks for the OLED-black PPM palette mapping."""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from dex_rewriter import DexModel, Proto, validate_dex  # noqa: E402

source = (ROOT / 'native' / 'flipbrowser.c').read_text()
assert '0x0e0e0e' not in source.lower(), 'near-black remains in native renderer'
assert '0xffffff, 0x000000, 0xffffff, 0x9c9c9c' in source
assert 'white_paper ? 0xffffffu : 0x000000u' in source
assert 'white_paper ? 0x000000u : 0xffffffu' in source

dex_path = ROOT / 'build' / 'classes-v1.4.5.dex'
if dex_path.exists():
    data = dex_path.read_bytes()
    validate_dex(data)
    assert data.count(bytes.fromhex('0e0e0eff')) == 0
    model = DexModel(data)
    ppm = model.class_by_desc('Lde/offlineflipnote/legacy/PpmFile;')
    methods = [
        method for method in ppm.direct_methods + ppm.virtual_methods
        if method.key.name == 'renderFrame'
        and method.key.proto == Proto('V', ('I', '[I'))
    ]
    assert len(methods) == 1 and methods[0].code is not None
    hits = []
    for index, word in enumerate(methods[0].code.insns[:-2]):
        if (word & 0xFF) == 0x14:
            value = (methods[0].code.insns[index + 1]
                     | (methods[0].code.insns[index + 2] << 16))
            if value == 0xFF000000:
                hits.append(index)
    assert hits == [33, 40, 45], hits

print('PASS: PPM black is RGB 0/0/0 in Java player and native previews')
