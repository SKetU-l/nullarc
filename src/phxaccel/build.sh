#!/bin/sh
# Builds PhxAccel.kext (the NullMoth accelerator in its AMD/Tahoe configuration) natively on macOS.
# Usage: ./build.sh <path-to-MacKernelSDK>
set -eu
MKSDK=${1:?usage: build.sh <MacKernelSDK>}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/build/PhxAccel.kext"
rm -rf "$HERE/build"
mkdir -p "$OUT/Contents/MacOS"
clang++ -arch x86_64 -mmacosx-version-min=11.0 -fapple-kext -mkernel -nostdinc -I"$MKSDK/Headers" -I"$HERE" \
  -I"$HERE/accel" -I"$HERE/accel/iofam" -I"$HERE/accel/re" \
  -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT -DNMX_ACCEL -DNM_TAHOE \
  -std=c++17 -fno-rtti -fno-exceptions -fno-builtin -fno-common -O2 \
  -Wall -Wno-unused-parameter -Wno-inconsistent-missing-override -Wno-unused-function -Wno-unused-variable \
  -c "$HERE/accel/nvrm-accel.cpp" -o "$HERE/build/accel.o"
clang++ -arch x86_64 -mmacosx-version-min=11.0 -nostdlib -Xlinker -kext -L"$MKSDK/Library/x86_64" \
  -lkmodc++ -lkmod -lcc_kext "$HERE/build/accel.o" -o "$OUT/Contents/MacOS/PhxAccel"
cp "$HERE/Info.plist" "$OUT/Contents/Info.plist"
codesign --force --sign - "$OUT"
echo "built $OUT"
