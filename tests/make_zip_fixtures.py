#!/usr/bin/env python3
"""Builds the ZIP fixtures for host_zip_test.c and the JNI host test.

usage: make_zip_fixtures.py OUT_DIR SYNTHETIC.ppm TRANSLATION.ppm

Everything is generated deterministically so the C test can hard-code the
expected structure. Besides ordinary Python zipfile output, a few archives
are written by hand to cover ZIP64 central-directory fields, encrypted
entries, prepended data and archive comments.
"""
import bz2
import os
import random
import struct
import sys
import zipfile
import zlib
from pathlib import Path


def crc(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def write_python_zip(path: Path, entries, compression, level=None):
    with zipfile.ZipFile(path, "w", compression=compression, compresslevel=level) as z:
        for name, data in entries:
            info = zipfile.ZipInfo(name, date_time=(2026, 9, 8, 12, 0, 0))
            info.compress_type = compression
            z.writestr(info, data)


class HandZip:
    """Minimal ZIP writer with optional ZIP64 records and raw flags."""

    def __init__(self):
        self.body = bytearray()
        self.central = []

    def add(self, name, data, method=8, flags=0, zip64=False, raw=None):
        raw_name = name.encode("utf-8")
        if raw is None:
            if method == 8:
                comp = zlib.compressobj(9, zlib.DEFLATED, -15)
                raw = comp.compress(data) + comp.flush()
            elif method == 0:
                raw = data
            elif method == 12:
                raw = bz2.compress(data)
            else:
                raise ValueError(method)
        offset = len(self.body)
        local_extra = b""
        usize = len(data)
        csize = len(raw)
        if zip64:
            local_extra = struct.pack("<HHQQ", 1, 16, usize, csize)
            l_usize = l_csize = 0xFFFFFFFF
        else:
            l_usize, l_csize = usize, csize
        self.body += struct.pack(
            "<IHHHHHIIIHH", 0x04034B50, 45 if zip64 else 20, flags, method,
            0, 0, crc(data), l_csize, l_usize, len(raw_name), len(local_extra))
        self.body += raw_name + local_extra + raw
        self.central.append((raw_name, flags, method, crc(data), csize, usize, offset, zip64))

    def finish(self, path: Path, prepend=b"", comment=b"", force_zip64_eocd=False):
        cd = bytearray()
        for raw_name, flags, method, c, csize, usize, offset, zip64 in self.central:
            extra = b""
            if zip64:
                extra = struct.pack("<HHQQQ", 1, 24, usize, csize, offset)
                c_usize = c_csize = c_offset = 0xFFFFFFFF
            else:
                c_usize, c_csize, c_offset = usize, csize, offset
            cd += struct.pack(
                "<IHHHHHHIIIHHHHHII", 0x02014B50, 45, 45 if zip64 else 20, flags,
                method, 0, 0, c, c_csize, c_usize, len(raw_name), len(extra), 0,
                0, 0, 0, c_offset)
            cd += raw_name + extra
        cd_offset = len(self.body)
        out = bytearray(prepend) + self.body + cd
        count = len(self.central)
        if force_zip64_eocd or any(e[7] for e in self.central):
            record_pos = len(out)
            out += struct.pack("<IQHHIIQQQQ", 0x06064B50, 44, 45, 45, 0, 0,
                               count, count, len(cd), cd_offset)
            out += struct.pack("<IIQI", 0x07064B50, 0, record_pos - len(prepend), 1)
            out += struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, 0xFFFF, 0xFFFF,
                               0xFFFFFFFF, 0xFFFFFFFF, len(comment)) + comment
        else:
            out += struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, count, count,
                               len(cd), cd_offset, len(comment)) + comment
        path.write_bytes(bytes(out))


def write_sparse_zip(path: Path, filler_size: int, entries, zip64: bool) -> bool:
    """Writes an archive whose first entry is a huge stored file of zeros
    (a hole in a sparse file), followed by the given PPM entries and the
    central directory. With zip64 the sizes and offsets use ZIP64 records.
    Returns False when the file system does not keep the file sparse."""
    with open(path, "wb") as f:
        central = []
        # Filler entry: local header, then a hole of filler_size bytes.
        name = b"filler/leer.bin"
        if zip64:
            extra = struct.pack("<HHQQ", 1, 16, filler_size, filler_size)
            f.write(struct.pack("<IHHHHHIIIHH", 0x04034B50, 45, 0, 0, 0, 0, 0,
                                0xFFFFFFFF, 0xFFFFFFFF, len(name), len(extra)) + name + extra)
        else:
            f.write(struct.pack("<IHHHHHIIIHH", 0x04034B50, 20, 0, 0, 0, 0, 0,
                                filler_size, filler_size, len(name), 0) + name)
        central.append((name, 0, 0, filler_size, filler_size, 0))
        f.seek(filler_size, 1)
        for entry_name, data, method in entries:
            raw_name = entry_name.encode("utf-8")
            if method == 8:
                comp = zlib.compressobj(9, zlib.DEFLATED, -15)
                raw = comp.compress(data) + comp.flush()
            else:
                raw = data
            offset = f.tell()
            f.write(struct.pack("<IHHHHHIIIHH", 0x04034B50, 20, 0, method, 0, 0,
                                crc(data), len(raw), len(data), len(raw_name), 0) + raw_name + raw)
            central.append((raw_name, method, crc(data), len(raw), len(data), offset))
        cd_offset = f.tell()
        for raw_name, method, c, csize, usize, offset in central:
            big = zip64 and (csize >= 0xFFFFFFFF or usize >= 0xFFFFFFFF or offset >= 0xFFFFFFFF)
            extra = b""
            c_usize, c_csize, c_offset = usize, csize, offset
            if big:
                fields = []
                if usize >= 0xFFFFFFFF:
                    fields.append(usize); c_usize = 0xFFFFFFFF
                if csize >= 0xFFFFFFFF:
                    fields.append(csize); c_csize = 0xFFFFFFFF
                if offset >= 0xFFFFFFFF:
                    fields.append(offset); c_offset = 0xFFFFFFFF
                extra = struct.pack("<HH", 1, 8 * len(fields)) + b"".join(struct.pack("<Q", v) for v in fields)
            f.write(struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 45, 45 if big else 20, 0,
                                method, 0, 0, c, c_csize, c_usize, len(raw_name), len(extra),
                                0, 0, 0, 0, c_offset) + raw_name + extra)
        cd_size = f.tell() - cd_offset
        count = len(central)
        if zip64:
            record_pos = f.tell()
            f.write(struct.pack("<IQHHIIQQQQ", 0x06064B50, 44, 45, 45, 0, 0,
                                count, count, cd_size, cd_offset))
            f.write(struct.pack("<IIQI", 0x07064B50, 0, record_pos, 1))
            f.write(struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, 0xFFFF, 0xFFFF,
                                0xFFFFFFFF, 0xFFFFFFFF, 0))
        else:
            f.write(struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, count, count,
                                cd_size, cd_offset, 0))
    st = os.stat(path)
    if st.st_blocks * 512 > 64 * 1024 * 1024:
        path.unlink()
        print("Hinweis: kein sparse-Dateisystem, grosse Archive uebersprungen:", path.name)
        return False
    return True


def pattern_data(size: int, seed: int) -> bytes:
    rng = random.Random(seed)
    words = [bytes(rng.choice(b"abcdefghijklmnopqrstuvwxyz") for _ in range(rng.randint(2, 9)))
             for _ in range(300)]
    out = bytearray()
    while len(out) < size:
        out += rng.choice(words) + b" "
        if rng.random() < 0.01:
            out += b"\n"
    return bytes(out[:size])


def main() -> None:
    out = Path(sys.argv[1])
    synthetic = Path(sys.argv[2]).read_bytes()
    translation = Path(sys.argv[3]).read_bytes()
    out.mkdir(parents=True, exist_ok=True)

    # 1. Hundreds folders, nested folder, junk, unsafe names (deflate).
    entries = []
    for folder, count in (("0000", 5), ("0100", 3), ("0200", 4)):
        for i in range(count):
            entries.append((f"{folder}/F{folder}{i:02d}.ppm", synthetic if i % 2 else translation))
    entries.append(("0100/unter/TIEF00.ppm", translation))
    entries.append(("0100/notizen.txt", b"kein flipnote"))
    entries.append(("0300/", b""))               # directory entry only
    entries.append(("./0300/PUNKT00.ppm", synthetic))  # leading ./ is stripped
    entries.append(("../BOESE.ppm", synthetic))  # must be ignored
    entries.append(("WURZEL.PPM", translation))   # upper-case extension at root
    write_python_zip(out / "archiv-deflate.zip", entries, zipfile.ZIP_DEFLATED, 9)
    # 2. Same content stored.
    write_python_zip(out / "archiv-stored.zip", entries, zipfile.ZIP_STORED)

    # 3. Hand-written ZIP64 central directory + ZIP64 EOCD.
    hz = HandZip()
    hz.add("0000/Z64A.ppm", synthetic, method=8, zip64=True)
    hz.add("0000/Z64B.ppm", translation, method=0, zip64=True)
    hz.add("0000/NORMAL.ppm", translation, method=8, zip64=False)
    hz.finish(out / "archiv-zip64.zip", force_zip64_eocd=True)

    # 4. Unsupported entries: bzip2 method and an "encrypted" flag.
    hz = HandZip()
    hz.add("0000/BZIP2.ppm", synthetic, method=12)
    hz.add("0000/GEHEIM.ppm", translation, method=8, flags=1)
    hz.add("0000/OFFEN.ppm", translation, method=8)
    hz.finish(out / "archiv-methoden.zip")

    # 5. Prepended data (self-extractor style) and an archive comment.
    hz = HandZip()
    hz.add("0000/VORNE.ppm", synthetic, method=8)
    hz.add("0000/HINTEN.ppm", translation, method=0)
    hz.finish(out / "archiv-vorspann.zip", prepend=b"#!/bin/sh\nSELBSTENTPACKER\n" * 40,
              comment=b"Kommentar mit PK\x05\x06 Signatur im Text")

    # 6. Empty archive and garbage with a .zip name.
    hz = HandZip()
    hz.finish(out / "archiv-leer.zip")
    (out / "kaputt.zip").write_bytes(b"Das ist keine ZIP-Datei " * 100)

    # 7. Big folder for the sorted truncation test: 1200 PPM entries.
    big = [(f"viele/V{i:04d}.ppm", translation) for i in range(1200)]
    write_python_zip(out / "archiv-gross.zip", big, zipfile.ZIP_DEFLATED, 1)

    # 8. Inflate round-trip payloads (named .ppm so they are indexed).
    payloads = {
        "eins.ppm": b"A",
        "hundert.ppm": bytes(range(100)),
        "block.ppm": pattern_data(65536 + 1, 1),
        "text300k.ppm": pattern_data(300 * 1024, 2),
        "zufall300k.ppm": random.Random(3).randbytes(300 * 1024),
        "rle.ppm": b"\x00" * 40000 + b"ab" * 20000 + b"\xff" * 33000,
        "weit.ppm": (pattern_data(32768, 4) * 3),
    }
    data_dir = out / "daten"
    data_dir.mkdir(exist_ok=True)
    for name, blob in payloads.items():
        (data_dir / name).write_bytes(blob)
    for level in (0, 1, 6, 9):
        with zipfile.ZipFile(out / f"daten-l{level}.zip", "w",
                             compression=zipfile.ZIP_DEFLATED, compresslevel=level) as z:
            for name, blob in payloads.items():
                z.writestr(name, blob)
    with zipfile.ZipFile(out / "daten-stored.zip", "w", compression=zipfile.ZIP_STORED) as z:
        for name, blob in payloads.items():
            z.writestr(name, blob)

    # 9. A directory that merely looks like an archive plus a plain archive
    #    next to real files, for the file-system listing tests.
    tree = out / "ordner"
    (tree / "echt.zip").mkdir(parents=True, exist_ok=True)
    (tree / "echt.zip" / "IMORDNER.ppm").write_bytes(translation)
    (tree / "lose.ppm").write_bytes(synthetic)
    (tree / "notiz.txt").write_bytes(b"x")
    (tree / "unterordner").mkdir(exist_ok=True)
    write_python_zip(tree / "archiv.zip",
                     [("0000/A.ppm", synthetic), ("0000/B.ppm", translation)],
                     zipfile.ZIP_DEFLATED, 6)
    # 10. Archives beyond the old 2 GB limit (sparse, nearly no disk use):
    #     a 2.15 GB archive with plain 32-bit offsets past 2^31, and a 4.4 GB
    #     archive that needs ZIP64 sizes, offsets and end records.
    if os.environ.get("FLIPNOTE_ZIP_BIG", "1") != "0":
        big_entries = [("0000/GROSS00.ppm", synthetic, 0), ("0000/GROSS01.ppm", translation, 8),
                       ("0100/GROSS02.ppm", synthetic, 8)]
        write_sparse_zip(out / "archiv-2gb.zip", 2_150_000_000, big_entries, zip64=False)
        write_sparse_zip(out / "archiv-4gb.zip", 4_400_000_000, big_entries, zip64=True)
    print("ZIP-Fixtures:", out)


if __name__ == "__main__":
    main()
