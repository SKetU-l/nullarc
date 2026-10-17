/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20251212 (64-bit version)
 * Copyright (c) 2000 - 2025 Intel Corporation
 * 
 * Disassembling to symbolic ASL+ operators
 *
 * Disassembly of /home/luuumi-dev/.gemini/antigravity/scratch/rdna3-driver/hardware/efi_firebat_u6c_simplify/EFI/OC/ACPI/SSDT-USB-Reset.aml
 *
 * Original Table Header:
 *     Signature        "SSDT"
 *     Length           0x00000164 (356)
 *     Revision         0x02
 *     Checksum         0x72
 *     OEM ID           "ZPSS"
 *     OEM Table ID     "UsbReset"
 *     OEM Revision     0x00001000 (4096)
 *     Compiler ID      "INTL"
 *     Compiler Version 0x20240927 (539232551)
 */
DefinitionBlock ("", "SSDT", 2, "ZPSS", "UsbReset", 0x00001000)
{
    External (_SB_.PCI0.GP17.XHC0.RHUB, DeviceObj)
    External (_SB_.PCI0.GP17.XHC1.RHUB, DeviceObj)
    External (_SB_.PCI0.GP19.XHC3.RHUB, DeviceObj)
    External (_SB_.PCI0.GP19.XHC4.RHUB, DeviceObj)

    Scope (\_SB.PCI0.GP17.XHC0.RHUB)
    {
        Method (_STA, 0, NotSerialized)  // _STA: Status
        {
            If (_OSI ("Darwin"))
            {
                Return (Zero)
            }
            Else
            {
                Return (0x0F)
            }
        }
    }

    Scope (\_SB.PCI0.GP17.XHC1.RHUB)
    {
        Method (_STA, 0, NotSerialized)  // _STA: Status
        {
            If (_OSI ("Darwin"))
            {
                Return (Zero)
            }
            Else
            {
                Return (0x0F)
            }
        }
    }

    Scope (\_SB.PCI0.GP19.XHC3.RHUB)
    {
        Method (_STA, 0, NotSerialized)  // _STA: Status
        {
            If (_OSI ("Darwin"))
            {
                Return (Zero)
            }
            Else
            {
                Return (0x0F)
            }
        }
    }

    Scope (\_SB.PCI0.GP19.XHC4.RHUB)
    {
        Method (_STA, 0, NotSerialized)  // _STA: Status
        {
            If (_OSI ("Darwin"))
            {
                Return (Zero)
            }
            Else
            {
                Return (0x0F)
            }
        }
    }
}

