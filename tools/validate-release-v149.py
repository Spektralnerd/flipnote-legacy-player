#!/usr/bin/env python3
from __future__ import annotations
import base64
import hashlib
import importlib.util
import re
import struct
import sys
import zipfile
import zlib
from pathlib import Path

from asn1crypto import cms, pem, x509
from cryptography import x509 as crypto_x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from dex_rewriter import DexModel, validate_dex  # noqa: E402


def parse_headers(blob: bytes) -> tuple[dict[str, str], dict[str, dict[str, str]]]:
    text = blob.decode("utf-8")
    # JAR manifests use CRLF and continuation lines. None of our generated lines
    # are long enough to fold, but accept folding for completeness.
    logical: list[str] = []
    for line in text.replace("\r\n", "\n").split("\n"):
        if line.startswith(" ") and logical:
            logical[-1] += line[1:]
        else:
            logical.append(line)
    chunks: list[list[str]] = []
    current: list[str] = []
    for line in logical:
        if line == "":
            if current:
                chunks.append(current)
                current = []
        else:
            current.append(line)
    if current:
        chunks.append(current)

    def attrs(lines: list[str]) -> dict[str, str]:
        result: dict[str, str] = {}
        for line in lines:
            key, value = line.split(": ", 1)
            result[key] = value
        return result

    main = attrs(chunks[0]) if chunks else {}
    sections: dict[str, dict[str, str]] = {}
    for chunk in chunks[1:]:
        item = attrs(chunk)
        name = item.pop("Name")
        sections[name] = item
    return main, sections


def manifest_sections_raw(blob: bytes) -> dict[str, bytes]:
    # Preserve exact CRLF bytes because .SF section digests cover the original
    # MANIFEST.MF section including the trailing blank line.
    out: dict[str, bytes] = {}
    cursor = blob.find(b"\r\n\r\n") + 4
    while cursor > 3 and cursor < len(blob):
        end = blob.find(b"\r\n\r\n", cursor)
        if end < 0:
            break
        section = blob[cursor:end + 4]
        first = section.split(b"\r\n", 1)[0]
        if first.startswith(b"Name: "):
            out[first[6:].decode("utf-8")] = section
        cursor = end + 4
    return out


def verify_signature(apk: Path) -> str:
    with zipfile.ZipFile(apk) as z:
        mf = z.read("META-INF/MANIFEST.MF")
        sf = z.read("META-INF/FLIPNOTE.SF")
        rsa = z.read("META-INF/FLIPNOTE.RSA")
        signed_files = {
            info.filename: z.read(info.filename)
            for info in z.infolist()
            if not info.is_dir() and not info.filename.upper().startswith("META-INF/")
        }

    mf_main, mf_sections = parse_headers(mf)
    assert mf_main.get("Manifest-Version") == "1.0"
    assert set(mf_sections) == set(signed_files)
    for name, data in signed_files.items():
        actual = base64.b64encode(hashlib.sha1(data).digest()).decode("ascii")
        assert mf_sections[name]["SHA1-Digest"] == actual, name

    sf_main, sf_sections = parse_headers(sf)
    assert sf_main.get("Signature-Version") == "1.0"
    assert sf_main["SHA1-Digest-Manifest"] == base64.b64encode(
        hashlib.sha1(mf).digest()).decode("ascii")
    raw_sections = manifest_sections_raw(mf)
    assert set(sf_sections) == set(raw_sections)
    for name, section in raw_sections.items():
        actual = base64.b64encode(hashlib.sha1(section).digest()).decode("ascii")
        assert sf_sections[name]["SHA1-Digest"] == actual, name

    content = cms.ContentInfo.load(rsa)
    signed_data = content["content"]
    signer = signed_data["signer_infos"][0]
    cert_choice = signed_data["certificates"][0]
    cert_der = cert_choice.chosen.dump()
    cert = crypto_x509.load_der_x509_certificate(cert_der)
    cert.public_key().verify(
        signer["signature"].native,
        sf,
        padding.PKCS1v15(),
        hashes.SHA1(),
    )
    return ":".join(f"{b:02X}" for b in hashlib.sha256(cert_der).digest())


def verify_alignment(apk: Path) -> None:
    raw = apk.read_bytes()
    with zipfile.ZipFile(apk) as z:
        for info in z.infolist():
            if info.compress_type != zipfile.ZIP_STORED:
                continue
            off = info.header_offset
            name_len, extra_len = struct.unpack_from("<HH", raw, off + 26)
            data_off = off + 30 + name_len + extra_len
            assert data_off % 4 == 0, (info.filename, data_off)


def load_manifest_module():
    spec = importlib.util.spec_from_file_location(
        "manifest_patch", ROOT / "tools" / "patch-manifest-v146.py")
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def manifest_values(data: bytes) -> tuple[dict[str, object], dict[str, object], set[str]]:
    mod = load_manifest_module()
    strings, pool_size, _ = mod.parse_string_pool(data, 8)
    resource_offset = 8 + pool_size
    _, resource_size = mod.parse_resource_map(data, resource_offset)
    body = data[resource_offset + resource_size:]
    root: dict[str, object] = {}
    sdk: dict[str, object] = {}
    permissions: set[str] = set()
    cursor = 0
    while cursor < len(body):
        chunk_type = mod.u16(body, cursor)
        size = mod.u32(body, cursor + 4)
        assert size >= 8 and cursor + size <= len(body)
        if chunk_type == mod.START_ELEMENT:
            tag = strings[mod.u32(body, cursor + 20)]
            attrs: dict[str, object] = {}
            attr_start = mod.u16(body, cursor + 24)
            attr_size = mod.u16(body, cursor + 26)
            attr_count = mod.u16(body, cursor + 28)
            pos = cursor + 16 + attr_start
            for _ in range(attr_count):
                name = strings[mod.u32(body, pos + 4)]
                value_type = body[pos + 15]
                value_data = mod.u32(body, pos + 16)
                attrs[name] = (
                    strings[value_data]
                    if value_type == mod.TYPE_STRING and value_data < len(strings)
                    else value_data
                )
                pos += attr_size
            if tag == "manifest":
                root = attrs
            elif tag == "uses-sdk":
                sdk = attrs
            elif tag == "uses-permission" and isinstance(attrs.get("name"), str):
                permissions.add(attrs["name"])
        cursor += size
    return root, sdk, permissions


def main() -> None:
    apk = Path(sys.argv[1])
    with zipfile.ZipFile(apk) as z:
        bad = z.testzip()
        assert bad is None, bad
        names = set(z.namelist())
        expected_libs = {
            f"lib/{abi}/libflipbrowser.so" for abi in
            ("armeabi", "armeabi-v7a", "x86", "arm64-v8a", "x86_64")
        }
        assert expected_libs.issubset(names)
        manifest = z.read("AndroidManifest.xml")
        dex = z.read("classes.dex")
        dex_sha = hashlib.sha256(dex).hexdigest()
        assert dex_sha == "b9e3c50f4b59f62ff5f973f1eb4796d82deced07b9a3f9d299c7c0677031224d"
        validate_dex(dex)
        model = DexModel(dex)
        assert len(model.strings) == len(set(model.strings))
        assert len(model.types) == len(set(model.types))
        assert len(model.fields) == len(set(model.fields))
        assert len(model.methods) == len(set(model.methods))

    root, sdk, permissions = manifest_values(manifest)
    assert root == {
        "versionCode": 25,
        "versionName": "1.4.9",
        "package": "de.offlineflipnote.legacy",
    }
    assert sdk == {"minSdkVersion": 9, "targetSdkVersion": 28}
    assert "android.permission.INTERNET" not in permissions
    assert "android.permission.WAKE_LOCK" not in permissions
    verify_alignment(apk)
    fingerprint = verify_signature(apk)
    expected_fp = "88:DD:F1:34:7A:2A:6C:94:A7:3C:4C:56:AA:FA:DD:46:65:71:C5:F9:7F:02:DF:7D:D7:8E:D1:E2:56:C3:90:A6"
    assert fingerprint == expected_fp
    print("APK:", apk)
    print("size:", apk.stat().st_size)
    print("sha256:", hashlib.sha256(apk.read_bytes()).hexdigest())
    print("manifest:", root, sdk)
    print("permissions:", sorted(permissions))
    print("dex:", dex_sha, len(model.strings), "strings", len(model.methods), "methods")
    print("certificate:", fingerprint)
    print("PASS: zip, alignment, manifest, DEX and classic V1 signature")


if __name__ == "__main__":
    main()
