#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
JAVA_HOME=${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}
OUT="$ROOT/build/jni-host"
mkdir -p "$OUT/classes" "$OUT/notes"
cp "$ROOT/tests/data/synthetic-test.ppm" "$OUT/notes/"
clang -shared -fPIC -O2 -fvisibility=hidden -DFLIPBROWSER_HOST_TEST=1 \
 -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/linux" \
 "$ROOT/native/flipbrowser.c" -Wl,--no-undefined -o "$OUT/libflipbrowser.so" -ldl -lpthread
find "$ROOT/tests/jni-host" -name '*.java' > "$OUT/sources.txt"
javac -d "$OUT/classes" @"$OUT/sources.txt"
java -Djava.library.path="$OUT" -cp "$OUT/classes" de.offlineflipnote.legacy.BridgeTest "$OUT/notes" 7
java -Djava.library.path="$OUT" -cp "$OUT/classes" de.offlineflipnote.legacy.BridgeTest "$OUT/notes" 8
