#!/usr/bin/env python3
"""Source-level regression checks for v1.4.13: ZIP folders, 1000-entry limit,
remembered start folder. The behaviour itself is exercised by host_zip_test.c."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BROWSER = (ROOT / "native" / "flipbrowser.c").read_text(encoding="utf-8")
ZIP = (ROOT / "native" / "zip_archive.inc").read_text(encoding="utf-8")
PAGER = (ROOT / "native" / "gallery_pager.inc").read_text(encoding="utf-8")
PREFS = (ROOT / "native" / "browser_preferences.inc").read_text(encoding="utf-8")
ECLAIR = (ROOT / "native" / "eclair_browser.inc").read_text(encoding="utf-8")
STUBS = (ROOT / "native" / "fake" / "libc_stubs.c").read_text(encoding="utf-8")
BUILD = (ROOT / "tools" / "build-native.sh").read_text(encoding="utf-8")

# The archive module stays self-contained: no libz, only libc calls that the
# freestanding build declares and the link stubs export.
assert "zlib" not in ZIP.lower() and "inflateInit" not in ZIP
for symbol in ("fwrite", "rename", "unlink", "rmdir", "mkdir", "open", "close", "read", "lseek64"):
    assert f" {symbol}(" in BROWSER.split("#else", 1)[1].split("#endif", 1)[0], symbol
    assert f" {symbol}(" in STUBS, symbol
assert "-lz" not in BUILD
# 64-bit archive positions (v1.4.14): no 32-bit stdio seeks or 31-bit offset
# caps in the archive reader; only cache files still use stdio.
READER = ZIP.split("static int zip_write_file", 1)[0]
for needle in ("fseek(", "ftell(", 'fopen(path, "rb")', "file_size32(path)", "offset > 0x7fffffffu",
               "file_size >= 0x7fffffff"):
    assert needle not in READER, needle
for needle in ("int64_t local_offset;", "static int bigfile_read_at(", "#define big_lseek lseek64",
               "BIG_OPEN_FLAGS 0x20000", "static int zip_read_i64(", "int64_t* offset)"):
    assert needle in ZIP, needle
assert "case '/': return (uint8_t)(1 << imin(4, imax(0, row - 1)));" in BROWSER
assert 'ZIP NICHT LESBAR - KEIN ZIP ODER DEFEKT' in BROWSER and 'MAX 2 GB' not in BROWSER

for needle in (
    "#define MAX_ITEMS 1000",
    'MAX_ITEMS_TEXT "MAXIMAL 1000 EINTRAEGE ANGEZEIGT"',
    '#include "zip_archive.inc"',
    "static int scan_insert(",           # sorted truncation instead of readdir order
    "static int scan_zip_entries(",
    "zip_split_path(path, zip_path, inner)",
    "candidate.is_archive = 1;",
    "static void draw_archive(",
    '"ZIP-ARCHIV"',
    '"NICHT UNTERSTUETZT"',
    "decode_zip_thumbnail(s, it, thumb)",
    "create_zip_animation(s, it, thumb, monotonicish_millis())",
    "zip_cache_ensure(cache_path, s->zip->path, loc)",
    "return_selected_file(s, env, cache_path)",
    "browser_target_readable(s->last_directory)",
    "browser_read_cache_dir(s, env)",
    "zip_cache_wipe_if_fresh(s->cache_root)",
    "browser_save_last_directory(s, env)",
    "if (s->zip) zip_archive_free(s->zip);",
):
    assert needle in BROWSER, needle

for needle in (
    "#define ZIP_SIG_EOCD64 0x06064b50u",
    "static int zip_locate_central(",
    "static int zip_parse_extra_zip64(",
    "static int zip_normalize_name(",
    "static int zip_inflate(",
    "static int inflate_dynamic(",
    "static void inflate_build_fast(",
    "zip_crc_update(0, out, wanted) != loc->crc",
    "#define ZIP_CACHE_RING 16",
    "static int zip_cache_ensure(",
    "static void zip_cache_wipe_if_fresh(",
    "static int zip_split_path(",
    'copy_text(temp + strlen(dest), ".part"',
):
    assert needle in ZIP, needle

for needle in (
    "ZipLocator locators[MAX_ITEMS];",
    "char virtual_directory[MAX_PATH];",
    "static int gallery_materialize(int index)",
    "if (!gallery_materialize(index)) return 0;",
    "g_gallery_runtime.from_zip",
):
    assert needle in PAGER, needle
assert PAGER.count("if (!gallery_materialize(index)) return 0;") == 2

for needle in ('"last_directory"', '"getCacheDir"', '"putString"', '"getString"'):
    assert needle in PREFS, needle
for needle in ("browser_read_cache_dir(&b->state, env)", "browser_save_last_directory(&b->state,env)",
               "zip_archive_free(b->state.zip)"):
    assert needle in ECLAIR, needle


def keep_sorted_beginning(names, limit):
    """Model of scan_insert(): the listing is the sorted beginning of the
    folder, independent of the order in which entries arrive."""
    kept = []
    for name in names:
        if len(kept) < limit:
            kept.append(name)
            continue
        worst = max(kept, key=str.lower)
        if name.lower() < worst.lower():
            kept[kept.index(worst)] = name
    return sorted(kept, key=str.lower)


names = [f"F{i:04d}.ppm" for i in range(2500)]
for order in (names, names[::-1], names[1::2] + names[::2]):
    assert keep_sorted_beginning(order, 999) == names[:999]


def split_virtual(path, is_zip_file):
    """Model of zip_split_path(): the first .zip component that is a file."""
    parts = path.split("/")
    for i in range(1, len(parts) + 1):
        prefix = "/".join(parts[:i])
        if prefix.lower().endswith(".zip") and is_zip_file(prefix):
            return prefix, "/".join(parts[i:]).strip("/")
    return None


files = {"/sdcard/archiv.zip", "/sdcard/x/y.ZIP"}
assert split_virtual("/sdcard/archiv.zip/0300", files.__contains__) == ("/sdcard/archiv.zip", "0300")
assert split_virtual("/sdcard/archiv.zip", files.__contains__) == ("/sdcard/archiv.zip", "")
assert split_virtual("/sdcard/x/y.ZIP/a/b/", files.__contains__) == ("/sdcard/x/y.ZIP", "a/b")
assert split_virtual("/sdcard/ordner.zip/0300", files.__contains__) is None
assert split_virtual("/sdcard/Flipnotes", files.__contains__) is None

print("PASS: ZIP folders with 64-bit positions, sorted 1000-entry limit, cache hand-over, remembered folder wired into browser, pager and API-7 host")
