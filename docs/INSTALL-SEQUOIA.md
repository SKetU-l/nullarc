# NullArc Sequoia 24H32 test installation

This experimental staging is only for macOS 15.8.1 build `24H32` on the HP EliteBook
845 G10 (`1002:15bf`). Keep the `PhxGPU + PhxFB (fallback)` and `Safe boot`
entries intact.

## Prepared boot paths

- **NullArc (24H32 test)** loads `PhxBringup.kext` from its own
  `/EFI/PHXNULLARC` OpenCore tree.
- **PhxGPU + PhxFB (fallback)** remains `/EFI/PHXMAC` and does not load
  `PhxBringup`.
- **Safe boot** remains `/EFI/PHXMACSAFE` and loads neither GPU driver.

`PHXNULLARC` is cloned from the known-working safe configuration. Its USB
ACPI, `PhxXhciKick`, `USBToolBox`, and `UTBDefault` files and configuration
records are unchanged so Android USB tethering follows the working path. This
preserves its configuration; tethering still needs a runtime check.

The initial test entry has `nullarc_test=1` and `-phxfboff`: the former keeps
the shared NullArc LaunchDaemon/framebuffer inactive on the fallbacks; the
latter prevents panel takeover even if an old `phxfb-boot` variable is present.

## Reproduce the stage from Linux

Build and validate before publishing a boot entry:

```sh
sh tools/build_sequoia_kexts.sh
sh tools/build_sequoia_tools.sh
python3 tools/stage_sequoia_test.py "$KDK_ROOT" "$BOOT_SNAPSHOT" "$RELEASE_ZIP" "$STAGE"
pkexec /usr/bin/python3 tools/install_sequoia_boot.py "$STAGE" --copy-package
```

`KDK_ROOT` must be the exact `24H32` extraction. `BOOT_SNAPSHOT` contains
`PHXMAC`, `PHXMACSAFE` and `limine.conf`. `RELEASE_ZIP` is the pinned upstream
`nullarc-0.3.0-alpha.zip`. `STAGE` must be a new directory. The stage uses the
verified upstream plugin/firmware only; its kexts, clients and scripts are local
Sequoia builds. It does not package the personal OpenCore configuration.

The publisher refuses stale snapshots, changed fallback files and an existing
unreviewed `PHXNULLARC` tree. It backs up Limine, publishes the complete new
tree first, then atomically adds only the menu entry. `boot-manifest.json`
records hashes of the staged tree and both unchanged fallbacks.
`--copy-package` also places the verified ZIP, checksum and this guide in a new
`/boot/NullArc-24H32` folder. It refuses to overwrite an existing folder.

## First macOS boot

1. Select **macOS → NullArc (24H32 test)** in Limine.
2. Confirm the desktop still uses the firmware framebuffer and Android USB
   tethering works.
3. In Terminal, verify `ioreg -l | grep phx-build` prints a result.

If any check fails, reboot through the PhxGPU fallback or Safe boot. Do not run
the installer yet.

## Install apps-only acceleration

Copy `nullarc-sequoia-24H32-test.zip` and `SHA256SUMS` to the HHD volume. Verify
the ZIP, then unzip it on the macOS Data volume (not directly on exFAT, which
cannot preserve Vulkan loader symlinks):

If the HHD is unavailable, mount the internal EFI partition from macOS using
`diskutil` and use its `NullArc-24H32` folder instead. Locate the internal EFI
partition with `diskutil list`; do not assume its disk number. The Linux
publisher's `/boot/NullArc-24H32` is that folder on the EFI volume.

```sh
cd /Volumes/HHD/NullArc-24H32
shasum -a 256 -c SHA256SUMS
unzip nullarc-sequoia-24H32-test.zip -d "$HOME/Downloads"
cd "$HOME/Downloads/nullarc-sequoia-24H32-test"
```

Then run:

```sh
sudo ./install.sh --check
sudo ./install.sh
```

Reboot through **NullArc (24H32 test)**. Wait for `/var/log/phxboot.log` to
contain `done` for the current boot. Run the included test:

```sh
sudo /Users/Shared/gpu/test-metal.sh
```

It starts a four-minute reboot watchdog and runs `mtlprobe`. It must print
`PASS` after shader compilation, an offscreen triangle and pixel readback;
success cancels the watchdog, failure/hang leaves it armed. Output is saved in
`/var/log/nullarc-metal-test.log`. This smoke test is not full app, sandbox, sleep/wake or display
qualification. The allow-list explicitly denies WindowServer and otherwise
allows only the plugin's built-in tests and `mtlprobe`. For another explicit
test app, add its executable name to `/Library/GPUBundles/nvmtl-allow.txt` after
each boot; do not add `*` or `!WindowServer` during this phase.

The plugin lives in `/Library/Application Support/NullArc/PhxGPU` on the Data
volume, linked from `/Library/GPUBundles`; the installer does not change the
sealed system volume. Sandbox access to this layout is still untested.
Existing system-side NullArc files are copied to
`/Users/Shared/nullarc-backup/<timestamp>` before replacement, and the previous
AuxKC is backed up separately by `kinst.sh`.

## Enable WindowServer only after apps pass

Only after explicit Metal apps and stability checks pass, remove `-phxfboff`
from **PHXNULLARC's boot-args only** and reboot through that entry. Do not
change either fallback configuration. Then:

```sh
sudo ./install.sh --windowserver
```

Follow the two-boot procedure printed by the installer. If the GPU path fails,
the watchdog leaves a recovery marker and the next boot disables bring-up. The
PhxGPU and Safe boot entries remain available independently.

To remove the staged system-side NullArc files, run `sudo ./uninstall.sh`.
This does not modify any OpenCore tree or an older plugin stored on the sealed
system volume. If rebuilding AuxKC fails, the existing shared collection is
retained and bring-up stays disabled; investigate the log before retrying.
