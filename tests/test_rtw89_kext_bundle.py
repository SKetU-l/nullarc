#!/usr/bin/env python3
"""
tests/test_rtw89_kext_bundle.py — Comprehensive Mach-O and KXLD Vtable Audit for Rtl89Driver.kext
"""

import os
import sys
import struct
import plistlib

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEXT_BUNDLE_PATH = os.path.join(PROJECT_ROOT, "build", "Rtl89Driver.kext")
BINARY_PATH = os.path.join(KEXT_BUNDLE_PATH, "Contents", "MacOS", "Rtl89Driver")
PLIST_PATH = os.path.join(KEXT_BUNDLE_PATH, "Contents", "Info.plist")


def test_bundle():
    print("=================================================================")
    print("  Rtl89Driver.kext Mach-O and KXLD Relocation Audit")
    print("=================================================================")

    assert os.path.exists(BINARY_PATH), f"Missing kext binary at {BINARY_PATH}"
    assert os.path.exists(PLIST_PATH), f"Missing Info.plist at {PLIST_PATH}"

    # 1. Audit Info.plist
    print("[*] Auditing Info.plist...")
    with open(PLIST_PATH, "rb") as f:
        pl = plistlib.load(f)

    assert pl.get("CFBundleIdentifier") == "org.drip.driver.Rtl89Driver"
    assert pl.get("CFBundleExecutable") == "Rtl89Driver"
    assert pl.get("OSBundleRequired") == "Network-Root"

    personalities = pl.get("IOKitPersonalities", {})
    assert "Rtl89Driver" in personalities
    p = personalities["Rtl89Driver"]
    assert p.get("IOClass") == "Rtl89Driver"
    assert p.get("IOPCIPrimaryMatch") == "0xb85210ec"
    assert p.get("IOUserClientClass") == "Rtl89UserClient"
    print("    -> Info.plist structure, CFBundleIdentifier, and PCI primary match PASSED")

    # 2. Audit Mach-O Header
    print("[*] Auditing Mach-O binary headers...")
    with open(BINARY_PATH, "rb") as f:
        data = f.read()

    magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = struct.unpack_from("<IIIIIIII", data, 0)
    assert magic == 0xFEEDFACF, f"Invalid magic: {hex(magic)}"
    assert filetype == 0x0B, f"Expected MH_KEXT_BUNDLE (0x0B), got: {hex(filetype)}"
    print(f"    -> Magic 0xFEEDFACF (64-bit), filetype 0x0B (MH_KEXT_BUNDLE), {ncmds} load commands")

    # 3. Parse Load Commands & Symbol Tables
    off = 32
    symoff = nsyms = stroff = strsize = 0
    dysym = None

    for _ in range(ncmds):
        c, csize = struct.unpack_from("<II", data, off)
        if c == 0x02:  # LC_SYMTAB
            symoff, nsyms, stroff, strsize = struct.unpack_from("<IIII", data, off + 8)
        elif c == 0x0B:  # LC_DYSYMTAB
            dysym = struct.unpack_from("<18I", data, off + 8)
        off += csize

    assert symoff > 0 and nsyms > 0, "Missing symbol table"
    assert dysym is not None, "Missing LC_DYSYMTAB"

    extreloff, nextrel, locreloff, nlocrel = dysym[14], dysym[15], dysym[16], dysym[17]
    print(f"    -> LC_DYSYMTAB: extreloff={extreloff}, nextrel={nextrel}, locreloff={locreloff}, nlocrel={nlocrel}")
    assert nextrel > 0, "No external relocations found in LC_DYSYMTAB!"
    assert nlocrel > 0, "No local relocations found in LC_DYSYMTAB!"

    # 4. Check Symbols
    print("[*] Verifying critical symbols and vtables...")
    sym_names = set()
    for i in range(nsyms):
        strx, stype, ssect, sdesc, sval = struct.unpack_from("<IBBHQ", data, symoff + i * 16)
        end = data.find(b"\x00", stroff + strx)
        name = data[stroff + strx:end].decode()
        sym_names.add(name)

    critical_syms = [
        "_kmod_info",
        "__ZTV11Rtl89Driver",
        "__ZTVN11Rtl89Driver9MetaClassE",
        "__ZTV15Rtl89UserClient",
        "__ZTVN15Rtl89UserClient9MetaClassE",
    ]

    for sym in critical_syms:
        assert sym in sym_names, f"Critical symbol '{sym}' missing from symbol table!"
        print(f"    -> Found symbol: {sym}")

    print("\n>>> ALL RTL89 MACH-O AND KXLD VTABLE TESTS PASSED! <<<")


if __name__ == "__main__":
    test_bundle()
