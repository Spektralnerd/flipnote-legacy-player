#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$ROOT/build/host-tests"
CLANG=${CLANG:-$(command -v clang)}
if [ -z "${JAVA_HOME:-}" ]; then
  JAVAC=$(command -v javac || true)
  if [ -n "$JAVAC" ]; then
    JAVA_HOME=$(dirname "$(dirname "$(readlink -f "$JAVAC")")")
  fi
fi
if [ -z "$CLANG" ] || [ -z "${JAVA_HOME:-}" ]; then
  echo "clang und JAVA_HOME werden benoetigt." >&2
  exit 1
fi
mkdir -p "$OUT"
# _FILE_OFFSET_BITS=64 keeps lseek 64-bit on 32-bit hosts (archives beyond 2 GB).
FLAGS="-O2 -ffunction-sections -fdata-sections -Wall -Wextra -Werror -D_FILE_OFFSET_BITS=64"
if [ "${SANITIZE:-0}" = 1 ]; then
  FLAGS="$FLAGS -O1 -g -fsanitize=address,undefined"
fi
# GCC (CLANG=gcc, e.g. when the clang sanitizer runtime is missing) reports
# maybe-uninitialized false positives in the unchanged pager code.
case "$CLANG" in *gcc*) FLAGS="$FLAGS -Wno-maybe-uninitialized" ;; esac
# shellcheck disable=SC2086
"$CLANG" $FLAGS -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/linux" \
  "$ROOT/tests/host_decoder_test.c" -Wl,--gc-sections -o "$OUT/decoder-test"
# shellcheck disable=SC2086
"$CLANG" $FLAGS -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/linux" \
  "$ROOT/tests/host_animation_stress.c" -Wl,--gc-sections -o "$OUT/stress-test"
# shellcheck disable=SC2086
"$CLANG" $FLAGS -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/linux" \
  "$ROOT/tests/host_zip_test.c" -Wl,--gc-sections -o "$OUT/zip-test"
# v1.4.16: the native mixer against a double-precision oracle (libm only for the oracle).
# shellcheck disable=SC2086
"$CLANG" $FLAGS -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/linux" \
  "$ROOT/tests/host_mixer_test.c" -Wl,--gc-sections -lm -o "$OUT/mixer-test"
"$OUT/decoder-test" \
  "$ROOT/tests/data/synthetic-test.ppm" \
  "$ROOT/tests/data/translation-test.ppm"
"$OUT/stress-test" "$ROOT/tests/data/synthetic-test.ppm"
"$OUT/mixer-test"
# ZIP archives as folders (v1.4.13): fixtures are generated, never committed.
rm -rf "$OUT/zip-fixtures"
python3 "$ROOT/tests/make_zip_fixtures.py" "$OUT/zip-fixtures" \
  "$ROOT/tests/data/synthetic-test.ppm" "$ROOT/tests/data/translation-test.ppm"
"$OUT/zip-test" "$OUT/zip-fixtures" \
  "$ROOT/tests/data/synthetic-test.ppm" "$ROOT/tests/data/translation-test.ppm"
python3 "$ROOT/tests/test_thumbnail_cache.py"
python3 "$ROOT/tests/test_aspect_ratio.py"
python3 "$ROOT/tests/test_gallery_swipe.py"
python3 "$ROOT/tests/test_browser_return.py"
python3 "$ROOT/tests/test_oled_black.py"

python3 "$ROOT/tests/test_player_oled_surround.py"
python3 "$ROOT/tests/test_file_association.py"
python3 "$ROOT/tests/test_metadata_info.py"

python3 "$ROOT/tests/test_player_centering.py"
python3 "$ROOT/tests/test_seekbar_repair.py"
python3 "$ROOT/tests/test_player_zoom.py"
python3 "$ROOT/tests/test_immersive_fullscreen.py"

python3 "$ROOT/tests/test_edge_to_edge_cutout.py"
python3 "$ROOT/tests/test_zip_browser.py"
python3 "$ROOT/tests/test_dalvik_verifier.py"
python3 "$ROOT/tests/test_gingerbread_performance.py"
