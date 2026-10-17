DefinitionBlock ("", "SSDT", 2, "ZPSS", "UsbReset", 0x00001000)
{
    External (_SB_.PCI0.GP17.XHC1.RHUB, DeviceObj)
    External (_SB_.PCI0.GP17.XHC0.RHUB.HUB1, DeviceObj)
    External (GUPC, MethodObj)
    External (GPLD, MethodObj)

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
        Method (_UPC, 0, NotSerialized)
        {
            Return (GUPC (One, 0xFF))
        }

        Method (_PLD, 0, NotSerialized)
        {
            Return (GPLD (Zero, 0x04, Zero))
        }
    }
}
