#!/usr/bin/env python3
"""
tools/build_kext.py — Build script for native RDNA3Driver.kext targeting macOS Sonoma/Tahoe.
Compiles freestanding C++17 modules with MacKernelSDK headers using Clang and ld64.lld.
"""

import os
import sys
import subprocess
import struct
import shutil
import plistlib

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SDK_HEADERS = os.path.join(PROJECT_ROOT, "reference", "MacKernelSDK", "Headers")
SDK_LIB = os.path.join(PROJECT_ROOT, "reference", "MacKernelSDK", "Library", "x86_64")
BUILD_DIR = os.path.join(PROJECT_ROOT, "build", "kext_obj")
KEXT_BUNDLE_DIR = os.path.join(PROJECT_ROOT, "build", "RDNA3Driver.kext")
DEST_KEXT_DIR = os.path.join(PROJECT_ROOT, "hardware", "efi_firebat_u6c_simplify", "EFI", "OC", "Kexts", "RDNA3Driver.kext")
CONFIG_PATH = os.path.join(PROJECT_ROOT, "hardware", "efi_firebat_u6c_simplify", "EFI", "OC", "config.plist")
OCVALIDATE_PATH = "/tmp/opencore_extracted/Utilities/ocvalidate/ocvalidate.linux"

SOURCES = [
    os.path.join(PROJECT_ROOT, "src", "kext", "RDNA3Driver.cpp"),
    os.path.join(PROJECT_ROOT, "src", "kext", "RDNA3UserClient.cpp"),
    os.path.join(PROJECT_ROOT, "src", "amd", "rdna3_dcn314_scanout.cpp"),
    os.path.join(PROJECT_ROOT, "src", "amd", "rdna3_ip_discovery.cpp"),
    os.path.join(PROJECT_ROOT, "src", "amd", "rdna3_rebar_uma.cpp"),
    os.path.join(PROJECT_ROOT, "src", "amd", "rdna3_ip_blocks.cpp"),
]

def run_cmd(cmd, check=True):
    print("RUN:", " ".join(cmd))
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0 and check:
        print("STDOUT:", res.stdout)
        print("STDERR:", res.stderr)
        raise RuntimeError(f"Command failed with code {res.returncode}")
    return res

def build_kext():
    print("=== Building RDNA3Driver.kext for macOS Sonoma (14.x) and Tahoe (15.x+) ===")
    os.makedirs(BUILD_DIR, exist_ok=True)
    os.makedirs(os.path.join(KEXT_BUNDLE_DIR, "Contents", "MacOS"), exist_ok=True)

    cflags = [
        "clang++",
        "-arch", "x86_64",
        "-target", "x86_64-apple-macos14.0",
        "-mmacosx-version-min=14.0",
        "-I" + SDK_HEADERS,
        "-I" + os.path.join(PROJECT_ROOT, "src"),
        "-DKERNEL", "-DKERNEL_PRIVATE", "-DDRIVER_PRIVATE", "-DAPPLE", "-DNeXT",
        "-D_FORTIFY_SOURCE=0",
        "-nostdinc",
        "-fno-builtin",
        "-fno-common",
        "-fno-stack-protector",
        "-mkernel",
        "-fapple-kext",
        "-std=c++17",
        "-fno-exceptions",
        "-fno-rtti",
        "-fcheck-new",
        "-fno-c++-static-destructors",
        "-Wall",
        "-Os",
        "-g",
    ]

    objects = []
    for src in SOURCES:
        basename = os.path.splitext(os.path.basename(src))[0]
        obj = os.path.join(BUILD_DIR, f"{basename}.o")
        cmd = cflags + ["-c", src, "-o", obj]
        run_cmd(cmd)
        objects.append(obj)

    raw_binary = os.path.join(BUILD_DIR, "RDNA3Driver_raw")
    ldflags = [
        "ld64.lld",
        "-arch", "x86_64",
        "-platform_version", "macos", "14.0", "14.0",
        "-bundle",
        "-undefined", "dynamic_lookup",
        "-L" + SDK_LIB,
        "-lkmod",
    ] + objects + ["-o", raw_binary]

    run_cmd(ldflags)

    # Patch byte 12 (FileType) from 0x8 (MH_BUNDLE) to 0xb (MH_KEXT_BUNDLE)
    with open(raw_binary, "rb") as f:
        data = bytearray(f.read())
    struct.pack_into("<I", data, 12, 0x0B)

    kext_exec = os.path.join(KEXT_BUNDLE_DIR, "Contents", "MacOS", "RDNA3Driver")
    with open(kext_exec, "wb") as f:
        f.write(data)
    os.chmod(kext_exec, 0o755)

    # Copy Info.plist
    src_plist = os.path.join(PROJECT_ROOT, "src", "kext", "Info.plist")
    dst_plist = os.path.join(KEXT_BUNDLE_DIR, "Contents", "Info.plist")
    shutil.copyfile(src_plist, dst_plist)

    print(f"Kext bundle successfully created at {KEXT_BUNDLE_DIR}")

    # Deploy to hardware EFI directory
    if os.path.exists(DEST_KEXT_DIR):
        shutil.rmtree(DEST_KEXT_DIR)
    shutil.copytree(KEXT_BUNDLE_DIR, DEST_KEXT_DIR)
    print(f"Deployed kext to {DEST_KEXT_DIR}")

    # Register in config.plist
    with open(CONFIG_PATH, "rb") as f:
        pl = plistlib.load(f)

    kext_entries = pl.get("Kernel", {}).get("Add", [])
    already_present = any("RDNA3Driver.kext" in k.get("BundlePath", "") for k in kext_entries)
    if not already_present:
        print("Registering RDNA3Driver.kext in config.plist Kernel -> Add")
        new_entry = {
            "Arch": "x86_64",
            "BundlePath": "RDNA3Driver.kext",
            "Comment": "Native AMD RDNA 3 (Radeon 780M / GFX1103) Graphics Driver",
            "Enabled": True,
            "ExecutablePath": "Contents/MacOS/RDNA3Driver",
            "MaxKernel": "",
            "MinKernel": "22.0.0",
            "PlistPath": "Contents/Info.plist"
        }
        # Insert after VirtualSMC (index 2)
        kext_entries.insert(2, new_entry)
        pl["Kernel"]["Add"] = kext_entries

        with open(CONFIG_PATH, "wb") as f:
            plistlib.dump(pl, f)
        print("config.plist updated with RDNA3Driver.kext")
    else:
        print("RDNA3Driver.kext already registered in config.plist")

    # Run ocvalidate
    if os.path.exists(OCVALIDATE_PATH):
        res = run_cmd([OCVALIDATE_PATH, CONFIG_PATH])
        print("ocvalidate output:\n", res.stdout)
    else:
        print("Warning: ocvalidate not found at", OCVALIDATE_PATH)

    print("=== Build and deployment completed successfully! ===")

if __name__ == "__main__":
    build_kext()
