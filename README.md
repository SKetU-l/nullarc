# AMD RDNA 3 (Radeon 780M / GFX1103) macOS Driver & Bringup Project

[![macOS](https://img.shields.io/badge/macOS-Sonoma%2014.x%20%2F%20Tahoe%2025.x-success?logo=apple&logoColor=white)](https://www.apple.com/macos/)
[![Architecture](https://img.shields.io/badge/Architecture-RDNA%203%20%2F%20GFX1103-red?logo=amd&logoColor=white)](https://www.amd.com/)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.4.363%20(RADV)-red?logo=vulkan&logoColor=white)](https://mesa3d.org/)
[![CI](https://github.com/luuumi-dev/amd-rdna3-macos-driver/actions/workflows/ci.yml/badge.svg)](https://github.com/luuumi-dev/amd-rdna3-macos-driver/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/luuumi-dev/amd-rdna3-macos-driver?color=blue&logo=github)](https://github.com/luuumi-dev/amd-rdna3-macos-driver/releases)
[![License](https://img.shields.io/badge/License-Multi--License-lightgrey.svg)](LICENSE)

Language: **English** | **[Русский перевод (Russian README)](README_RU.md)**

Experimental open-source graphics bringup and acceleration stack for **AMD RDNA 3 integrated graphics (Radeon 780M / Phoenix & Hawk Point APUs, GFX1103 / GC 11.0.1)** on **macOS Sonoma (14.x)** and **macOS Tahoe (Darwin 25.x)**.

> [!NOTE]
> **Factual Hardware Status:**
> Metal compute tests pass (`mtlprobe compute` 4096/4096 workgroups), and `WindowServer` desktop compositing runs on the Radeon 780M on our reference **Firebat U6C** laptop (AMD Ryzen 7 7840HS / 8845HS). Other laptops, handhelds, APUs, and desktop setups are currently **untested**.

---

## 🧪 Installation & Early Tester Guide (Quickstart)

> [!WARNING]
> This driver is early-stage alpha software. Disabling system security features carries real risks. Review the [Security Risks & Limitations](#-security-risks--disabling-amfi) section before proceeding.

### 1. Hardware Scope
* **Target APUs:** AMD Ryzen 7 7840HS, 7840U, 8845HS, 8840U (Phoenix / Hawk Point).
* **Target iGPU:** AMD Radeon 780M (`1002:15bf` / `1002:1900`, GC 11.0.1 / GFX1103).
* **Reference Machine:** Firebat U6C (16-inch 2560x1600 eDP internal panel). Other hardware models are experimental.

### 2. Download Release Assets
Download the official release archives from the [GitHub Releases Page](https://github.com/luuumi-dev/amd-rdna3-macos-driver/releases):
* **[`AMD-RDNA3-Radeon-780M-macOS-Full-Package-v0.2.1-alpha.zip`](https://github.com/luuumi-dev/amd-rdna3-macos-driver/releases/download/v0.2.1-alpha/AMD-RDNA3-Radeon-780M-macOS-Full-Package-v0.2.1-alpha.zip)** *(Recommended)*: Complete package including `PhxBringup.kext` for EFI, `PhxAccel.kext` for `/Library/Extensions`, and `NVMTLDriver.bundle` for `/Library/GPUBundles`.
* **[`PhxBringup.kext-v0.2.1-alpha.zip`](https://github.com/luuumi-dev/amd-rdna3-macos-driver/releases/download/v0.2.1-alpha/PhxBringup.kext-v0.2.1-alpha.zip)**: Standalone OpenCore EFI kext for low-level silicon bringup and ring testing.

### 3. OpenCore Boot Configuration
Add the required boot arguments to your OpenCore `config.plist` under `NVRAM -> Add -> 7C436110-AB2A-4BBB-A880-FE41995C9F82 -> boot-args`:

```text
keepsyms=1 revpatch=cpuname,sbvmm ipc_control_port_options=0 -amfipassbeta amfi_get_out_of_my_way=0x1 amfi=0x80 phxdebug=1
```

A clean sample configuration is available at [`hardware/Sample.plist`](hardware/Sample.plist). For third-party kext requirements, see [`hardware/REQUIRED_KEXTS.md`](hardware/REQUIRED_KEXTS.md).

### 4. Deploy Driver Components

1. **EFI Kernel Extension (`PhxBringup.kext`):**
   Copy `PhxBringup.kext` into `EFI/OC/Kexts/` on your EFI partition and register it in `config.plist` under `Kernel -> Add`.
2. **Auxiliary Kernel Extension (`PhxAccel.kext`):**
   Copy `PhxAccel.kext` to `/Library/Extensions/` and rebuild the kernel collection:
   ```bash
   sudo cp -R PhxAccel.kext /Library/Extensions/
   sudo chown -R root:wheel /Library/Extensions/PhxAccel.kext
   sudo kmutil install --volume-root / --repository /Library/Extensions
   ```
3. **Metal Translation Bundle (`NVMTLDriver.bundle`):**
   Copy `NVMTLDriver.bundle` and `nvmtl` into `/Library/GPUBundles/`:
   ```bash
   sudo mkdir -p /Library/GPUBundles
   sudo cp -R NVMTLDriver.bundle /Library/GPUBundles/
   sudo chown -R root:wheel /Library/GPUBundles/NVMTLDriver.bundle
   ```

### 5. Verification Commands

Open Terminal to verify that acceleration and the driver endpoints are operational:

```bash
# 1. Build and run the Metal compute probe (4096 workgroups on 780M).
#    Keep the output name "mtlprobe": it is on NVMTLDriver's process allow list.
clang -fobjc-arc -framework Foundation -framework Metal \
      -o tools/mtlprobe/mtlprobe tools/mtlprobe/mtlprobe.m
./tools/mtlprobe/mtlprobe compute

# Expected Output:
# [MTLPROBE] Device: AMD Radeon 780M Graphics (RADV PHOENIX)
# [MTLPROBE] 4096/4096 correct.

# 2. Check WindowServer hardware frame counters
sysctl debug.nvaccel_iop_ok debug.nvaccel_iop_fail debug.nvaccel_iop_copy_us

# Expected:
# debug.nvaccel_iop_ok: > 1000  (frames rendered by hardware)
# debug.nvaccel_iop_fail: 0
# debug.nvaccel_iop_copy_us: ~1500 (1.5 ms streaming copy)
```

---

## ⚠️ Security Risks & Disabling AMFI

To load third-party graphics translation bundles into `WindowServer` on macOS Sonoma and Tahoe, the system must be booted with `amfi=0x80` and `amfi_get_out_of_my_way=0x1`.

### What This Disables
* **Library Validation:** Relaxing Apple Mobile File Integrity (AMFI) allows processes to load code signed by arbitrary certificates or ad-hoc signatures.
* **Broken Services:** System services relying on strict platform integrity will stop working:
  * FairPlay DRM playback (Apple TV+, Netflix 4K in Safari).
  * Certain App Store applications and developer tools enforcing Hardened Runtime validation.
* **System Updates:** Incremental macOS OTA updates may fail while AMFI and SIP are modified.

### How to Revert
If you need to restore full system security:
1. While still booted with the current `boot-args`, remove the driver components:
   ```bash
   sudo rm -rf /Library/Extensions/PhxAccel.kext /Library/GPUBundles/NVMTLDriver.bundle /Library/GPUBundles/nvmtl
   sudo kmutil install --volume-root / --update-all
   ```
   Approve the change in System Settings → Privacy & Security if prompted, then reboot.
2. Remove `PhxBringup.kext` from `EFI/OC/Kexts/` and its `Kernel -> Add` entry in `config.plist`.
3. Remove `amfi=0x80`, `amfi_get_out_of_my_way=0x1` and `-amfipassbeta` from `boot-args` and reboot.

Removing the bundle first matters: with AMFI re-enabled, `WindowServer` would otherwise refuse the ad-hoc signed bundle on its next launch.

---

## 🔍 What is `NVMTLDriver.bundle`?

Despite the `NV` prefix in the file name, **`NVMTLDriver.bundle` is NOT an NVIDIA driver**.

The prefix is a historical identifier inherited from upstream research by **NullMoth**, who originally designed a Vulkan-to-Metal translation architecture for macOS. In this driver stack, `NVMTLDriver.bundle` acts as a translation layer:
1. It intercepts macOS Metal API calls from `WindowServer` and client applications.
2. It translates them into Vulkan calls executed by the Darwin-patched **Mesa RADV** driver (`libvulkan_radeon.dylib`).
3. Mesa RADV executes commands directly on the AMD RDNA 3 hardware ring buffers via `PhxBringup.kext`.

---

## ⚠️ Known Issues & Tested Hardware

* **Tested Hardware:** Firebat U6C (AMD Ryzen 7 7840HS / 8845HS, Radeon 780M GFX1103, 2560x1600 120Hz eDP panel).
* **Untested Hardware:** Framework 13 AMD, ASUS ROG Ally, GPD Win, Lenovo ThinkPad / Yoga Phoenix laptops. Compatibility is unconfirmed.
* **Frame Delivery Rate:** WindowServer frames are copied to the active display scanout via SSE/MOVNTI streaming instructions, achieving ~41 FPS on the internal panel.
* **External Displays:** HDMI and USB-C DisplayPort Alt Mode hotplugging and audio are experimental.
* **Sleep / Wake:** Power management sleep states have not been thoroughly tested and may cause display wake failures.
* **Login Window:** On initial boot, the macOS login screen (`loginwindow`) may appear dark before session initialization. We recommend enabling **Automatic Login** in macOS Users & Groups settings.

---

## 🛠️ Building from Source & Reproducibility

### Prerequisites
* **macOS:** Xcode 15+ or Command Line Tools with Apple Clang.
* **Linux / Cross-compilation:** Clang 15+, Make, and Python 3.9+.
* **Kernel Headers:** [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) headers for kernel extension compilation.

### Build Commands

```bash
# Download Phoenix firmware fixtures from linux-firmware (needed by the ucode tests)
make fixtures

# Compile and run unit tests (Linux or macOS)
make test

# Build PhxBringup.kext on macOS
sh src/phxbringup/build.sh /path/to/MacKernelSDK

# Build PhxAccel.kext on macOS
sh src/phxaccel/build.sh /path/to/MacKernelSDK
```

Continuous integration runs unit test suites on every commit via [GitHub Actions](.github/workflows/ci.yml).

---

## 🏛️ Driver Architecture

```
┌────────────────────────────────────────────────────────────────────────┐
│                   macOS Userland Applications                          │
│     (Finder, Safari, Metal Apps, WindowServer Compositor)              │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Metal API
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│  /Library/GPUBundles/NVMTLDriver.bundle (Metal-over-Vulkan Shim)       │
│  └── /Library/GPUBundles/nvmtl/libvulkan_radeon.dylib (Mesa RADV 1.4)  │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ IOKit UserClient ABI
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│  macOS Kernel Extensions (IOKit / Mach-O)                              │
│  ├── PhxBringup.kext (Build 29 in EFI)                                 │
│  │   ├── PSP 13.0.4 Microcode Loader & Ring Management                 │
│  │   ├── MES v11 Scheduler & KIQ Ring Submission                       │
│  │   └── Per-Process GPUVM 4-Level Page Table Allocator (VMID 8..15)   │
│  └── PhxAccel.kext (Auxiliary Kernel Collection)                       │
│      ├── PhxAMDFB (IOFramebuffer takeover with AMD vendor tagging)     │
│      ├── PhxAGDC (AppleGraphicsDeviceControl display handshake)        │
│      └── NMXAccel / NVDisplayPipe (Accelerated Surface Flip Engine)    │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ MMIO BAR0 / Doorbell BAR2
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│         AMD Radeon 780M Silicon (Hawk Point / Phoenix GFX1103)         │
│          DCN 3.1.4 Scanout Panel (2560x1600 @ 120Hz eDP)               │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 📄 License & Attribution

This repository is governed by multiple licenses depending on the component. See the root [LICENSE](LICENSE) file for complete details.

* **PhxBringup.kext & Utilities (`src/phxbringup`, `tools/phxctl`):** Licensed under **GPL-2.0-only** or MIT, reflecting architectural alignment with upstream Linux `amdgpu`.
* **PhxAccel & NVMTLDriver Patches (`src/phxaccel`, `tools/nvmtl-patches`):** Derived from architecture by NullMoth, licensed under the **PolyForm Noncommercial License 1.0.0**.
  ```
  NullMoth NVIDIA driver for macOS
  Copyright (c) 2026 NullMoth Systems.
  SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
  ```
  *Commercial use is strictly prohibited under this license.*
* **Mesa Patches (`mesa-patches-phoenix`):** Licensed under the **MIT License** with original Mesa copyrights preserved.
* **Credits:** AMD Linux `amdgpu` kernel team, Mesa 3D project, [Acidanthera](https://github.com/acidanthera), [Cobalt64](https://cobalt64.com), and the AMD-OSX community.
