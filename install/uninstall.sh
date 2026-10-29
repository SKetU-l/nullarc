#!/bin/bash
# nullarc Sequoia uninstaller: sudo ./uninstall.sh. Removes the staged Data-volume files;
# PhxBringup.kext and the boot-args stay in PHXNULLARC (see INSTALL-SEQUOIA.md). Reboot afterwards.
set -uo pipefail
[ "$(id -u)" = 0 ] || { echo "run it with sudo"; exit 1; }
G=/Users/Shared/gpu
H="/Library/Application Support/NullArc/PhxGPU"
L=/Library/GPUBundles

echo "== boot-time bring-up"
mkdir -p "$G"; touch "$G/phxboot-off"
rm -f /Library/LaunchDaemons/local.phxboot.plist /var/db/phxboot.pending
nvram -d phxfb-boot 2>/dev/null

echo "== Metal plugin"
for n in NVMTLDriver.bundle nvmtl; do [ -L "$L/$n" ] && rm -f "$L/$n"; done
rm -f "$L/nvmtl-allow.txt"
rm -rf "$H"

echo "== PhxAccel.kext"
rm -rf /Library/Extensions/PhxAccel.kext
# Rebuild from the remaining kexts. Never delete a shared collection on a build
# failure: it may also contain unrelated kexts needed by the fallback.
if ! { [ -x "$G/kinst.sh" ] && BK=/var/tmp "$G/kinst.sh"; }; then
    echo "  rebuild failed: existing Auxiliary collection retained; bring-up disabled"
    echo "  keep $G/kinst.sh and inspect /var/tmp/phx-kmutil.log before retrying"
    exit 1
fi

rm -rf "$G"
echo ""
echo "Removed. Reboot using PHXMAC or PHXMACSAFE. Only PHXNULLARC contains PhxBringup and the NullArc test boot-args."
