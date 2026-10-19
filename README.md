# AMD RDNA 3 (Radeon 780M / GFX1103) macOS Driver & Bringup Project

[![macOS](https://img.shields.io/badge/macOS-Sonoma%2014.8.x-success?logo=apple&logoColor=white)](https://www.apple.com/macos/sonoma/)
[![Architecture](https://img.shields.io/badge/Architecture-RDNA%203%20%2F%20GFX1103-red?logo=amd&logoColor=white)](https://www.amd.com/)
[![Kernel](https://img.shields.io/badge/Kernel-IOKit%20DriverKit-black?logo=apple&logoColor=white)](https://developer.apple.com/)

Pioneering open-source driver stack and low-level hardware bringup for **AMD RDNA 3 integrated graphics (Radeon 780M / Phoenix & Hawk Point APUs, GFX1103 / GC 11.0.1)** on **macOS**.

---

## 🚀 Overview & Breakthroughs

Apple officially dropped AMD driver development after RDNA 2 (Navi 21/23/24). RDNA 3 (Phoenix, Hawk Point, Strix Point) has **never** been supported by macOS, rendering modern AMD laptops completely devoid of hardware acceleration.

This project is the first to achieve **real, hardware-level code execution on AMD RDNA 3 silicon under macOS Sonoma**:

* **DCN 3.1.4 Native Display Scanout**: Pixel-perfect hardware display scanout at native **2560x1600 @ 120Hz** on the built-in laptop eDP panel without white-screen or framebuffer corruption.
* **PSP & RLC Initialization**: Successful validation and loading of AMD Platform Security Processor (PSP) firmware and RLC autoloading in macOS.
* **SMU GfxImu Bringup**: Initialized Graphics Internal Microcontroller via SMU message passing (`EnableGfxImu`).
* **MES v11 (Micro Engine Scheduler) Running**: Spun up AMD MES v11 scheduler and Kernel Interface Queue (KIQ) pipe.
* **Silicon-Level PM4 Execution**: Successfully mapped compute queues via MES (`ADD_QUEUE map_legacy_kq`) and executed PM4 command streams (`SET_UCONFIG_REG`, `WRITE_DATA`) directly on the physical Radeon 780M compute units.

---

## 🏗️ Architecture & Repository Structure

```text
rdna3-driver/
├── src/
│   ├── kext/                  # RDNA3Driver kernel extension (IOKit / C++)
│   │   ├── RDNA3Driver.cpp    # PCI probe, BAR mapping, power management
│   │   ├── RDNA3Framebuffer.cpp# IOFramebuffer & ATIFramebuffer handshake
│   │   ├── RDNA3UserClient.cpp # UserClient interface for diagnostic ring access
│   │   └── DCN314Scanout.cpp  # Display Core Next 3.1.4 timing & surface scanout
│   ├── phxbringup/            # Low-level Phoenix bringup kernel module
│   └── phxprobe/              # Hardware capability probing
├── tools/
│   ├── phxctl/                # Low-level control & PM4 dispatch tools
│   │   ├── mesboot.c          # MES scheduling & GART / GCVM initialization
│   │   ├── phxctl.c           # MMIO BAR register inspection & doorbells
│   │   ├── mes_v11_api_def.h  # MES v11 firmware packet structure definitions
│   │   └── bringup*.sh        # Automated bringup sequencing scripts
│   ├── vkprobe/               # Vulkan compute shader execution verification
│   │   ├── vkprobe.c          # Vulkan instance & headless dispatch tester
│   │   └── fill.comp          # SPIR-V compute shader test payload
│   └── ue200/                 # Hardware Ethernet watchdog & PHY reset
├── mesa-patches-phoenix/      # Mesa RADV (Radeon Vulkan) GFX1103 adaptation
└── hardware/                  # Physical hardware dumps, ftrace registers, ACPI
    └── dumps/firebat_u6c/
        └── linux_amdgpu_trace/# Full ftrace register capture of working Linux amdgpu
```

---

## ⚡ Current Milestone: GCVM / GART Translation

Current active engineering focus is **GPU Virtual Memory (GCVM Context 0 / GART)** translation. 

While direct physical system aperture (`0x80_...`) operations execute flawlessly, virtual address translation through GART depth-0 page tables requires precise register configuration discovered by diffing Linux kernel traces:
1. **Invalidation Range Programming**: `regGCVM_INVALIDATE_ENG0..17_ADDR_RANGE` must be configured with full address ranges (`0xffffffff` / `0x1f`) to prevent TLB invalidation from being silently constrained to page 0.
2. **Protection Fault Unmasking**: `GCVM_L2_PROTECTION_FAULT_CNTL` must clear `ENABLE_DEFAULT` bits (bit 1 `ALLOW_SUBSEQUENT_STATUS_UPDATES`) to prevent faults from being silently redirected to dummy pages.
3. **VMID 0 Aperture Binding**: `regSH_MEM_CONFIG` (`0x0000c00c`) configuration for 64-bit flat shader addressing.

---

## 🗺️ Roadmap to Full Metal 3D Acceleration

```text
[Phase 1: Kernel & Microcode] -> COMPLETED (PM4 execution on silicon verified)
      │
[Phase 2: GPU Virtual Memory] -> CURRENT MILESTONE (GCVM / GART bringup)
      │
[Phase 3: Mesa RADV (Vulkan)] -> Userland shader compiler & IOSurface export
      │
[Phase 4: Metal / WindowServer Bridge] -> IOAccelerator interface for macOS Desktop
```

---

## 👥 Authors & Collaborators
* **Lead Maintainer & Systems / Hardware Testing:** [@luuumi-dev](https://github.com/luuumi-dev)
* **Kernel & Low-Level AI Systems Engineering:** Anthropic Claude & Google DeepMind Gemini (Antigravity)
* **Hardware Architecture & Reference:** AMD Linux Kernel (`amdgpu` driver team) & Mesa 3D Project (RADV team)
* **macOS Kernel & OpenCore Community:** [Acidanthera](https://github.com/acidanthera) & AMD-OSX
