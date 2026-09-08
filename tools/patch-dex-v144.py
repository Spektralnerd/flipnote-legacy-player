#!/usr/bin/env python3
"""Map the PPM near-black palette (#0E0E0E) to OLED black (#000000)."""
from __future__ import annotations

import hashlib
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dex_rewriter import DexModel, Proto, validate_dex  # noqa: E402

OLD_COLOR = 0xFF0E0E0E
NEW_COLOR = 0xFF000000
OLD_BYTES = struct.pack('<I', OLD_COLOR)
NEW_BYTES = struct.pack('<I', NEW_COLOR)


def render_frame_color_hits(data: bytes, color: int) -> list[int]:
    model = DexModel(data)
    target_class = model.class_by_desc('Lde/offlineflipnote/legacy/PpmFile;')
    methods = [
        method for method in target_class.direct_methods + target_class.virtual_methods
        if method.key.name == 'renderFrame'
        and method.key.proto == Proto('V', ('I', '[I'))
    ]
    if len(methods) != 1 or methods[0].code is None:
        raise ValueError('Expected exactly one PpmFile.renderFrame(int, int[]) method')
    hits: list[int] = []
    insns = methods[0].code.insns
    for index in range(len(insns) - 2):
        if (insns[index] & 0xFF) != 0x14:  # const vAA, #+BBBBBBBB
            continue
        value = insns[index + 1] | (insns[index + 2] << 16)
        if value == color:
            hits.append(index)
    return hits


def refresh_header(data: bytearray) -> None:
    data[0x0C:0x20] = hashlib.sha1(data[0x20:]).digest()
    struct.pack_into('<I', data, 0x08, zlib.adler32(data[0x0C:]) & 0xFFFFFFFF)


def patch(source: bytes) -> bytes:
    validate_dex(source)
    old_method_hits = render_frame_color_hits(source, OLD_COLOR)
    if old_method_hits != [33, 40, 45]:
        raise ValueError(f'Unexpected near-black constants in renderFrame: {old_method_hits}')

    raw_hits: list[int] = []
    cursor = 0
    while True:
        offset = source.find(OLD_BYTES, cursor)
        if offset < 0:
            break
        raw_hits.append(offset)
        cursor = offset + 1
    if len(raw_hits) != 3:
        raise ValueError(f'Expected exactly three raw #0E0E0E constants, found {raw_hits}')

    out = bytearray(source)
    for offset in raw_hits:
        out[offset:offset + 4] = NEW_BYTES
    refresh_header(out)
    result = bytes(out)
    validate_dex(result)

    if render_frame_color_hits(result, OLD_COLOR):
        raise ValueError('Near-black still present in renderFrame')
    new_hits = render_frame_color_hits(result, NEW_COLOR)
    if new_hits != [33, 40, 45]:
        raise ValueError(f'Unexpected OLED-black constants in renderFrame: {new_hits}')
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
    print('raw color replacements:', original.count(OLD_BYTES), '#0E0E0E -> #000000')


if __name__ == '__main__':
    main()
