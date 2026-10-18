DefinitionBlock ("", "SSDT", 2, "ZPSS", "UsbReset", 0x00001000)
{
    External (_SB_.PCI0.GP17.XHC1.RHUB, DeviceObj)
    External (_SB_.PCI0.GP17.XHC0.RHUB, DeviceObj)
    External (_SB_.PCI0.GP17.XHC0.RHUB.HUB1, DeviceObj)

    Scope (\_SB.PCI0.GP17.XHC1.RHUB)
    {
        Method (_STA, 0, NotSerialized)
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

    Scope (\_SB.PCI0.GP17.XHC0.RHUB.HUB1)
    {
        Method (_STA, 0, NotSerialized)
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

    Scope (\_SB.PCI0.GP17.XHC0.RHUB)
    {
        Device (PRT4)
        {
            Name (_ADR, 0x04)

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

            Method (_UPC, 0, NotSerialized)
            {
                Return (Package (0x04)
                {
                    One, 
                    0xFF, 
                    Zero, 
                    Zero
                })
            }

            Method (_PLD, 0, NotSerialized)
            {
                Return (Package (0x01)
                {
                    Buffer (0x10)
                    {
                        0x82, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x20, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                    }
                })
            }
        }
    }
}
