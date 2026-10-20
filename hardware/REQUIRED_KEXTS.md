# Required & Recommended Kexts for AMD Phoenix/Hawk Point (RDNA 3)

This document lists the required and recommended third-party OpenCore kernel extensions (kexts) to boot macOS Sonoma (14.x) or macOS Tahoe (15.x / Darwin 25.x) on AMD Ryzen 7040/8040 series laptops (e.g. Firebat U6C, Framework 13 AMD, etc.), paired with the RDNA 3 graphics driver.

> [!IMPORTANT]
> To keep this repository focused and lightweight, third-party precompiled binaries are not bundled. Download official release builds directly from the upstream repositories listed below.

---

## 1. Core System Kexts (Mandatory)

| Kext | Purpose | Official Release Repository |
| :--- | :--- | :--- |
| **Lilu.kext** | Essential kernel patching engine required by all other kexts. | [acidanthera/Lilu](https://github.com/acidanthera/Lilu/releases) |
| **VirtualSMC.kext** | Emulates Apple SMC chip (includes `SMCProcessor`, `SMCBatteryManager`). | [acidanthera/VirtualSMC](https://github.com/acidanthera/VirtualSMC/releases) |
| **RestrictEvents.kext** | Blocks unwanted system events, enables CPU name spoofing (`revpatch=cpuname,sbvmm`). | [acidanthera/RestrictEvents](https://github.com/acidanthera/RestrictEvents/releases) |
| **AMFIPass.kext** | Allows passing AMFI validation on modern macOS versions without fully disabling AMFI. | [acidanthera/AMFIPass](https://github.com/acidanthera/AMFIPass/releases) |

---

## 2. Graphics Driver (This Project)

| Kext / Bundle | Purpose | Source / Download |
| :--- | :--- | :--- |
| **PhxBringup.kext** | OpenCore EFI kext (in `EFI/OC/Kexts/`). Initializes AMD Phoenix/Hawk Point silicon, PSP 13.0.4 firmware, MES v11 scheduler, and GPUVM 4-level page tables. | [Releases](https://github.com/luuumi-dev/amd-rdna3-macos-driver/releases) or build via `make` |
| **PhxAccel.kext** | Auxiliary Kernel Collection (`/Library/Extensions/`). Provides `PhxAMDFB` native framebuffer and `PhxAGDC` display control. | [Releases](https://github.com/luuumi-dev/amd-rdna3-macos-driver/releases) |
| **NVMTLDriver.bundle** | Metal-over-Vulkan translation runtime + Mesa RADV (`/Library/GPUBundles/`). | [Releases](https://github.com/luuumi-dev/amd-rdna3-macos-driver/releases) |

---

## 3. Audio, Input & Peripherals

| Kext | Purpose | Official Release Repository |
| :--- | :--- | :--- |
| **AppleALC.kext** | High-definition audio patching (layout-id configured in `boot-args` / `DeviceProperties`). | [acidanthera/AppleALC](https://github.com/acidanthera/AppleALC/releases) |
| **VoodooI2C.kext** | I2C bus driver for modern laptop trackpads and touchscreens. | [VoodooI2C/VoodooI2C](https://github.com/VoodooI2C/VoodooI2C/releases) |
| **VoodooI2CHID.kext** | Satellite driver for I2C HID precision trackpads (multitouch gestures). | [VoodooI2C/VoodooI2C](https://github.com/VoodooI2C/VoodooI2C/releases) |
| **BrightnessKeys.kext** | Automatic handling of ACPI brightness keys (F-keys). | [acidanthera/BrightnessKeys](https://github.com/acidanthera/BrightnessKeys/releases) |
| **USBPorts.kext** | Custom codeless USB port map tailored for your motherboard controller. | Map using [USBToolBox](https://github.com/USBToolBox/tool) or CorpNewt's USBMap |

---

## 4. Networking (Hardware-Dependent)

| Kext | Target Hardware | Official Release Repository |
| :--- | :--- | :--- |
| **AirportItlwm.kext** / **itlwm.kext** | Intel Wi-Fi cards (e.g. AX200 / AX210). | [OpenIntelWireless/itlwm](https://github.com/OpenIntelWireless/itlwm/releases) |
| **IntelBluetoothFirmware.kext** | Intel Bluetooth adapters. | [OpenIntelWireless/IntelBluetoothFirmware](https://github.com/OpenIntelWireless/IntelBluetoothFirmware/releases) |
| **LucyRTL8125Ethernet.kext** | Realtek 2.5GbE Ethernet controllers. | [Mieze/LucyRTL8125Ethernet](https://github.com/Mieze/LucyRTL8125Ethernet/releases) |

---

## 5. Reference OpenCore Configuration

A clean reference configuration with dummy SMBIOS values is provided at:
👉 [`Sample.plist`](Sample.plist)

> [!WARNING]
> Before booting, generate your own unique SMBIOS identifiers (`SystemSerialNumber`, `MLB`, `SystemUUID`) using [GenSMBIOS](https://github.com/corpnewt/GenSMBIOS) with model `MacBookPro16,1`. Never use public or shared serial numbers.
