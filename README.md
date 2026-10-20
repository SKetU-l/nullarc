# AMD RDNA 3 (Radeon 780M / GFX1103) macOS Driver & Bringup Project

[![macOS](https://img.shields.io/badge/macOS-Tahoe%2026.6.2%20%2F%20Sonoma-success?logo=apple&logoColor=white)](https://www.apple.com/macos/)
[![Architecture](https://img.shields.io/badge/Architecture-RDNA%203%20%2F%20GFX1103-red?logo=amd&logoColor=white)](https://www.amd.com/)
[![Metal](https://img.shields.io/badge/Metal-3D%20%26%20Compute%20Active-orange?logo=apple&logoColor=white)](https://developer.apple.com/metal/)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.4.363%20(RADV)-red?logo=vulkan&logoColor=white)](https://mesa3d.org/)
[![Status](https://img.shields.io/badge/Milestone-M2%20Active%20(WindowServer%20Metal)-brightgreen)](https://github.com/luuumi-dev/amd-rdna3-macos-driver)

World's first open-source graphics driver stack and low-level hardware bringup providing **Metal 3D Acceleration, Metal Compute, and WindowServer Desktop Compositing** for **AMD RDNA 3 integrated graphics (Radeon 780M / Phoenix & Hawk Point APUs, GFX1103 / GC 11.0.1)** on **macOS Tahoe (Darwin 25.x)** and macOS Sonoma.

---

## ⚡ Quick Start & Installation Guide (Early Testers)

> [!WARNING]
> This driver is bleeding-edge and intended for testers, developers, and Hackintosh enthusiasts comfortable with OpenCore and recovery environments.

### 1. Hardware Requirements
* **APU:** AMD Ryzen 7 7840HS, 7840U, 8845HS, 8840U, or compatible Phoenix/Hawk Point APU.
* **iGPU:** AMD Radeon 780M (`1002:15bf`, GC 11.0.1 / GFX1103).
* **Display:** Internal eDP panel (2560x1600 or 1920x1080).
* **OS:** macOS Tahoe (26.x / Darwin 25.x) or macOS Sonoma (14.x).

---

### 2. Driver Components (Why One Kext Isn't Enough)
Unlike legacy GPUs supported by Apple's built-in `AMDRadeonX6000.kext`, Apple never wrote RDNA 3 graphics drivers for macOS. Full Metal 3D desktop acceleration therefore requires a **coordinated 3-part stack**:

1. **`PhxBringup.kext` (OpenCore EFI)**: Initializes RDNA 3 silicon, loads PSP 13.0.4 & RLC microcode, sets up MES v11 queues, and allocates per-process GPUVM page tables.
2. **`PhxAccel.kext` (Auxiliary Kernel Collection)**: Implements `PhxAMDFB` (framebuffer), `PhxAGDC` (display control), and `NVDisplayPipe` (hardware surface flip engine).
3. **`NVMTLDriver.bundle` (`/Library/GPUBundles/`)**: Userland Metal translator backed by Mesa RADV (Vulkan 1.4 with ACO shader compiler).

---

### 3. OpenCore Boot Configuration
Add the required boot arguments to your OpenCore `config.plist` under `NVRAM -> Add -> 7C436110-AB2A-4BBB-A880-FE41995C9F82 -> boot-args`:

```text
keepsyms=1 revpatch=cpuname,sbvmm ipc_control_port_options=0 -amfipassbeta amfi_get_out_of_my_way=0x1 amfi=0x80 phxdebug=1
```

* `amfi=0x80 amfi_get_out_of_my_way=0x1`: Bypasses Apple Mobile File Integrity library validation so `WindowServer` can load the third-party GPU bundle.
* `phxdebug=1`: Enables diagnostic sysctl endpoints and root bringup tools.

---

### 4. Deploying the Driver Stack
1. **EFI Kernel Extension:**
   Copy `PhxBringup.kext` into `EFI/OC/Kexts/` and enable it in OpenCore `config.plist`.
2. **System Kernel Extension (Aux KC):**
   Copy `PhxAccel.kext` into `/Library/Extensions/` and rebuild the auxiliary kernel collection:
   ```bash
   sudo kmutil install --volume-root / --repository /Library/Extensions
   ```
3. **Metal Bundle & Runtime:**
   Copy the patched Metal bundle into `/Library/GPUBundles/`:
   ```bash
   sudo cp -R NVMTLDriver.bundle /Library/GPUBundles/
   sudo chown -R root:wheel /Library/GPUBundles/NVMTLDriver.bundle
   ```

---

### 5. Verifying Acceleration
After rebooting, verify that hardware acceleration is active:

```bash
# 1. Test Metal Compute Pipeline (4096 parallel workgroups on 780M)
./tools/mtlprobe/mtlprobe compute

# Expected:
# [MTLPROBE] Device: AMD Radeon 780M Graphics (RADV PHOENIX)
# [MTLPROBE] 4096/4096 correct.

# 2. Test Metal 3D Geometry Rasterization
./tools/mtlprobe/mtlprobe render

# Expected:
# [MTLPROBE] 3D pipeline compiled and executed on 780M.
# [MTLPROBE] pixels: red 32640 blue 32896 other 0

# 3. Check Live WindowServer Frame Counter & Flip Latency
sysctl debug.nvaccel_iop_ok debug.nvaccel_iop_fail debug.nvaccel_iop_copy_us

# Expected:
# debug.nvaccel_iop_ok: > 1000  (frames rendered by 780M)
# debug.nvaccel_iop_fail: 0
# debug.nvaccel_iop_copy_us: ~1500 (1.5 ms per frame)
```

> **Note for Early Testers:** On initial boots, the macOS login screen (`loginwindow`) may render dark until password entry. Configuring automatic user login in macOS System Settings is recommended so the system boots straight into the accelerated desktop.

---

## 🌟 Breakthrough Milestones (October 2026)

| Milestone | Status | Details |
| :--- | :---: | :--- |
| **Silicon & Microcode (E1–E5)** | ✅ **DONE** | PSP 13.0.4 firmware autoload, RLC, SMU GfxImu, MES v11 (Micro Engine Scheduler), SDMA DMA transfers. |
| **GPU Virtual Memory (E7)** | ✅ **DONE** | 4-level GPUVM page tables in system RAM with snooped coherent PTEs, multi-client VMID (8..15) isolation. |
| **Vulkan 1.4 (E8 / RADV)** | ✅ **DONE** | Native Mesa RADV running under Darwin with ACO compiler; verified `vkprobe` compute shader dispatch. |
| **Metal 3D & Compute (M1)** | ✅ **DONE** | Hardware Metal compute (`mtlprobe` 4096/4096) and 3D rasterization on real silicon (`AMD Radeon 780M Graphics`). |
| **WindowServer Compositing (M2)** | ✅ **ACTIVE** | `WindowServer` compositing entire macOS desktop via `CompositorMetal` on 780M. 0% CPU compositor overhead, up to 41+ FPS. |
| **Display Pipe & AGDC** | ✅ **DONE** | `PhxAMDFB` native framebuffer, `PhxAGDC` device control, high-speed streaming frame copy (~1.5 ms/frame). |
| **Identity & Profiler** | ✅ **DONE** | Build 28 kext publishes `AMD Radeon 780M` and dedicated VRAM directly to IORegistry and *About This Mac*. |

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
│  /Library/GPUBundles/NVMTLDriver.bundle (Patched Metal Translation)    │
│  ├── libnvmtl_translate.dylib                                          │
│  └── /Library/GPUBundles/nvmtl/libvulkan_radeon.dylib (Mesa RADV 1.4)  │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ IOKit UserClient ABI
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│  macOS Kernel Extensions (IOKit / Mach-O)                              │
│  ├── PhxBringup.kext (Build 28 in EFI)                                 │
│  │   ├── PSP 13.0.4 Ring & TMR Microcode Loader                         │
│  │   ├── MES v11 Sched / KIQ Management & Ring Submission               │
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

## 👥 Authors & Credits

* **Lead Maintainer & Systems Testing:** [@luuumi-dev](https://github.com/luuumi-dev)
* **Kernel & Low-Level AI Systems Engineering:** Anthropic Claude (Opus 5.5) & Google DeepMind Gemini (Antigravity)
* **Pioneering macOS GPU Research & Architecture:** [Cobalt64](https://cobalt64.com) (RDNA4 / Tahoe Metal) & **NullMoth** (IOAccelerator architecture)
* **Hardware Reference & Open Source:** AMD Linux `amdgpu` kernel team & Mesa 3D Project (RADV ACO team)
* **OpenCore & Hackintosh Tooling:** [Acidanthera](https://github.com/acidanthera) & AMD-OSX Community

---

## 📄 License

This repository is licensed under the MIT License. Portions derived from NullMoth are subject to the PolyForm Noncommercial License 1.0.0. Mesa RADV components are licensed under the MIT/X11 License.
