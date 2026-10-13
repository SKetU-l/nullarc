/*
 * SSDT-EC-USBX-AMD for Firebat U6C
 * Provides fake Embedded Controller (EC) device for AppleACPIEC
 * and USB Power parameters for AppleBusPowerControllerUSB.
 */
DefinitionBlock ("", "SSDT", 2, "DRIP", "ECUSBX", 0x00001000)
{
    Scope (\_SB)
    {
        Device (EC)
        {
            Name (_HID, "ACPI0004")
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Device (USBX)
        {
            Name (_ADR, Zero)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }

            Method (_DSM, 4, NotSerialized)
            {
                If ((Arg2 == Zero))
                {
                    Return (Buffer (One) { 0x03 })
                }

                Return (Package (0x08)
                {
                    "kUSBSleepPowerSupply", 
                    0x1388, 
                    "kUSBSleepPortCurrentLimit", 
                    0x0834, 
                    "kUSBWakePowerSupply", 
                    0x1388, 
                    "kUSBWakePortCurrentLimit", 
                    0x0834
                })
            }
        }
    }
}