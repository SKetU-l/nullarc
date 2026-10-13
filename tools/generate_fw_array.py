#!/usr/bin/env python3
"""
tools/generate_fw_array.py

Downloads the official Realtek RTL8852B firmware (rtw8852b_fw.bin) from
the official kernel firmware repository (or mirrors) and generates:
  - src/firmware/rtw8852b_fw_bin.h
  - src/firmware/rtw8852b_fw_bin.c
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

FIRMWARE_URLS = [
    "https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/rtw89/rtw8852b_fw.bin",
    "https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main/rtw89/rtw8852b_fw.bin",
    "https://raw.githubusercontent.com/wkennington/linux-firmware/master/rtw89/rtw8852b_fw.bin",
    "https://raw.githubusercontent.com/CoreELEC/linux-firmware/master/rtw89/rtw8852b_fw.bin",
]

LOCAL_FALLBACK_PATHS = [
    Path("/lib/firmware/rtw89/rtw8852b_fw.bin"),
    Path("/lib/firmware/rtw89/rtw8852b_fw.bin.zst"),
]

MIN_FIRMWARE_SIZE = 200 * 1024  # > 200 KB
RTW89_MFW_SIG = 0xFF


def download_with_curl(url: str, dest_path: Path) -> bool:
    """Download url using curl with retries and realistic options."""
    if not shutil.which("curl"):
        return False
    cmd = [
        "curl",
        "-f",
        "-s",
        "-S",
        "-L",
        "--retry", "3",
        "--retry-delay", "2",
        "--connect-timeout", "15",
        "--max-time", "60",
        "-o", str(dest_path),
        url,
    ]
    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        return res.returncode == 0
    except Exception as exc:
        print(f"[-] curl invocation error: {exc}", file=sys.stderr)
        return False


def download_with_urllib(url: str, dest_path: Path) -> bool:
    """Download url using urllib with custom user agent and timeout."""
    headers = {
        "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
    }
    req = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            if resp.status != 200:
                return False
            data = resp.read()
            with open(dest_path, "wb") as f:
                f.write(data)
        return True
    except Exception as exc:
        print(f"[-] urllib download error: {exc}", file=sys.stderr)
        return False


def decompress_zstd(src_path: Path, dest_path: Path) -> bool:
    """Decompress .zst file if zstd or unzstd is available."""
    tool = shutil.which("unzstd") or shutil.which("zstd")
    if not tool:
        return False
    cmd = [tool, "-d", "-c", str(src_path)]
    try:
        with open(dest_path, "wb") as out_f:
            res = subprocess.run(cmd, stdout=out_f, stderr=subprocess.PIPE)
            return res.returncode == 0
    except Exception as exc:
        print(f"[-] zstd decompression error: {exc}", file=sys.stderr)
        return False


def validate_firmware_bytes(data: bytes) -> tuple[bool, str]:
    """Validate firmware size and Realtek header structure."""
    if len(data) == 0:
        return False, "Firmware content is empty"
    if len(data) <= MIN_FIRMWARE_SIZE:
        return False, f"Firmware size ({len(data)} bytes) is <= 200 KB minimum requirement"
    if data[0] != RTW89_MFW_SIG:
        return False, f"Invalid Realtek MFW signature: 0x{data[0]:02x} (expected 0x{RTW89_MFW_SIG:02x})"

    fw_nr = data[1]
    if fw_nr < 1:
        return False, f"Invalid subfirmware count: {fw_nr}"

    min_hdr_len = 16 + fw_nr * 16
    if len(data) < min_hdr_len:
        return False, f"Firmware data shorter than MFW table ({len(data)} < {min_hdr_len})"

    return True, f"Valid Realtek firmware ({len(data)} bytes, {fw_nr} sub-firmware components)"


def acquire_firmware(cache_path: Path | None = None) -> bytes:
    """Acquire and validate firmware binary from network or fallbacks."""
    if cache_path and cache_path.is_file():
        data = cache_path.read_bytes()
        valid, msg = validate_firmware_bytes(data)
        if valid:
            print(f"[+] Using existing valid firmware from {cache_path}: {msg}")
            return data

    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_file = Path(tmp_dir) / "rtw8852b_fw.bin"

        # Try network URLs first
        for idx, url in enumerate(FIRMWARE_URLS, start=1):
            print(f"[*] Trying mirror #{idx}: {url} ...")
            if download_with_curl(url, tmp_file) or download_with_urllib(url, tmp_file):
                if tmp_file.is_file():
                    data = tmp_file.read_bytes()
                    valid, msg = validate_firmware_bytes(data)
                    if valid:
                        print(f"[+] Download successful and verified: {msg}")
                        return data
                    else:
                        print(f"[-] Downloaded data invalid: {msg}")
            else:
                print(f"[-] Failed to download from {url}")

        # Try local fallbacks
        print("[*] Network downloads failed or unavailable; checking local fallbacks...")
        for local_path in LOCAL_FALLBACK_PATHS:
            if not local_path.is_file():
                continue
            if local_path.suffix == ".zst":
                print(f"[*] Found local compressed file {local_path}, attempting decompression...")
                if decompress_zstd(local_path, tmp_file):
                    data = tmp_file.read_bytes()
                    valid, msg = validate_firmware_bytes(data)
                    if valid:
                        print(f"[+] Local fallback verified: {msg}")
                        return data
            else:
                data = local_path.read_bytes()
                valid, msg = validate_firmware_bytes(data)
                if valid:
                    print(f"[+] Local fallback verified: {msg}")
                    return data

    raise RuntimeError("Failed to acquire valid rtw8852b_fw.bin from any mirror or local fallback")


def generate_header(dest_h: Path) -> None:
    """Generate src/firmware/rtw8852b_fw_bin.h"""
    header_content = """#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t rtw8852b_fw_bin[];
extern const size_t rtw8852b_fw_bin_len;

#ifdef __cplusplus
}
#endif
"""
    dest_h.parent.mkdir(parents=True, exist_ok=True)
    dest_h.write_text(header_content, encoding="utf-8")
    print(f"[+] Generated header: {dest_h}")


def generate_c_source(data: bytes, dest_c: Path) -> None:
    """Generate src/firmware/rtw8852b_fw_bin.c with hex byte array."""
    dest_c.parent.mkdir(parents=True, exist_ok=True)
    with open(dest_c, "w", encoding="utf-8") as f:
        f.write('/* Automatically generated by tools/generate_fw_array.py — DO NOT EDIT */\n')
        f.write('#include "rtw8852b_fw_bin.h"\n\n')
        f.write('const uint8_t rtw8852b_fw_bin[] = {\n')

        # Format 16 bytes per line
        bytes_per_line = 16
        total = len(data)
        buffer = []
        for i in range(0, total, bytes_per_line):
            chunk = data[i:i + bytes_per_line]
            hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
            if i + bytes_per_line < total:
                buffer.append(f"    {hex_str},\n")
            else:
                buffer.append(f"    {hex_str}\n")

            if len(buffer) >= 1024:
                f.writelines(buffer)
                buffer.clear()

        if buffer:
            f.writelines(buffer)

        f.write('};\n\n')
        f.write('const size_t rtw8852b_fw_bin_len = sizeof(rtw8852b_fw_bin);\n')

    print(f"[+] Generated C source: {dest_c} ({dest_c.stat().st_size} bytes)")


def main():
    parser = argparse.ArgumentParser(description="Download Realtek RTL8852B firmware and generate C array files.")
    parser.add_argument("--repo-root", type=Path, default=None, help="Root directory of the repository")
    parser.add_argument("--input-file", type=Path, default=None, help="Optional existing rtw8852b_fw.bin file")
    args = parser.parse_args()

    repo_root = args.repo_root or Path(__file__).resolve().parent.parent
    firmware_dir = repo_root / "src" / "firmware"
    dest_h = firmware_dir / "rtw8852b_fw_bin.h"
    dest_c = firmware_dir / "rtw8852b_fw_bin.c"

    print(f"[*] Repository root: {repo_root}")
    print(f"[*] Target header:  {dest_h}")
    print(f"[*] Target C source: {dest_c}")

    fw_data = acquire_firmware(cache_path=args.input_file)

    generate_header(dest_h)
    generate_c_source(fw_data, dest_c)
    print(f"[SUCCESS] RTL8852B firmware embedded C array successfully generated ({len(fw_data)} bytes).")


if __name__ == "__main__":
    main()
