#!/bin/sh
# Build from the binary reference, keeping all existing player method bodies.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BASE="$ROOT/reference/base-v1.4.11.apk"
OUT=${1:-"$ROOT/build/Flipnote-Legacy-Player-v1.4.16-Gingerbread-Performance-Test.apk"}
mkdir -p "$ROOT/build"
python3 - "$BASE" "$ROOT/build" <<'PY'
import sys,zipfile
from pathlib import Path
with zipfile.ZipFile(sys.argv[1]) as z:
 d=Path(sys.argv[2]);(d/'base.dex').write_bytes(z.read('classes.dex'));(d/'base-manifest.xml').write_bytes(z.read('AndroidManifest.xml'))
PY
sh "$ROOT/tools/build-native.sh"
python3 "$ROOT/tools/patch-manifest-v1416.py" "$ROOT/build/base-manifest.xml" "$ROOT/build/AndroidManifest-v1.4.16.xml"
# 1.4.16: native mixer, device-class UI tick (on top of the 1.4.15 Dalvik fixes and the 1.4.12 bridge).
python3 "$ROOT/tools/patch-dex-v1416.py" "$ROOT/build/base.dex" "$ROOT/build/classes-v1.4.16.dex"
if [ -n "${SIGN_CERT:-}" ] && [ -n "${SIGN_KEY:-}" ]; then
 python3 "$ROOT/tools/build-apk.py" --base "$BASE" \
  --manifest "$ROOT/build/AndroidManifest-v1.4.16.xml" --dex "$ROOT/build/classes-v1.4.16.dex" \
  --libs "$ROOT/build/libs" --output "$OUT" --cert "$SIGN_CERT" --key "$SIGN_KEY"
else
 python3 "$ROOT/tools/build-apk.py" --base "$BASE" \
  --manifest "$ROOT/build/AndroidManifest-v1.4.16.xml" --dex "$ROOT/build/classes-v1.4.16.dex" \
  --libs "$ROOT/build/libs" --output "$OUT"
 echo 'UNSIGNIERT: Zur Installation wird ein eigener Signaturschluessel benoetigt.'
fi
