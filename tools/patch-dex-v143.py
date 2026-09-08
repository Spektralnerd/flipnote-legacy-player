#!/usr/bin/env python3
"""Add the native MainActivity.onBackPressed() override for v1.4.3."""
from __future__ import annotations

import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dex_rewriter import (  # noqa: E402
    ACC_NATIVE,
    ACC_PUBLIC,
    DexModel,
    EncodedMethod,
    MethodKey,
    Proto,
    validate_dex,
)


def patch(source: bytes) -> bytes:
    model = DexModel(source)
    main_desc = 'Lde/offlineflipnote/legacy/MainActivity;'
    main = model.class_by_desc(main_desc)
    on_back = MethodKey(main_desc, 'onBackPressed', Proto('V', ()))
    model.add_method_ref(on_back)
    existing = {method.key for method in main.direct_methods + main.virtual_methods}
    if on_back in existing:
        raise ValueError('MainActivity.onBackPressed already exists')
    main.virtual_methods.append(EncodedMethod(on_back, ACC_PUBLIC | ACC_NATIVE, None))
    built = model.build()
    validate_dex(built)
    return built


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(f'Usage: {Path(sys.argv[0]).name} INPUT OUTPUT')
    source = Path(sys.argv[1])
    destination = Path(sys.argv[2])
    result = patch(source.read_bytes())
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(result)
    print(destination)
    print('old:', len(source.read_bytes()), hashlib.sha256(source.read_bytes()).hexdigest())
    print('new:', len(result), hashlib.sha256(result).hexdigest())


if __name__ == '__main__':
    main()
