/*
 * SSDT-PNLF for Firebat U6C (AMD Radeon 780M / DCN 3.1.4)
 * Attaches PNLF backlight device for AppleBacklight / WhateverGreen / NootedRed
 */
DefinitionBlock ("", "SSDT", 2, "DRIP", "PNLF", 0x00001000)
{
    Scope (\_SB)
    {
        Device (PNLF)
        {
            Name (_HID, EisaId ("APP0002"))  // Apple Backlight Device
            Name (_CID, "backlight")
            Name (_UID, 0x13)  // UID 19 for AMD iGPU / WhateverGreen / NootedRed
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0B)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }
    }
}
