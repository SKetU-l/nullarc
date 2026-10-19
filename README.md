# AMD RDNA 3 (Radeon 780M / GFX1103) macOS Driver & Bringup Project

[![macOS](https://img.shields.io/badge/macOS-Tahoe%20(Darwin%2025.x)-success?logo=apple&logoColor=white)](https://www.apple.com/macos/)
[![Architecture](https://img.shields.io/badge/Architecture-RDNA%203%20%2F%20GFX1103-red?logo=amd&logoColor=white)](https://www.amd.com/)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.4.363%20(RADV)-red?logo=vulkan&logoColor=white)](https://mesa3d.org/)
[![Kernel](https://img.shields.io/badge/Kernel-IOKit%20DriverKit-black?logo=apple&logoColor=white)](https://developer.apple.com/)

Pioneering open-source driver stack and low-level hardware bringup for **AMD RDNA 3 integrated graphics (Radeon 780M / Phoenix & Hawk Point APUs, GFX1103 / GC 11.0.1)** on **macOS Tahoe (Darwin 25.x)**.

---

## 🚀 Overview & Breakthroughs

Apple officially dropped AMD driver development after RDNA 2 (Navi 21/23/24). RDNA 3 (Phoenix, Hawk Point, Strix Point) has **never** been supported by macOS, rendering modern AMD laptops completely devoid of hardware acceleration.

This project is the first in history to achieve **real, hardware-level code execution and userland Vulkan API support on AMD RDNA 3 silicon under macOS Tahoe**:

* **DCN 3.1.4 Native Display Scanout**: Pixel-perfect hardware display scanout at native **2560x1600 @ 120Hz** on the built-in laptop eDP panel without white-screen or framebuffer corruption.
* **PSP & RLC Initialization**: Successful validation and loading of AMD Platform Security Processor (PSP) firmware and RLC autoloading in macOS.
* **SMU GfxImu Bringup**: Initialized Graphics Internal Microcontroller via SMU message passing (`EnableGfxImu`).
* **MES v11 (Micro Engine Scheduler) Running**: Spun up AMD MES v11 scheduler and Kernel Interface Queue (KIQ) pipe.
* **SDMA Engine DMA**: Verified high-speed DMA transfers directly into system host memory and VRAM via MMHUB AGP.
* **Hardware GART Memory Translation**: Fully working GPUVM Context 0 (GART) page tables with coherent `MTYPE=4` cache routing.
* **User-Space Virtual Memory (VMID 8)**: 4-level page table walking for user processes, executing PM4 `INDIRECT_BUFFER` packets with `IB_VALID (bit 23)`.
* **Mesa RADV Vulkan (Vulkan 1.4.363) Running**: Device enumeration (`[0] AMD Radeon 780M Graphics (RADV PHOENIX)`), logical device creation, and compute pipeline compilation via Mesa ACO under macOS Darwin!

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
│   ├── phxbringup/            # Low-level Phoenix bringup kernel module (Build 18)
│   └── phxprobe/              # Hardware capability probing
├── tools/
│   ├── phxctl/                # Low-level control & PM4 dispatch tools
│   │   ├── mesboot.c          # MES scheduling, GART & VMID 8 multi-level paging
│   │   ├── phxctl.c           # MMIO BAR register inspection & doorbells
│   │   ├── mes_v11_api_def.h  # MES v11 firmware packet structure definitions
│   │   └── bringup*.sh        # Automated bringup sequencing scripts
│   ├── vkprobe/               # Vulkan compute shader execution verification
│   │   ├── vkprobe.c          # Vulkan instance, device & headless dispatch tester
│   │   └── fill.comp          # SPIR-V compute shader test payload
│   └── ue200/                 # Hardware Ethernet watchdog & PHY reset
├── mesa-patches-phoenix/      # Mesa RADV (Radeon Vulkan) GFX1103 adaptation
└── hardware/                  # Physical hardware dumps, ftrace registers, ACPI
    └── dumps/firebat_u6c/
        └── linux_amdgpu_trace/# Full ftrace register capture of working Linux amdgpu
```

---

## 🗺️ Roadmap to Full Metal 3D Acceleration

```text
[Phase 1: Silicon & Microcode] ──► COMPLETED (PSP, MES v11, SDMA, DCN 3.1.4 120Hz)
      │
[Phase 2: GPU Virtual Memory]  ──► COMPLETED (GART, 4-level VMID 8, Indirect Buffers)
      │
[Phase 3: Mesa RADV (Vulkan)]  ──► ACTIVE (Vulkan 1.4 initialized, device created, ACO compiled)
      │
[Phase 4: Metal / WindowServer] ──► NEXT (metal2vulkan bridge & IOAccelerator for macOS Desktop)
```

---

## 👥 Authors & Collaborators
* **Lead Maintainer & Systems / Hardware Testing:** [@luuumi-dev](https://github.com/luuumi-dev)
* **Kernel & Low-Level AI Systems Engineering:** Anthropic Claude & Google DeepMind Gemini (Antigravity)
* **Pioneering macOS AMD GPU Research & Architecture:** [Cobalt64](https://cobalt64.com) & **nullmoth**
* **Hardware Architecture & Reference:** AMD Linux Kernel (`amdgpu` driver team) & Mesa 3D Project (RADV team)
* **macOS Kernel & OpenCore Community:** [Acidanthera](https://github.com/acidanthera) & AMD-OSX
