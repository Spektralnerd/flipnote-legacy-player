#!/usr/bin/env python3
"""Patch the compiled v1.4.7 manifest to v1.4.8 / versionCode 24."""
from __future__ import annotations

import struct
import sys
from pathlib import Path


def u16(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from('<H', data, offset)[0]


def u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from('<I', data, offset)[0]


def patch_manifest(original: bytes) -> bytes:
    data = bytearray(original)
    if u16(data, 0) != 0x0003 or u16(data, 2) != 8:
        raise ValueError('Compiled binary Android XML expected')

    pool = 8
    if u16(data, pool) != 0x0001:
        raise ValueError('String pool expected')
    header_size = u16(data, pool + 2)
    pool_size = u32(data, pool + 4)
    string_count = u32(data, pool + 8)
    style_count = u32(data, pool + 12)
    flags = u32(data, pool + 16)
    strings_start = u32(data, pool + 20)
    if flags & 0x100 or style_count:
        raise ValueError('Expected unstyled UTF-16 string pool')

    strings: list[str] = []
    text_offsets: list[int] = []
    for index in range(string_count):
        relative = u32(data, pool + header_size + index * 4)
        cursor = pool + strings_start + relative
        length = u16(data, cursor)
        cursor += 2
        if length & 0x8000:
            length = ((length & 0x7FFF) << 16) | u16(data, cursor)
            cursor += 2
        raw = bytes(data[cursor:cursor + length * 2])
        strings.append(raw.decode('utf-16le'))
        text_offsets.append(cursor)

    matches = [i for i, text in enumerate(strings) if text == '1.4.7']
    if len(matches) != 1:
        raise ValueError(f'Expected one v1.4.7 string, found {len(matches)}')
    version_name_index = matches[0]
    replacement = '1.4.8'.encode('utf-16le')
    old_raw = '1.4.7'.encode('utf-16le')
    pos = text_offsets[version_name_index]
    if bytes(data[pos:pos + len(old_raw)]) != old_raw:
        raise ValueError('Version name bytes do not match')
    data[pos:pos + len(replacement)] = replacement
    strings[version_name_index] = '1.4.7'

    resource_map = pool + pool_size
    if u16(data, resource_map) != 0x0180:
        raise ValueError('Resource map expected')
    cursor = resource_map + u32(data, resource_map + 4)
    patched_code = False

    while cursor < len(data):
        chunk_type = u16(data, cursor)
        chunk_size = u32(data, cursor + 4)
        if chunk_size < 8 or cursor + chunk_size > len(data):
            raise ValueError(f'Malformed chunk at {cursor}')
        if chunk_type == 0x0102:  # RES_XML_START_ELEMENT_TYPE
            attribute_start = u16(data, cursor + 24)
            attribute_size = u16(data, cursor + 26)
            attribute_count = u16(data, cursor + 28)
            attr = cursor + 16 + attribute_start
            for _ in range(attribute_count):
                name_index = u32(data, attr + 4)
                if strings[name_index] == 'versionCode':
                    value = u32(data, attr + 16)
                    if value != 23:
                        raise ValueError(f'Expected versionCode 23, got {value}')
                    struct.pack_into('<I', data, attr + 16, 24)
                    patched_code = True
                attr += attribute_size
        cursor += chunk_size

    if not patched_code:
        raise ValueError('versionCode attribute not found')
    return bytes(data)


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(f'Usage: {Path(sys.argv[0]).name} INPUT OUTPUT')
    source, destination = map(Path, sys.argv[1:])
    destination.parent.mkdir(parents=True, exist_ok=True)
    result = patch_manifest(source.read_bytes())
    destination.write_bytes(result)
    print(f'{destination}: {len(result)} bytes')


if __name__ == '__main__':
    main()
