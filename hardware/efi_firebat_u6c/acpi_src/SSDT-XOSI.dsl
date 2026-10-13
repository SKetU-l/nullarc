/*
 * SSDT-XOSI for Firebat U6C
 * Spoofs Windows 11 / Windows 2021 environment to ACPI methods
 * Pairs with OpenCore ACPI patch: _OSI to XOSI rename.
 */
DefinitionBlock ("", "SSDT", 2, "DRIP", "XOSI", 0x00001000)
{
    Method (XOSI, 1, NotSerialized)
    {
        Name (WINV, Package ()
        {
            "Windows", 
            "Windows 2001", 
            "Windows 2001 SP1", 
            "Windows 2001 SP2", 
            "Windows 2006", 
            "Windows 2006 SP1", 
            "Windows 2009", 
            "Windows 2012", 
            "Windows 2013", 
            "Windows 2015", 
            "Windows 2016", 
            "Windows 2017", 
            "Windows 2018", 
            "Windows 2019", 
            "Windows 2020", 
            "Windows 2021", 
            "Windows 2022"
        })

        If (_OSI ("Darwin"))
        {
            Return ((Match (WINV, MEQ, Arg0, MTR, Zero, Zero) != Ones))
        }
        Else
        {
            Return (_OSI (Arg0))
        }
    }
}
