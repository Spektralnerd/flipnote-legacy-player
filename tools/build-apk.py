#!/usr/bin/env python3
"""Assemble the v1.4.10 immersive-fullscreen APK with optional classic V1 signing."""
from __future__ import annotations
import argparse
import base64
import hashlib
import struct
import zipfile
from pathlib import Path


def aligned_extra(current_offset: int, name: str, compress_type: int) -> bytes:
    if compress_type != zipfile.ZIP_STORED:
        return b''
    base = current_offset + 30 + len(name.encode('utf-8'))
    need = (-base) % 4
    return b'' if need == 0 else struct.pack('<HH', 0xFFFF, need) + b'\0' * need


def make_manifest(files: dict[str, bytes]) -> tuple[bytes, bytes]:
    manifest = b'Manifest-Version: 1.0\r\n\r\n'
    sections: list[bytes] = []
    for name in sorted(files):
        digest = base64.b64encode(hashlib.sha1(files[name]).digest()).decode('ascii')
        section = f'Name: {name}\r\nSHA1-Digest: {digest}\r\n\r\n'.encode('ascii')
        manifest += section
        sections.append(section)
    signature_file = (
        b'Signature-Version: 1.0\r\n'
        b'Created-By: 1.0 (Android)\r\n'
        b'SHA1-Digest-Manifest: '
        + base64.b64encode(hashlib.sha1(manifest).digest())
        + b'\r\n\r\n'
    )
    for section in sections:
        signature_file += (
            section.split(b'\r\n', 1)[0]
            + b'\r\nSHA1-Digest: '
            + base64.b64encode(hashlib.sha1(section).digest())
            + b'\r\n\r\n'
        )
    return manifest, signature_file


def sign_signature_file(signature_file: bytes, cert_path: Path, key_path: Path) -> bytes:
    try:
        from asn1crypto import algos, cms, core, pem, x509
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import padding
    except ImportError as exc:
        raise SystemExit(
            'Signing needs the packages in requirements-signing.txt. '
            'Install them with: pip install -r requirements-signing.txt'
        ) from exc

    cert_pem = cert_path.read_bytes()
    key_pem = key_path.read_bytes()
    _type, _headers, cert_der = pem.unarmor(cert_pem)
    cert = x509.Certificate.load(cert_der)
    key = serialization.load_pem_private_key(key_pem, password=None)
    signature = key.sign(signature_file, padding.PKCS1v15(), hashes.SHA1())
    sha1_algorithm = algos.DigestAlgorithm({'algorithm': 'sha1', 'parameters': core.Null()})
    rsa_algorithm = algos.SignedDigestAlgorithm({'algorithm': 'rsassa_pkcs1v15', 'parameters': core.Null()})
    signer = cms.SignerInfo({
        'version': 'v1',
        'sid': {'issuer_and_serial_number': {
            'issuer': cert.issuer,
            'serial_number': cert.serial_number,
        }},
        'digest_algorithm': sha1_algorithm,
        'signature_algorithm': rsa_algorithm,
        'signature': signature,
    })
    signed = cms.SignedData({
        'version': 'v1',
        'digest_algorithms': [sha1_algorithm],
        'encap_content_info': {'content_type': 'data'},
        'certificates': [cms.CertificateChoices({'certificate': cert})],
        'signer_infos': [signer],
    })
    return cms.ContentInfo({'content_type': 'signed_data', 'content': signed}).dump()


def write_apk(output: Path, files: dict[str, bytes], compression: dict[str, int], signatures: dict[str, bytes]) -> None:
    preferred = [
        'AndroidManifest.xml',
        'res/drawable-mdpi-v4/ic_launcher.png',
        'res/drawable-hdpi-v4/ic_launcher.png',
        'res/drawable-xhdpi-v4/ic_launcher.png',
        'resources.arsc', 'assets/synthetic-test.ppm', 'classes.dex',
        'lib/armeabi/libflipbrowser.so',
        'lib/armeabi-v7a/libflipbrowser.so',
        'lib/x86/libflipbrowser.so',
        'lib/arm64-v8a/libflipbrowser.so',
        'lib/x86_64/libflipbrowser.so',
    ]
    extras = sorted(set(files) - set(preferred))
    signature_order = ['META-INF/FLIPNOTE.SF', 'META-INF/FLIPNOTE.RSA', 'META-INF/MANIFEST.MF']
    output.unlink(missing_ok=True)
    with zipfile.ZipFile(output, 'w', allowZip64=False) as archive:
        for name in preferred + extras + signature_order:
            if name not in files and name not in signatures:
                continue
            data = files.get(name, signatures.get(name))
            method = compression.get(name, zipfile.ZIP_DEFLATED)
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.compress_type = method
            info.create_system = 0
            info.external_attr = 0
            info.extra = aligned_extra(archive.fp.tell(), name, method)
            archive.writestr(info, data, compress_type=method, compresslevel=9)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--base', required=True, type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--dex', required=True, type=Path)
    parser.add_argument('--libs', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--cert', type=Path, help='PEM certificate for classic V1 signing')
    parser.add_argument('--key', type=Path, help='Unencrypted PEM private key')
    args = parser.parse_args()
    if (args.cert is None) != (args.key is None):
        parser.error('--cert and --key must be supplied together')

    files: dict[str, bytes] = {}
    compression: dict[str, int] = {}
    with zipfile.ZipFile(args.base) as source:
        for info in source.infolist():
            if info.filename.upper().startswith('META-INF/') or info.filename.endswith('/'):
                continue
            files[info.filename] = (
                args.manifest.read_bytes()
                if info.filename == 'AndroidManifest.xml'
                else args.dex.read_bytes()
                if info.filename == 'classes.dex'
                else source.read(info.filename)
            )
            compression[info.filename] = info.compress_type

    for abi in ('armeabi', 'armeabi-v7a', 'x86', 'arm64-v8a', 'x86_64'):
        name = f'lib/{abi}/libflipbrowser.so'
        files[name] = (args.libs / abi / 'libflipbrowser.so').read_bytes()
        compression[name] = zipfile.ZIP_DEFLATED

    signatures: dict[str, bytes] = {}
    if args.cert:
        manifest, signature_file = make_manifest(files)
        signature_block = sign_signature_file(signature_file, args.cert, args.key)
        signatures = {
            'META-INF/FLIPNOTE.SF': signature_file,
            'META-INF/FLIPNOTE.RSA': signature_block,
            'META-INF/MANIFEST.MF': manifest,
        }
        for name in signatures:
            compression[name] = zipfile.ZIP_DEFLATED

    write_apk(args.output, files, compression, signatures)
    print(args.output)
    print('SHA-256:', hashlib.sha256(args.output.read_bytes()).hexdigest())
    if not signatures:
        print('Hinweis: Diese Ausgabe ist unsigniert und kann nicht installiert werden.')


if __name__ == '__main__':
    main()
