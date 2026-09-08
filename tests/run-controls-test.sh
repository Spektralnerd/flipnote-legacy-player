#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
JAVA_HOME=${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}
mkdir -p "$ROOT/build/host-tests"
SAN=""
if [ "${SANITIZE:-0}" = 1 ];then SAN="-g -fsanitize=address,undefined";fi
clang -O1 -Wall -Wextra -Werror $SAN -ffunction-sections -fdata-sections \
 -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/linux" "$ROOT/tests/host_browser_controls.c" \
 -Wl,--gc-sections -ldl -lpthread -o "$ROOT/build/host-tests/browser-controls"
"$ROOT/build/host-tests/browser-controls" "$ROOT/tests/data/synthetic-test.ppm"
