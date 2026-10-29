#!/usr/bin/env python3
"""Stage an exact-24H32 package and a USB-preserving PHXNULLARC boot tree.

Usage: stage_sequoia_test.py KDK_ROOT BOOT_SNAPSHOT RELEASE_ZIP OUTPUT
Run the Sequoia cross-build scripts first. OUTPUT must not already exist.
This does not modify /boot. No personal OpenCore configuration is published.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import re
import shutil
import stat
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RELEASE_SHA = '2cba22d8f39b8ba4bd6e76ae82d182b9c7dce2b871acc7d63d0381350daaca12'
PACKAGE = 'nullarc-sequoia-24H32-test'
APPLE_GUID = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
BOOT_ARGS = ('amfi=0x80', 'amfi_get_out_of_my_way=0x1', 'phxdebug=1', 'nullarc_test=1', '-phxfboff')
USB_KEXTS = ('PhxXhciKick.kext', 'USBToolBox.kext', 'UTBDefault.kext')
USB_ACPI = ('SSDT-USB-Reset.aml', 'SSDT-USBX.aml')
FIRMWARE = ('psp_13_0_4_toc.bin', 'gc_11_0_1_pfp.bin', 'gc_11_0_1_me.bin',
            'gc_11_0_1_mec.bin', 'gc_11_0_1_mes_2.bin', 'gc_11_0_1_mes1.bin',
            'gc_11_0_1_imu.bin', 'gc_11_0_1_rlc.bin', 'sdma_6_0_1.bin')
MENU_ENTRY = ('    //NullArc (24H32 test)\n'
              '    comment: Experimental apps-only Metal; USB configuration preserved from Safe boot\n'
              '    protocol: efi\n'
              '    path: boot():/EFI/PHXNULLARC/OpenCore.efi\n\n')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tree_hashes(root):
    """Boot trees contain only regular files; reject symlinks/special files."""
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise ValueError(f'Unsupported boot asset: {path}')
        if path.is_file():
            result[path.relative_to(root).as_posix()] = digest(path)
    if not result:
        raise ValueError(f'Empty boot tree: {root}')
    return result


def nullarc_config(source):
    result = copy.deepcopy(source)
    additions = result['Kernel']['Add']
    forbidden = ('PhxGPU.kext', 'PhxBringup.kext', 'PhxAccel.kext')
    if any(x['BundlePath'] in forbidden for x in additions):
        raise ValueError('Clone source is not a clean Safe boot configuration')
    for name in USB_KEXTS:
        if not any(x['BundlePath'] == name and x['Enabled'] for x in additions):
            raise ValueError(f'Missing enabled USB dependency: {name}')
    for name in USB_ACPI:
        if not any(x['Path'] == name and x['Enabled'] for x in result['ACPI']['Add']):
            raise ValueError(f'Missing enabled USB ACPI: {name}')
    additions.append({
        'Arch': 'x86_64', 'BundlePath': 'PhxBringup.kext',
        'Comment': 'NullArc Sequoia 24H32 test (PhxAccel stays in AuxKC)',
        'Enabled': True, 'ExecutablePath': 'Contents/MacOS/PhxBringup',
        'MaxKernel': '24.99.99', 'MinKernel': '24.0.0',
        'PlistPath': 'Contents/Info.plist',
    })
    nvram = result['NVRAM']['Add'][APPLE_GUID]
    args = nvram['boot-args'].split()
    # Preserve every original argument; never silently override a conflicting value.
    for arg in BOOT_ARGS:
        key = arg.split('=')[0]
        if any(x.split('=')[0] == key and x != arg for x in args):
            raise ValueError(f'Conflicting boot argument: {key}')
        if arg not in args:
            args.append(arg)
    nvram['boot-args'] = ' '.join(args)
    if 'boot-args' not in result['NVRAM']['Delete'][APPLE_GUID]:
        raise ValueError('Safe source does not replace stale NVRAM boot-args')
    return result


def validate_assets(root, config):
    for x in config['Kernel']['Add']:
        if not x['Enabled']:
            continue
        bundle = root / 'Kexts' / x['BundlePath']
        info = plistlib.loads((bundle / x['PlistPath']).read_bytes())
        executable = x['ExecutablePath']
        if executable and not (bundle / executable).is_file():
            raise ValueError(f'Missing injected executable: {bundle / executable}')
        if info.get('CFBundleExecutable') and not executable:
            raise ValueError(f'ExecutablePath missing: {bundle}')
    for category, entries in (('ACPI', config['ACPI']['Add']),
                              ('Drivers', config['UEFI']['Drivers'])):
        for x in entries:
            if x['Enabled'] and not (root / category / x['Path']).is_file():
                raise ValueError(f'Missing enabled {category} asset: {x["Path"]}')


def add_menu(before):
    text = before.decode('utf-8')
    if '/EFI/PHXNULLARC/' in text:
        raise ValueError('NullArc entry already exists; refusing to duplicate it')
    anchor = '    //PhxGPU + PhxFB (fallback)\n'
    if text.count(anchor) != 1 or '/macOS' not in text.splitlines():
        raise ValueError('Expected dedicated macOS/fallback menu anchor not found')
    for name in ('PHXMAC', 'PHXMACSAFE'):
        if f'path: boot():/EFI/{name}/OpenCore.efi' not in text:
            raise ValueError(f'Missing fallback menu path: {name}')
    return text.replace(anchor, MENU_ENTRY + anchor, 1).encode('utf-8')


def extract_release_assets(archive, destination):
    if digest(archive) != RELEASE_SHA:
        raise ValueError('Release ZIP does not match the pinned v0.3.0-alpha digest')
    with zipfile.ZipFile(archive) as z:
        for entry in z.infolist():
            parts = PurePosixPath(entry.filename).parts
            if not parts or parts[0] != 'nullarc-0.3.0-alpha' or '..' in parts:
                raise ValueError(f'Unsafe release path: {entry.filename}')
            relative = PurePosixPath(*parts[1:])
            if not (str(relative).startswith('PhxGPU/') or
                    str(relative).startswith('gpu/fw/')):
                continue
            path = destination / relative
            if entry.is_dir():
                path.mkdir(parents=True, exist_ok=True)
                continue
            path.parent.mkdir(parents=True, exist_ok=True)
            data = z.read(entry)
            mode = entry.external_attr >> 16
            if stat.S_ISLNK(mode):
                target = data.decode('utf-8')
                if '/' in target or target in ('', '.', '..'):
                    raise ValueError(f'Unsafe release symlink: {entry.filename}')
                path.symlink_to(target)
            else:
                path.write_bytes(data)
                path.chmod(0o755 if mode & 0o111 else 0o644)
    for name in FIRMWARE:
        if not (destination / 'gpu/fw' / name).is_file():
            raise ValueError(f'Release firmware missing: {name}')
    for path in (destination / 'PhxGPU').rglob('*'):
        if path.is_symlink() and not path.resolve().is_relative_to(destination.resolve()):
            raise ValueError(f'Symlink escaped package: {path}')


def zip_package(root, output):
    """Stable ZIP timestamps, ordering and Unix symlink metadata."""
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in sorted(root.rglob('*')):
            if path.is_dir() and not path.is_symlink():
                continue
            info = zipfile.ZipInfo(root.name + '/' + path.relative_to(root).as_posix(),
                                   date_time=(2026, 10, 29, 0, 0, 0))
            info.create_system = 3
            info.external_attr = path.lstat().st_mode << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, os.readlink(path).encode() if path.is_symlink() else path.read_bytes())


def check_package_binaries(package):
    versions = {}
    for path in sorted(package.rglob('*')):
        if path.is_symlink() or not path.is_file() or not (
                path.suffix == '.dylib' or path.name in ('NVMTLDriver', 'PhxAccel', 'PhxBringup', 'phxctl', 'mesboot', 'mtlprobe')):
            continue
        headers = subprocess.check_output(['llvm-objdump', '--macho', '--arch=x86_64',
                                           '--private-headers', str(path)], text=True)
        match = re.search(r'cmd LC_BUILD_VERSION\s+cmdsize \d+\s+platform macos\s+sdk [\d.]+\s+minos ([\d.]+)', headers)
        if not match:
            match = re.search(r'cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version ([\d.]+)', headers)
        if 'X86_64' in headers and 'KEXTBUNDLE' in headers:
            # ld64's static kext mode omits deployment commands; the private
            # linkage/layout gate, not a userspace minOS, controls these builds.
            versions[path.relative_to(package).as_posix()] = 'kext (24H32 ABI gate)'
            continue
        if not match or 'X86_64' not in headers:
            raise ValueError(f'No x86_64 macOS deployment target: {path}')
        version = match.group(1)
        if tuple(int(x) for x in (version.split('.') + ['0', '0'])[:3]) > (15, 8, 1):
            raise ValueError(f'Package binary requires newer macOS: {path} ({version})')
        versions[path.relative_to(package).as_posix()] = version
    return versions


def stage(kdk, snapshot, archive, output, build):
    if output.exists():
        raise ValueError(f'Output already exists (not overwritten): {output}')
    source = snapshot / 'PHXMACSAFE'
    config = plistlib.loads((source / 'config.plist').read_bytes())
    updated = nullarc_config(config)
    before = (snapshot / 'limine.conf').read_bytes()
    after = add_menu(before)
    fallbacks = {name: tree_hashes(snapshot / name)
                 for name in ('PHXMAC', 'PHXMACSAFE')}
    validate_assets(source, config)
    subprocess.run([sys.executable, str(ROOT / 'tools/check_sequoia_abi.py'),
                    str(kdk), str(build / 'PhxAccel.kext/Contents/MacOS/PhxAccel')], check=True)
    if digest(archive) != RELEASE_SHA:
        raise ValueError('Release ZIP hash mismatch')
    output.mkdir(parents=True)
    boot = output / 'EFI/PHXNULLARC'
    shutil.copytree(source, boot)
    shutil.copytree(build / 'PhxBringup.kext', boot / 'Kexts/PhxBringup.kext')
    (boot / 'config.plist').write_bytes(plistlib.dumps(updated, sort_keys=False))
    validate_assets(boot, updated)
    boot_hashes = tree_hashes(boot)
    for relative, sha in fallbacks['PHXMACSAFE'].items():
        if relative != 'config.plist' and boot_hashes.get(relative) != sha:
            raise ValueError(f'Safe boot asset changed: {relative}')

    package = output / PACKAGE
    package.mkdir()
    extract_release_assets(archive, package)
    for name, relative in (('PhxBringup', 'EFI/OC/Kexts'), ('PhxAccel', 'Library/Extensions')):
        shutil.copytree(build / f'{name}.kext', package / relative / f'{name}.kext')
    (package / 'gpu/phxctl').mkdir(parents=True)
    for name in ('phxctl', 'mesboot', 'mtlprobe'):
        shutil.copy2(build / 'tools' / name, package / 'gpu/phxctl' / name)
        (package / 'gpu/phxctl' / name).chmod(0o755)
    for name in ('phxboot.sh', 'full.sh', 'bringup5.sh', 'kinst.sh', 'test-metal.sh', 'local.phxboot.plist'):
        shutil.copy2(ROOT / 'tools/phxctl' / name, package / 'gpu' / name)
        (package / 'gpu' / name).chmod(0o644 if name.endswith('.plist') else 0o755)
    for name in ('install.sh', 'uninstall.sh'):
        shutil.copy2(ROOT / 'install' / name, package / name)
        (package / name).chmod(0o755)
    for name in ('INSTALL-SEQUOIA.md', 'sequoia-abi-status.md'):
        shutil.copy2(ROOT / 'docs' / name, package / name)
    for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
        shutil.copy2(ROOT / name, package / name)
    shutil.copytree(ROOT / 'LICENSES', package / 'LICENSES')
    (package / 'README.txt').write_text(
        'NullArc experimental Sequoia 15.8.1 (24H32) package\n'
        'Read INSTALL-SEQUOIA.md. Start with apps-only Metal.\n'
        'The v0.3.0-alpha plugin/firmware are reused; kexts and tools are Sequoia builds.\n'
        'Static ABI passed; hardware/runtime functionality is NOT yet validated.\n')
    source_paths = [ROOT / 'Makefile']
    for directory in ('src/phxbringup', 'src/phxaccel', 'tools/phxctl', 'install'):
        source_paths.extend(p for p in (ROOT / directory).rglob('*') if p.is_file())
    source_paths.extend(ROOT / 'tools' / name for name in (
        'build_sequoia_kexts.sh', 'build_sequoia_tools.sh', 'check_sequoia_abi.py', 'stage_sequoia_test.py'))
    (package / 'build-info.json').write_text(json.dumps({
        'target': 'macOS 15.8.1 (24H32)', 'static_abi': 'passed',
        'runtime': 'not tested', 'release_sha256': RELEASE_SHA,
        'checkout_source_sha256': {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(source_paths)},
        'binary_minimum_macos': check_package_binaries(package),
    }, indent=2) + '\n')
    package_manifest = {p.relative_to(package).as_posix():
                        ('symlink:' + os.readlink(p) if p.is_symlink() else digest(p))
                        for p in sorted(package.rglob('*')) if p.is_file() or p.is_symlink()}
    (package / 'manifest.json').write_text(json.dumps(package_manifest, indent=2) + '\n')
    zip_path = output / f'{PACKAGE}.zip'
    zip_package(package, zip_path)
    (output / 'SHA256SUMS').write_text(f'{digest(zip_path)}  {zip_path.name}\n')
    (output / 'limine.before.conf').write_bytes(before)
    (output / 'limine.after.conf').write_bytes(after)
    manifest = {'schema': 1, 'build': '24H32', 'release_sha256': RELEASE_SHA,
                'limine_before_sha256': hashlib.sha256(before).hexdigest(),
                'limine_after_sha256': hashlib.sha256(after).hexdigest(),
                'fallbacks': fallbacks, 'nullarc': boot_hashes,
                'package': zip_path.name, 'package_sha256': digest(zip_path)}
    (output / 'boot-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Staged: {output}\nPackage: {zip_path}\nUSB assets and all original Safe boot records preserved')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('kdk', 'snapshot', 'archive', 'output'):
        parser.add_argument(name, type=Path)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/sequoia')
    args = parser.parse_args()
    try:
        stage(args.kdk, args.snapshot, args.archive, args.output, args.build)
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        sys.exit(f'Staging failed: {exc}')
