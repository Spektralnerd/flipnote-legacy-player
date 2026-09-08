#!/usr/bin/env python3
"""Patch the compiled v1.4.9 manifest to v1.4.10 / versionCode 26.

Unlike the earlier equal-length version patches, this rebuilds the UTF-16
string pool because "1.4.10" is one code unit longer than "1.4.9". Existing
string indexes and all intent filters remain unchanged.
"""
from __future__ import annotations

import importlib.util
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_manifest_helpers():
    path = ROOT / "tools" / "patch-manifest-v146.py"
    spec = importlib.util.spec_from_file_location("manifest_helpers", path)
    if not spec or not spec.loader:
        raise RuntimeError(f"Cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def patch_version_code(body: bytes, strings: list[str], helpers) -> bytes:
    result = bytearray(body)
    cursor = 0
    changed = False
    while cursor < len(result):
        chunk_type = helpers.u16(result, cursor)
        chunk_size = helpers.u32(result, cursor + 4)
        if chunk_size < 8 or cursor + chunk_size > len(result):
            raise ValueError(f"Malformed XML chunk at {cursor}")
        if chunk_type == helpers.START_ELEMENT:
            attr_start = helpers.u16(result, cursor + 24)
            attr_size = helpers.u16(result, cursor + 26)
            attr_count = helpers.u16(result, cursor + 28)
            attr = cursor + 16 + attr_start
            for _ in range(attr_count):
                name_index = helpers.u32(result, attr + 4)
                if strings[name_index] == "versionCode":
                    old = helpers.u32(result, attr + 16)
                    if old != 25:
                        raise ValueError(f"Expected versionCode 25, got {old}")
                    struct.pack_into("<I", result, attr + 16, 26)
                    changed = True
                attr += attr_size
        cursor += chunk_size
    if not changed:
        raise ValueError("versionCode attribute not found")
    return bytes(result)


def patch_manifest(original: bytes) -> bytes:
    helpers = load_manifest_helpers()
    if helpers.u16(original, 0) != helpers.XML_TYPE or helpers.u16(original, 2) != 8:
        raise ValueError("Compiled binary Android XML expected")

    pool_offset = 8
    strings, pool_size, _ = helpers.parse_string_pool(original, pool_offset)
    resource_offset = pool_offset + pool_size
    resource_ids, resource_size = helpers.parse_resource_map(original, resource_offset)
    body = original[resource_offset + resource_size:]
    if len(strings) != len(resource_ids):
        raise ValueError("Expected one resource-map entry per string")

    matches = [i for i, text in enumerate(strings) if text == "1.4.9"]
    if len(matches) != 1:
        raise ValueError(f"Expected one 1.4.9 string, found {len(matches)}")
    strings[matches[0]] = "1.4.10"
    body = patch_version_code(body, strings, helpers)

    new_pool = helpers.build_string_pool(strings)
    new_resource_map = helpers.build_resource_map(resource_ids)
    total_size = 8 + len(new_pool) + len(new_resource_map) + len(body)
    result = struct.pack("<HHI", helpers.XML_TYPE, 8, total_size)
    result += new_pool + new_resource_map + body
    if len(result) != total_size:
        raise AssertionError("Binary XML size mismatch")
    return result


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} INPUT OUTPUT")
    source, destination = map(Path, sys.argv[1:])
    destination.parent.mkdir(parents=True, exist_ok=True)
    output = patch_manifest(source.read_bytes())
    destination.write_bytes(output)
    print(f"{destination}: {len(output)} bytes")


if __name__ == "__main__":
    main()
