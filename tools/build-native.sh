#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SRC="$ROOT/native"
OUT="$ROOT/build/libs"
FAKE="$ROOT/build/fake"
CLANG=${CLANG:-$(command -v clang)}
OBJCOPY=${OBJCOPY:-$(command -v llvm-objcopy || true)}
READELF=${READELF:-$(command -v readelf)}
if [ -z "$OBJCOPY" ] && [ -x /usr/local/swift/usr/bin/llvm-objcopy ]; then
  OBJCOPY=/usr/local/swift/usr/bin/llvm-objcopy
fi
if [ -z "${JAVA_HOME:-}" ]; then
  JAVAC=$(command -v javac || true)
  if [ -n "$JAVAC" ]; then
    JAVA_HOME=$(dirname "$(dirname "$(readlink -f "$JAVAC")")")
  fi
fi
if [ -z "${JAVA_HOME:-}" ] || [ ! -f "$JAVA_HOME/include/jni.h" ]; then
  echo "JAVA_HOME mit jni.h wurde nicht gefunden." >&2
  exit 1
fi
if [ -z "$CLANG" ] || [ -z "$OBJCOPY" ] || [ -z "$READELF" ]; then
  echo "clang, llvm-objcopy und readelf werden benoetigt." >&2
  exit 1
fi
rm -rf "$OUT" "$FAKE"
mkdir -p "$OUT" "$FAKE"
build_one() {
  abi=$1; triple=$2; march=$3
  out="$OUT/$abi"; fake="$FAKE/$abi"
  mkdir -p "$out" "$fake"
  flags="--target=$triple $march -fPIC -Os -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -ffreestanding -nostdinc -I$SRC/include -I$JAVA_HOME/include -I$JAVA_HOME/include/linux"
  # shellcheck disable=SC2086
  "$CLANG" $flags -nostdlib -shared "$SRC/fake/libc_stubs.c" -Wl,-soname,libc.so -Wl,--build-id=none -o "$fake/libc.so"
  # shellcheck disable=SC2086
  "$CLANG" $flags -nostdlib -shared "$SRC/fake/libdl_stubs.c" -Wl,-soname,libdl.so -Wl,--build-id=none -o "$fake/libdl.so"
  # shellcheck disable=SC2086
  "$CLANG" $flags -ffunction-sections -fdata-sections -fvisibility=hidden -c "$SRC/flipbrowser.c" -o "$fake/flipbrowser.o"
  # shellcheck disable=SC2086
  "$CLANG" --target="$triple" $march -nostdlib -shared "$fake/flipbrowser.o" -L"$fake" \
    -Wl,--no-as-needed -ldl -lc -Wl,--as-needed -Wl,--gc-sections \
    -Wl,--build-id=none -Wl,-soname,libflipbrowser.so -Wl,-z,noexecstack \
    -Wl,--hash-style=sysv -Wl,-Bsymbolic -o "$out/libflipbrowser.so"
  "$OBJCOPY" --strip-unneeded "$out/libflipbrowser.so"
  "$READELF" -Ws "$out/libflipbrowser.so" | grep -q ANativeActivity_onCreate
}
build_one armeabi armv5te-none-linux-androideabi7 '-march=armv5te -mfloat-abi=soft'
build_one armeabi-v7a armv7a-none-linux-androideabi7 '-march=armv7-a -mfloat-abi=softfp -mfpu=vfpv3-d16'
build_one x86 i686-none-linux-android7 '-march=i686 -mstackrealign'
build_one arm64-v8a aarch64-none-linux-android21 ''
build_one x86_64 x86_64-none-linux-android21 ''
echo "Native Bibliotheken: $OUT"
