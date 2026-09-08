#!/bin/sh
# Build from the binary reference, keeping all existing player method bodies.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BASE="$ROOT/reference/base-v1.4.11.apk"
OUT=${1:-"$ROOT/build/Flipnote-Legacy-Player-v1.4.12-Android-2.1-Browser-Test.apk"}
mkdir -p "$ROOT/build"
python3 - "$BASE" "$ROOT/build" <<'PY'
import sys,zipfile
from pathlib import Path
with zipfile.ZipFile(sys.argv[1]) as z:
 d=Path(sys.argv[2]);(d/'base.dex').write_bytes(z.read('classes.dex'));(d/'base-manifest.xml').write_bytes(z.read('AndroidManifest.xml'))
PY
sh "$ROOT/tools/build-native.sh"
python3 "$ROOT/tools/patch-manifest-v1412.py" "$ROOT/build/base-manifest.xml" "$ROOT/build/AndroidManifest-v1.4.12.xml"
python3 "$ROOT/tools/patch-dex-v1412.py" "$ROOT/build/base.dex" "$ROOT/build/classes-v1.4.12.dex"
if [ -n "${SIGN_CERT:-}" ] && [ -n "${SIGN_KEY:-}" ]; then
 python3 "$ROOT/tools/build-apk.py" --base "$BASE" \
  --manifest "$ROOT/build/AndroidManifest-v1.4.12.xml" --dex "$ROOT/build/classes-v1.4.12.dex" \
  --libs "$ROOT/build/libs" --output "$OUT" --cert "$SIGN_CERT" --key "$SIGN_KEY"
else
 python3 "$ROOT/tools/build-apk.py" --base "$BASE" \
  --manifest "$ROOT/build/AndroidManifest-v1.4.12.xml" --dex "$ROOT/build/classes-v1.4.12.dex" \
  --libs "$ROOT/build/libs" --output "$OUT"
 echo 'UNSIGNIERT: Zur Installation wird ein eigener Signaturschluessel benoetigt.'
fi
