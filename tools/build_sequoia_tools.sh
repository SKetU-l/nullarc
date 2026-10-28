#!/bin/sh
# Cross-build the bring-up clients with a complete macOS userspace SDK.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TOOLCHAIN=${TOOLCHAIN:-$HOME/.cache/phx-toolchain}
SDK=${SDK:-$TOOLCHAIN/MacOSX15.5.sdk}
CC=${CC:-clang}
LD64=${LD64:-ld64.lld}
OUT=${OUT:-$ROOT/build/sequoia/tools}

[ -f "$SDK/usr/include/stdio.h" ] || { echo "macOS userspace SDK not found at $SDK" >&2; exit 1; }
mkdir -p "$OUT"
for tool in phxctl mesboot; do
    "$CC" -target x86_64-apple-macos15 -isysroot "$SDK" -O2 -Wall -Wextra -Werror \
        -c "$ROOT/tools/phxctl/$tool.c" -o "$OUT/$tool.o"
    "$LD64" -arch x86_64 -platform_version macos 15.0 15.5 -syslibroot "$SDK" \
        -lSystem -framework IOKit -framework CoreFoundation "$OUT/$tool.o" -o "$OUT/$tool"
done
echo "built Sequoia tools in $OUT"
