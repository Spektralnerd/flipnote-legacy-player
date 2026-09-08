#!/usr/bin/env python3
"""Static regression test for the compiled v1.4.8 Android manifest."""
from __future__ import annotations
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "manifest_patch", ROOT / "tools" / "patch-manifest-v146.py")
assert spec and spec.loader
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

manifest = ROOT / "build" / "AndroidManifest-v1.4.8.xml"
data = manifest.read_bytes()
strings, pool_size, _ = module.parse_string_pool(data, 8)
resource_offset = 8 + pool_size
_, resource_size = module.parse_resource_map(data, resource_offset)
body = data[resource_offset + resource_size:]

cursor = 0
activity: str | None = None
root_values: dict[str, object] = {}
sdk_values: dict[str, object] = {}
filters: list[dict[str, object]] = []

while cursor < len(body):
    chunk_type = module.u16(body, cursor)
    size = module.u32(body, cursor + 4)
    assert size >= 8 and cursor + size <= len(body)
    if chunk_type == module.START_ELEMENT:
        tag = strings[module.u32(body, cursor + 20)]
        attrs: dict[str, object] = {}
        attr_start = module.u16(body, cursor + 24)
        attr_size = module.u16(body, cursor + 26)
        attr_count = module.u16(body, cursor + 28)
        pos = cursor + 16 + attr_start
        for _ in range(attr_count):
            name = strings[module.u32(body, pos + 4)]
            value_type = body[pos + 15]
            value_data = module.u32(body, pos + 16)
            attrs[name] = (
                strings[value_data]
                if value_type == module.TYPE_STRING and value_data < len(strings)
                else value_data
            )
            pos += attr_size
        if tag == "manifest":
            root_values = attrs
        elif tag == "uses-sdk":
            sdk_values = attrs
        elif tag == "activity":
            activity = attrs.get("name") if isinstance(attrs.get("name"), str) else None
        elif tag == "intent-filter":
            filters.append({
                "activity": activity,
                "actions": [],
                "categories": [],
                "data": [],
            })
        elif tag == "action" and filters:
            filters[-1]["actions"].append(attrs.get("name"))
        elif tag == "category" and filters:
            filters[-1]["categories"].append(attrs.get("name"))
        elif tag == "data" and filters:
            filters[-1]["data"].append(attrs)
    elif chunk_type == module.END_ELEMENT:
        tag = strings[module.u32(body, cursor + 20)]
        if tag == "activity":
            activity = None
    cursor += size

assert root_values == {
    "versionCode": 24,
    "versionName": "1.4.8",
    "package": "de.offlineflipnote.legacy",
}
assert sdk_values == {"minSdkVersion": 9, "targetSdkVersion": 28}

view_filters = [
    item for item in filters
    if item["activity"] == ".MainActivity"
    and "android.intent.action.VIEW" in item["actions"]
]
assert len(view_filters) == 2
assert all("android.intent.category.DEFAULT" in item["categories"]
           for item in view_filters)

mime_types = {
    data_item.get("mimeType")
    for item in view_filters
    for data_item in item["data"]
    if "mimeType" in data_item
}
assert {
    "image/x-portable-pixmap",
    "application/x-ppm",
    "application/ppm",
    "application/octet-stream",
    "application/vnd.nintendo.flipnote.ppm",
}.issubset(mime_types)

path_rules = {
    (data_item.get("scheme"), data_item.get("host"), data_item.get("pathPattern"))
    for item in view_filters
    for data_item in item["data"]
    if "pathPattern" in data_item
}
assert ("content", "*", ".*\\.ppm") in path_rules
assert ("content", "*", ".*\\.PPM") in path_rules

# A scheme-only ACTION_VIEW filter would claim unrelated untyped files.
for item in view_filters:
    for data_item in item["data"]:
        assert not (
            data_item.get("scheme") in {"content", "file"}
            and "mimeType" not in data_item
            and "host" not in data_item
        )

print("Dateiverknuepfung/Manifest: OK")
