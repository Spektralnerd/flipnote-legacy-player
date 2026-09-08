#!/usr/bin/env python3
"""Create the v1.4.6 binary Android manifest.

Changes from v1.4.5:
- versionName 1.4.6 / versionCode 22
- ACTION_VIEW file association for Flipnote Studio .ppm files
- MIME handling for Android's built-in .ppm mapping and generic providers

The script operates directly on compiled Android binary XML so no Android SDK
or aapt is required. Existing string indexes remain stable; new strings are
appended and new intent-filter chunks are inserted into MainActivity.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

NO_INDEX = 0xFFFFFFFF
XML_TYPE = 0x0003
STRING_POOL_TYPE = 0x0001
RESOURCE_MAP_TYPE = 0x0180
START_NS = 0x0100
END_NS = 0x0101
START_ELEMENT = 0x0102
END_ELEMENT = 0x0103
TYPE_STRING = 0x03

ANDROID_ATTR_SCHEME = 0x01010027
ANDROID_ATTR_HOST = 0x01010028
ANDROID_ATTR_PATH_PATTERN = 0x0101002C


def u16(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from('<H', data, offset)[0]


def u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from('<I', data, offset)[0]


def put_u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into('<I', data, offset, value)


def encode_length16(length: int) -> bytes:
    if length < 0x8000:
        return struct.pack('<H', length)
    if length > 0x7FFFFFFF:
        raise ValueError('String too long')
    return struct.pack('<HH', 0x8000 | (length >> 16), length & 0xFFFF)


def parse_string_pool(data: bytes, offset: int) -> tuple[list[str], int, int]:
    if u16(data, offset) != STRING_POOL_TYPE:
        raise ValueError('String pool expected')
    header_size = u16(data, offset + 2)
    chunk_size = u32(data, offset + 4)
    string_count = u32(data, offset + 8)
    style_count = u32(data, offset + 12)
    flags = u32(data, offset + 16)
    strings_start = u32(data, offset + 20)
    if flags & 0x100 or style_count:
        raise ValueError('Expected unstyled UTF-16 string pool')
    strings: list[str] = []
    for index in range(string_count):
        relative = u32(data, offset + header_size + index * 4)
        cursor = offset + strings_start + relative
        length = u16(data, cursor)
        cursor += 2
        if length & 0x8000:
            length = ((length & 0x7FFF) << 16) | u16(data, cursor)
            cursor += 2
        raw = data[cursor:cursor + length * 2]
        strings.append(raw.decode('utf-16le'))
    return strings, chunk_size, header_size


def build_string_pool(strings: list[str]) -> bytes:
    header_size = 28
    offsets = bytearray()
    payload = bytearray()
    for text in strings:
        offsets += struct.pack('<I', len(payload))
        utf16 = text.encode('utf-16le')
        units = len(utf16) // 2
        payload += encode_length16(units)
        payload += utf16
        payload += b'\0\0'
    while len(payload) % 4:
        payload.append(0)
    strings_start = header_size + len(offsets)
    size = strings_start + len(payload)
    header = struct.pack(
        '<HHIIIIII',
        STRING_POOL_TYPE,
        header_size,
        size,
        len(strings),
        0,  # style count
        0,  # UTF-16 flags
        strings_start,
        0,  # styles start
    )
    return header + offsets + payload


def parse_resource_map(data: bytes, offset: int) -> tuple[list[int], int]:
    if u16(data, offset) != RESOURCE_MAP_TYPE:
        raise ValueError('Resource map expected')
    size = u32(data, offset + 4)
    if size < 8 or (size - 8) % 4:
        raise ValueError('Malformed resource map')
    entries = [u32(data, offset + 8 + i * 4) for i in range((size - 8) // 4)]
    return entries, size


def build_resource_map(entries: list[int]) -> bytes:
    size = 8 + len(entries) * 4
    return struct.pack('<HHI', RESOURCE_MAP_TYPE, 8, size) + b''.join(
        struct.pack('<I', value) for value in entries
    )


def typed_string_attr(ns: int, name: int, value: int) -> bytes:
    return struct.pack('<IIIHBBI', ns, name, value, 8, 0, TYPE_STRING, value)


def start_element(name: int, attrs: list[bytes], line: int = 0) -> bytes:
    attr_blob = b''.join(attrs)
    size = 36 + len(attr_blob)
    return (
        struct.pack('<HHIII', START_ELEMENT, 16, size, line, NO_INDEX)
        + struct.pack('<IIHHHHHH', NO_INDEX, name, 20, 20, len(attrs), 0, 0, 0)
        + attr_blob
    )


def end_element(name: int, line: int = 0) -> bytes:
    return struct.pack('<HHIIIII', END_ELEMENT, 16, 24, line, NO_INDEX, NO_INDEX, name)


def child_text_element(element_name: int, android_uri: int, name_attr: int, value: int) -> bytes:
    return (
        start_element(element_name, [typed_string_attr(android_uri, name_attr, value)])
        + end_element(element_name)
    )


def data_element(android_uri: int, attrs: list[tuple[int, int]]) -> bytes:
    packed = [typed_string_attr(android_uri, name_index, value_index)
              for name_index, value_index in attrs]
    return start_element(INDEX['data'], packed) + end_element(INDEX['data'])


def intent_filter(label_value: int, action_value: int, mime_values: list[int] | None = None,
                  data_attrs: list[list[tuple[int, int]]] | None = None) -> bytes:
    android_uri = INDEX['android_uri']
    chunks = [start_element(
        INDEX['intent-filter'],
        [typed_string_attr(android_uri, INDEX['label'], label_value)],
    )]
    chunks.append(child_text_element(INDEX['action'], android_uri, INDEX['name'], action_value))
    chunks.append(child_text_element(
        INDEX['category'], android_uri, INDEX['name'], INDEX['android.intent.category.DEFAULT']))
    if mime_values:
        for mime in mime_values:
            chunks.append(data_element(android_uri, [(INDEX['mimeType'], mime)]))
    if data_attrs:
        for attrs in data_attrs:
            chunks.append(data_element(android_uri, attrs))
    chunks.append(end_element(INDEX['intent-filter']))
    return b''.join(chunks)


def find_main_activity_end(body: bytes, strings: list[str]) -> int:
    cursor = 0
    depth = 0
    activity_stack: list[bool] = []
    in_main = False
    while cursor < len(body):
        chunk_type = u16(body, cursor)
        size = u32(body, cursor + 4)
        if size < 8 or cursor + size > len(body):
            raise ValueError(f'Malformed body chunk at {cursor}')
        if chunk_type == START_ELEMENT:
            name_index = u32(body, cursor + 20)
            element = strings[name_index]
            is_main = False
            if element == 'activity':
                attr_start = u16(body, cursor + 24)
                attr_size = u16(body, cursor + 26)
                attr_count = u16(body, cursor + 28)
                attr = cursor + 16 + attr_start
                for _ in range(attr_count):
                    attr_name = strings[u32(body, attr + 4)]
                    value_type = body[attr + 15]
                    value_data = u32(body, attr + 16)
                    if attr_name == 'name' and value_type == TYPE_STRING:
                        is_main = strings[value_data] == '.MainActivity'
                    attr += attr_size
                activity_stack.append(is_main)
                if is_main:
                    in_main = True
            depth += 1
        elif chunk_type == END_ELEMENT:
            name_index = u32(body, cursor + 20)
            element = strings[name_index]
            if element == 'activity':
                closing_main = activity_stack.pop() if activity_stack else False
                if closing_main:
                    return cursor
                in_main = any(activity_stack)
            depth -= 1
        cursor += size
    raise ValueError('MainActivity closing element not found')


def patch_version_attributes(body: bytes, strings: list[str]) -> bytes:
    result = bytearray(body)
    cursor = 0
    patched_code = False
    while cursor < len(result):
        chunk_type = u16(result, cursor)
        size = u32(result, cursor + 4)
        if chunk_type == START_ELEMENT:
            attr_start = u16(result, cursor + 24)
            attr_size = u16(result, cursor + 26)
            attr_count = u16(result, cursor + 28)
            attr = cursor + 16 + attr_start
            for _ in range(attr_count):
                name_index = u32(result, attr + 4)
                if strings[name_index] == 'versionCode':
                    old = u32(result, attr + 16)
                    if old != 21:
                        raise ValueError(f'Expected versionCode 21, got {old}')
                    put_u32(result, attr + 16, 22)
                    patched_code = True
                attr += attr_size
        cursor += size
    if not patched_code:
        raise ValueError('versionCode not found')
    return bytes(result)


INDEX: dict[str, int] = {}


def patch_manifest(original: bytes) -> bytes:
    global INDEX
    if u16(original, 0) != XML_TYPE or u16(original, 2) != 8:
        raise ValueError('Compiled binary Android XML expected')

    pool_offset = 8
    strings, pool_size, _ = parse_string_pool(original, pool_offset)
    resource_offset = pool_offset + pool_size
    resource_ids, resource_size = parse_resource_map(original, resource_offset)
    body = original[resource_offset + resource_size:]
    if len(resource_ids) != len(strings):
        raise ValueError('Expected one resource-map entry per string')

    # Keep all old indexes stable. Replace the equal-length version string and
    # append only the strings needed by the new filters.
    try:
        version_index = strings.index('1.4.5')
    except ValueError as exc:
        raise ValueError('v1.4.5 version string not found') from exc
    strings[version_index] = '1.4.6'

    additions: list[tuple[str, int]] = [
        ('android.intent.action.VIEW', 0),
        ('Flipnote abspielen', 0),
        ('image/x-portable-pixmap', 0),
        ('application/x-ppm', 0),
        ('application/ppm', 0),
        ('application/octet-stream', 0),
        ('application/vnd.nintendo.flipnote.ppm', 0),
        ('scheme', ANDROID_ATTR_SCHEME),
        ('host', ANDROID_ATTR_HOST),
        ('pathPattern', ANDROID_ATTR_PATH_PATTERN),
        ('content', 0),
        ('*', 0),
        ('.*\\.ppm', 0),
        ('.*\\.PPM', 0),
    ]
    for text, resource_id in additions:
        if text in strings:
            idx = strings.index(text)
            if resource_id and resource_ids[idx] not in (0, resource_id):
                raise ValueError(f'Conflicting resource id for {text}')
            if resource_id:
                resource_ids[idx] = resource_id
        else:
            strings.append(text)
            resource_ids.append(resource_id)

    INDEX = {
        'label': strings.index('label'),
        'name': strings.index('name'),
        'action': strings.index('action'),
        'category': strings.index('category'),
        'data': strings.index('data'),
        'mimeType': strings.index('mimeType'),
        'scheme': strings.index('scheme'),
        'host': strings.index('host'),
        'pathPattern': strings.index('pathPattern'),
        'intent-filter': strings.index('intent-filter'),
        'android_uri': strings.index('http://schemas.android.com/apk/res/android'),
        'android.intent.category.DEFAULT': strings.index('android.intent.category.DEFAULT'),
        'android.intent.action.VIEW': strings.index('android.intent.action.VIEW'),
        'Flipnote abspielen': strings.index('Flipnote abspielen'),
        'content': strings.index('content'),
        'wildcard_host': strings.index('*'),
        'lower_pattern': strings.index('.*\\.ppm'),
        'upper_pattern': strings.index('.*\\.PPM'),
    }

    body = patch_version_attributes(body, strings)
    insertion = find_main_activity_end(body, strings)
    mime_values = [strings.index(value) for value in (
        'image/x-portable-pixmap',
        'application/x-ppm',
        'application/ppm',
        'application/octet-stream',
        'application/vnd.nintendo.flipnote.ppm',
    )]
    mime_filter = intent_filter(
        INDEX['Flipnote abspielen'],
        INDEX['android.intent.action.VIEW'],
        mime_values=mime_values,
    )
    path_filter = intent_filter(
        INDEX['Flipnote abspielen'],
        INDEX['android.intent.action.VIEW'],
        # Path matching only has an effect when both scheme and authority
        # are present. A wildcard authority keeps this filter restricted to
        # content-provider URIs whose path actually ends in .ppm. File URIs
        # and providers with a declared MIME type are covered by the MIME
        # filter above; using a scheme-only fallback would incorrectly claim
        # every untyped file/content URI.
        data_attrs=[
            [(INDEX['scheme'], INDEX['content']),
             (INDEX['host'], INDEX['wildcard_host']),
             (INDEX['pathPattern'], INDEX['lower_pattern'])],
            [(INDEX['scheme'], INDEX['content']),
             (INDEX['host'], INDEX['wildcard_host']),
             (INDEX['pathPattern'], INDEX['upper_pattern'])],
        ],
    )
    new_body = body[:insertion] + mime_filter + path_filter + body[insertion:]
    new_pool = build_string_pool(strings)
    new_resource_map = build_resource_map(resource_ids)
    total_size = 8 + len(new_pool) + len(new_resource_map) + len(new_body)
    result = struct.pack('<HHI', XML_TYPE, 8, total_size) + new_pool + new_resource_map + new_body
    if len(result) != total_size:
        raise AssertionError('Size mismatch')
    return result


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(f'Usage: {Path(sys.argv[0]).name} INPUT OUTPUT')
    source, destination = map(Path, sys.argv[1:])
    result = patch_manifest(source.read_bytes())
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(result)
    print(f'{destination}: {len(result)} bytes')


if __name__ == '__main__':
    main()
