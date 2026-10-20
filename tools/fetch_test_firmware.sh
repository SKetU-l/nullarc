#!/usr/bin/env bash
# Download the Phoenix (Radeon 780M) firmware blobs used by tests/test_rdna3_ucode.cpp
# from upstream linux-firmware. These files are redistributable under AMD's
# LICENSE.amdgpu but are not committed to this repository.
set -euo pipefail

DEST="${1:-tests/fixtures/phoenix}"
BASE="${LINUX_FIRMWARE_URL:-https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/amdgpu}"
FILES=(
  gc_11_0_1_rlc.bin
  gc_11_0_1_pfp.bin
  gc_11_0_1_me.bin
  gc_11_0_1_mec.bin
  gc_11_0_1_imu.bin
  sdma_6_0_1.bin
  dcn_3_1_4_dmcub.bin
)

mkdir -p "$DEST"
for f in "${FILES[@]}"; do
  if [ -s "$DEST/$f" ]; then
    echo "[fixtures] $f already present"
    continue
  fi
  echo "[fixtures] fetching $f"
  curl -fsSL --retry 3 -o "$DEST/$f.tmp" "$BASE/$f"
  mv "$DEST/$f.tmp" "$DEST/$f"
done
echo "[fixtures] done: $DEST"
