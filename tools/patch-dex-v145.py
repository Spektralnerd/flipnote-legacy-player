#!/usr/bin/env python3
"""Make the main player's outer background true OLED black.

The v1.4.4 MainActivity builds its root LinearLayout with
Color.rgb(17, 19, 24), which remains visible in the fixed top/bottom margins
around PpmPlayerView. This patch changes only those three RGB arguments to
zero, preserving code size and every other instruction.
"""
from __future__ import annotations

import hashlib
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dex_rewriter import DexModel, Proto, validate_dex  # noqa: E402

MAIN = 'Lde/offlineflipnote/legacy/MainActivity;'
COLOR = 'Landroid/graphics/Color;'
EXPECTED = (17, 19, 24)


def signed16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def refresh_header(data: bytearray) -> None:
    data[0x0C:0x20] = hashlib.sha1(data[0x20:]).digest()
    struct.pack_into('<I', data, 0x08, zlib.adler32(data[0x0C:]) & 0xFFFFFFFF)


def locate_rgb_operands(data: bytes) -> list[int]:
    model = DexModel(data)
    target_class = model.class_by_desc(MAIN)
    methods = [
        method for method in target_class.direct_methods + target_class.virtual_methods
        if method.key.name == 'buildInterface' and method.key.proto == Proto('V', ())
    ]
    if len(methods) != 1 or methods[0].code is None:
        raise ValueError('Expected exactly one MainActivity.buildInterface() method')

    code = methods[0].code
    insns = code.insns
    expected_method = next(
        i for i, method in enumerate(model.methods)
        if method.owner == COLOR
        and method.name == 'rgb'
        and method.proto == Proto('I', ('I', 'I', 'I'))
    )

    # Exact v1.4.4 pattern:
    # const/16 v2, 17; const/16 v3, 19; const/16 v4, 24;
    # invoke-static {v2,v3,v4}, Color.rgb(III)I
    starts = []
    for i in range(len(insns) - 9):
        if (insns[i] & 0xFF) != 0x13:
            continue
        if (insns[i + 2] & 0xFF) != 0x13 or (insns[i + 4] & 0xFF) != 0x13:
            continue
        if (insns[i + 6] & 0xFF) != 0x71:  # invoke-static
            continue
        values = tuple(signed16(insns[pos + 1]) for pos in (i, i + 2, i + 4))
        if values != EXPECTED or insns[i + 7] != expected_method:
            continue
        # Verify registers v2, v3, v4 and invoke register list {v2,v3,v4}.
        regs = tuple((insns[pos] >> 8) & 0xFF for pos in (i, i + 2, i + 4))
        if regs != (2, 3, 4):
            raise ValueError(f'Unexpected RGB registers: {regs}')
        if insns[i + 6] != 0x3071 or insns[i + 8] != 0x0432:
            raise ValueError('Unexpected Color.rgb invoke encoding')
        starts.append(i)

    if len(starts) != 1:
        raise ValueError(f'Expected one root background RGB sequence, found {starts}')

    code_off = None
    # Find this code item's instruction array in the original DEX by matching
    # the complete code-unit sequence. The sequence is unique in this file.
    needle = struct.pack('<%dH' % len(insns), *insns)
    hits = []
    cursor = 0
    while True:
        hit = data.find(needle, cursor)
        if hit < 0:
            break
        hits.append(hit)
        cursor = hit + 1
    if len(hits) != 1:
        raise ValueError(f'Could not uniquely locate buildInterface insns: {hits}')
    code_off = hits[0]
    start = starts[0]
    return [code_off + (start + delta + 1) * 2 for delta in (0, 2, 4)]


def patch(source: bytes) -> bytes:
    validate_dex(source)
    operands = locate_rgb_operands(source)
    before = tuple(signed16(struct.unpack_from('<H', source, off)[0]) for off in operands)
    if before != EXPECTED:
        raise ValueError(f'Unexpected source RGB operands: {before}')

    out = bytearray(source)
    for off in operands:
        struct.pack_into('<H', out, off, 0)
    refresh_header(out)
    result = bytes(out)
    validate_dex(result)

    after = tuple(signed16(struct.unpack_from('<H', result, off)[0]) for off in operands)
    if after != (0, 0, 0):
        raise ValueError(f'RGB patch failed: {after}')

    # Only the three 16-bit literal operands and the DEX header may differ.
    allowed = set(range(0x08, 0x20))
    for off in operands:
        allowed.update((off, off + 1))
    unexpected = [i for i, (a, b) in enumerate(zip(source, result)) if a != b and i not in allowed]
    if unexpected:
        raise ValueError(f'Unexpected DEX changes at {unexpected[:16]}')
    return result


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(f'Usage: {Path(sys.argv[0]).name} INPUT OUTPUT')
    source = Path(sys.argv[1])
    destination = Path(sys.argv[2])
    original = source.read_bytes()
    result = patch(original)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(result)
    print(destination)
    print('old:', len(original), hashlib.sha256(original).hexdigest())
    print('new:', len(result), hashlib.sha256(result).hexdigest())
    print('root background: RGB(17,19,24) -> RGB(0,0,0)')


if __name__ == '__main__':
    main()
