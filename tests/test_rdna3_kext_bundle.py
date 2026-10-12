#!/usr/bin/env python3
"""
tests/test_rdna3_kext_bundle.py — Verification of RDNA3Driver.kext bundle, Mach-O binary, and OpenCore integration.
"""

import os
import sys
import struct
import plistlib
import subprocess

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEXT_DIR = os.path.join(PROJECT_ROOT, "hardware", "efi_firebat_u6c_simplify", "EFI", "OC", "Kexts", "RDNA3Driver.kext")
EXEC_PATH = os.path.join(KEXT_DIR, "Contents", "MacOS", "RDNA3Driver")
PLIST_PATH = os.path.join(KEXT_DIR, "Contents", "Info.plist")
CONFIG_PATH = os.path.join(PROJECT_ROOT, "hardware", "efi_firebat_u6c_simplify", "EFI", "OC", "config.plist")
OCVALIDATE = "/tmp/opencore_extracted/Utilities/ocvalidate/ocvalidate.linux"

def test_kext_structure():
    print("Testing Kext bundle directory structure...")
    assert os.path.isdir(KEXT_DIR), f"Kext dir does not exist: {KEXT_DIR}"
    assert os.path.isfile(EXEC_PATH), f"Executable missing: {EXEC_PATH}"
    assert os.path.isfile(PLIST_PATH), f"Info.plist missing: {PLIST_PATH}"
    print("[PASS] Kext directory structure valid")

def test_macho_header():
    print("Testing Mach-O 64-bit binary header...")
    with open(EXEC_PATH, "rb") as f:
        header = f.read(32)
    magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = struct.unpack("<IIIIIIII", header)

    assert magic == 0xfeedfacf, f"Expected MH_MAGIC_64 (0xfeedfacf), got {hex(magic)}"
    assert cputype == 0x01000007, f"Expected CPU_TYPE_X86_64 (0x1000007), got {hex(cputype)}"
    assert filetype == 0x0B, f"Expected MH_KEXT_BUNDLE (0x0b), got {hex(filetype)}"
    assert (flags & 0x4) != 0, "Expected DYLDLINK flag set"
    assert (flags & 0x80) != 0, "Expected TWOLEVEL flag set"
    print(f"[PASS] Mach-O 64-bit kext bundle verified (filetype=0x{filetype:x}, {ncmds} load commands, flags=0x{flags:x})")

def test_info_plist():
    print("Testing Info.plist dictionary...")
    with open(PLIST_PATH, "rb") as f:
        pl = plistlib.load(f)

    assert pl.get("CFBundleIdentifier") == "org.drip.driver.RDNA3Driver"
    assert pl.get("CFBundleExecutable") == "RDNA3Driver"
    assert pl.get("OSBundleRequired") in ("Root", "Local-Root")
    with open(EXEC_PATH, "rb") as f:
        assert b"kmod_info" in f.read(), "_kmod_info symbol missing in kext executable"

    personalities = pl.get("IOKitPersonalities", {})
    assert "RDNA3Driver" in personalities, "Missing RDNA3Driver personality"
    p = personalities["RDNA3Driver"]
    assert p.get("IOClass") == "RDNA3Driver"
    assert "0x19001002" in p.get("IOPCIPrimaryMatch", "")
    assert p.get("IOProviderClass") == "IOPCIDevice"
    assert p.get("IOUserClientClass") == "RDNA3UserClient"

    libs = pl.get("OSBundleLibraries", {})
    assert "com.apple.kpi.iokit" in libs
    assert "com.apple.iokit.IOPCIFamily" in libs
    print("[PASS] Info.plist personality and dependencies verified")

def test_opencore_config():
    print("Testing OpenCore config.plist registration...")
    with open(CONFIG_PATH, "rb") as f:
        pl = plistlib.load(f)

    kexts = pl.get("Kernel", {}).get("Add", [])
    entry = None
    entry_idx = -1
    for idx, k in enumerate(kexts):
        if k.get("BundlePath") == "RDNA3Driver.kext":
            entry = k
            entry_idx = idx
            break

    assert entry is not None, "RDNA3Driver.kext not found in Kernel -> Add"
    assert entry.get("Enabled") is True, "RDNA3Driver.kext is not enabled"
    assert entry_idx == 2, f"Expected RDNA3Driver.kext at index 2 (after VirtualSMC), found at {entry_idx}"
    assert pl["Kernel"]["Quirks"]["PanicNoKextDump"] is False, "Expected PanicNoKextDump = False"

    # Check that SSDT-PLUG-ALT is removed
    for a in pl.get("ACPI", {}).get("Add", []):
        assert "SSDT-PLUG-ALT" not in a.get("Path", ""), f"SSDT-PLUG-ALT should be removed, found: {a.get('Path')}"

    print("[PASS] OpenCore config.plist registration and quirks verified")

def test_ocvalidate():
    print("Running ocvalidate on OpenCore config...")
    if not os.path.exists(OCVALIDATE):
        print("[SKIP] ocvalidate not available at", OCVALIDATE)
        return
    res = subprocess.run([OCVALIDATE, CONFIG_PATH], capture_output=True, text=True)
    assert res.returncode == 0, f"ocvalidate failed with return code {res.returncode}:\n{res.stdout}\n{res.stderr}"
    assert "No issues found" in res.stdout, f"ocvalidate reported issues:\n{res.stdout}"
    print("[PASS] ocvalidate passed with 0 issues")

if __name__ == "__main__":
    print("=== Running RDNA3Driver Kext & EFI Verification Tests ===")
    test_kext_structure()
    test_macho_header()
    test_info_plist()
    test_opencore_config()
    test_ocvalidate()
    print("=== All Kext & EFI tests PASSED successfully ===")
