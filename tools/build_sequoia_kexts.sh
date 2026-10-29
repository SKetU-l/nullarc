#!/bin/sh
# Cross-build the two NullArc kexts for macOS 15 (Sequoia) from Linux.
# Selects the private-vtable forwarders checked against Sequoia 24H32.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TOOLCHAIN=${TOOLCHAIN:-$HOME/.cache/phx-toolchain}
MKSDK=${MKSDK:-$TOOLCHAIN/MacKernelSDK}
CC=${CC:-clang}
CXX=${CXX:-clang++}
LD64=${LD64:-$(find "$TOOLCHAIN/cctools/bin" -maxdepth 1 -name 'x86_64-apple-darwin*-ld' -print -quit 2>/dev/null)}
OUT=${OUT:-$ROOT/build/sequoia}

[ -f "$MKSDK/Headers/IOKit/IOService.h" ] || { echo "MacKernelSDK not found at $MKSDK" >&2; exit 1; }
[ -x "$LD64" ] || { echo "Apple ld64 not found (set LD64)" >&2; exit 1; }

case "$OUT" in ""|/) echo "unsafe output directory: $OUT" >&2; exit 1;; esac
rm -rf "$OUT/obj" "$OUT/PhxBringup.kext" "$OUT/PhxAccel.kext"
mkdir -p "$OUT/obj/bringup" "$OUT/obj/accel" \
  "$OUT/PhxBringup.kext/Contents/MacOS" "$OUT/PhxAccel.kext/Contents/MacOS"

COMMON="-target x86_64-apple-macos11 -mmacosx-version-min=11.0 -mkernel -nostdinc -I$MKSDK/Headers
  -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
  -fno-builtin -fno-common -fno-stack-protector -Os -Wall"
CXX_COMMON="$COMMON -std=c++17 -fapple-kext -fno-rtti -fno-exceptions -fno-c++-static-destructors
  -Wno-unused-parameter -Wno-inconsistent-missing-override"
LIB="$MKSDK/Library/universal"

$CC -x c $COMMON -c "$ROOT/src/phxbringup/kmod_info.c" -o "$OUT/obj/bringup/kmod_info.o"
$CXX $CXX_COMMON -c "$ROOT/src/phxbringup/PhxBringup.cpp" -o "$OUT/obj/bringup/PhxBringup.o"
$LD64 -arch x86_64 -kext -static -macos_version_min 11.0 -no_deduplicate -L"$LIB" \
  "$OUT/obj/bringup/PhxBringup.o" -lkmodc++ "$OUT/obj/bringup/kmod_info.o" -lkmod \
  -o "$OUT/PhxBringup.kext/Contents/MacOS/PhxBringup"
cp "$ROOT/src/phxbringup/Info.plist" "$OUT/PhxBringup.kext/Contents/Info.plist"

$CXX $CXX_COMMON -O2 -I"$ROOT/src/phxaccel" -I"$ROOT/src/phxaccel/accel" -I"$ROOT/src/phxaccel/accel/iofam" \
  -I"$ROOT/src/phxaccel/accel/re" -DNMX_ACCEL -DNM_SEQUOIA -DPHX_FB \
  -Wno-unused-function -Wno-unused-variable -c "$ROOT/src/phxaccel/accel/nvrm-accel.cpp" -o "$OUT/obj/accel/accel.o"
$CXX $CXX_COMMON -O2 -DNM_SEQUOIA -I"$ROOT/src/phxaccel" -c "$ROOT/src/phxaccel/accel/phxfb.cpp" -o "$OUT/obj/accel/phxfb.o"
$CXX $CXX_COMMON -O2 -I"$ROOT/src/phxaccel" -c "$ROOT/src/phxaccel/accel/phxagdc.cpp" -o "$OUT/obj/accel/phxagdc.o"
$LD64 -arch x86_64 -kext -static -macos_version_min 11.0 -no_deduplicate -L"$LIB" \
  "$OUT/obj/accel/accel.o" "$OUT/obj/accel/phxfb.o" "$OUT/obj/accel/phxagdc.o" -lkmodc++ -lkmod \
  -o "$OUT/PhxAccel.kext/Contents/MacOS/PhxAccel"
cp "$ROOT/src/phxaccel/Info.plist" "$OUT/PhxAccel.kext/Contents/Info.plist"

echo "built experimental Sequoia kexts in $OUT"
echo "run the exact-24H32 ABI checker before installation; see docs/sequoia-abi-status.md"
