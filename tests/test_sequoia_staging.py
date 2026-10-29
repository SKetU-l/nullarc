#!/usr/bin/env python3
"""Staging and recovery regressions, using temporary boot trees, never /boot."""
import copy
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from stage_sequoia_test import (APPLE_GUID, BOOT_ARGS, MENU_ENTRY, USB_ACPI, USB_KEXTS,
                                add_menu, digest, nullarc_config, tree_hashes,
                                validate_assets, zip_package)
from install_sequoia_boot import install_stage


def config_fixture():
    return {
        'ACPI': {'Add': [{'Enabled': True, 'Path': name} for name in USB_ACPI]},
        'Kernel': {'Add': [{'Enabled': True, 'BundlePath': name, 'PlistPath': 'Contents/Info.plist',
                            'ExecutablePath': ''} for name in USB_KEXTS],
                   'Quirks': {'XhciPortLimit': False}},
        'NVRAM': {'Add': {APPLE_GUID: {'boot-args': '-v npci=0x2000', 'csr-active-config': b'C\x0a\0\0'}},
                  'Delete': {APPLE_GUID: ['boot-args', 'csr-active-config']}},
        'UEFI': {'Drivers': []},
        'DeviceProperties': {'Add': {'PCI-test': {'USB-test': b'unchanged'}}},
    }


class ConfigTests(unittest.TestCase):
    def test_only_bringup_and_boot_args_change(self):
        source = config_fixture()
        before = copy.deepcopy(source)
        result = nullarc_config(source)
        self.assertEqual(source, before)
        for arg in BOOT_ARGS:
            self.assertIn(arg, result['NVRAM']['Add'][APPLE_GUID]['boot-args'].split())
        self.assertEqual(result['Kernel']['Add'][-1]['BundlePath'], 'PhxBringup.kext')
        result['Kernel']['Add'].pop()
        result['NVRAM']['Add'][APPLE_GUID]['boot-args'] = before['NVRAM']['Add'][APPLE_GUID]['boot-args']
        self.assertEqual(result, before)  # includes every USB record, property and quirk

    def test_unclean_source_or_missing_usb_is_rejected(self):
        for name in ('PhxGPU.kext', 'PhxAccel.kext', 'PhxBringup.kext'):
            source = config_fixture()
            source['Kernel']['Add'].append({'BundlePath': name})
            with self.subTest(name=name), self.assertRaises(ValueError):
                nullarc_config(source)
        source = config_fixture()
        source['Kernel']['Add'][0]['Enabled'] = False
        with self.assertRaises(ValueError):
            nullarc_config(source)

    def test_conflicting_boot_arg_is_not_overwritten(self):
        source = config_fixture()
        source['NVRAM']['Add'][APPLE_GUID]['boot-args'] += ' phxdebug=0'
        with self.assertRaises(ValueError):
            nullarc_config(source)

    def test_menu_edit_is_additive(self):
        before = (b'/Linux\n protocol: linux\n/macOS\n'
                  b'    //PhxGPU + PhxFB (fallback)\n'
                  b'    path: boot():/EFI/PHXMAC/OpenCore.efi\n'
                  b'    //Safe boot\n    path: boot():/EFI/PHXMACSAFE/OpenCore.efi\n')
        after = add_menu(before)
        self.assertEqual(after.replace(MENU_ENTRY.encode(), b'', 1), before)
        with self.assertRaises(ValueError):
            add_menu(after)

    def test_zip_preserves_symlink_and_is_reproducible(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as tmp:
            root = Path(tmp) / 'package'
            root.mkdir()
            (root / 'loader').write_bytes(b'dylib')
            (root / 'alias').symlink_to('loader')
            first, second = Path(tmp) / '1.zip', Path(tmp) / '2.zip'
            zip_package(root, first)
            (root / 'loader').touch()
            zip_package(root, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as z:
                self.assertEqual(z.read('package/alias'), b'loader')
                self.assertEqual(z.getinfo('package/alias').external_attr >> 28, 0xa)


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.boot, self.stage = self.root / 'boot', self.root / 'stage'
        self.boot.mkdir()
        self.stage.mkdir()
        self.before = (b'/macOS\n    //PhxGPU + PhxFB (fallback)\n'
                       b'    path: boot():/EFI/PHXMAC/OpenCore.efi\n'
                       b'    //Safe boot\n    path: boot():/EFI/PHXMACSAFE/OpenCore.efi\n')
        (self.boot / 'limine.conf').write_bytes(self.before)
        self.after = add_menu(self.before)
        fallbacks = {}
        for name in ('PHXMAC', 'PHXMACSAFE'):
            path = self.boot / 'EFI' / name
            path.mkdir(parents=True)
            (path / 'OpenCore.efi').write_bytes(b'loader')
            (path / 'usb.asset').write_bytes(b'keep-usb')
            fallbacks[name] = tree_hashes(path)
        source = self.stage / 'EFI/PHXNULLARC'
        shutil.copytree(self.boot / 'EFI/PHXMACSAFE', source)
        (source / 'config.plist').write_bytes(b'nullarc config')
        (self.stage / 'limine.before.conf').write_bytes(self.before)
        (self.stage / 'limine.after.conf').write_bytes(self.after)
        self.manifest = {'schema': 1, 'build': '24H32', 'fallbacks': fallbacks,
                         'nullarc': tree_hashes(source),
                         'limine_before_sha256': hashlib.sha256(self.before).hexdigest(),
                         'limine_after_sha256': hashlib.sha256(self.after).hexdigest()}
        (self.stage / 'boot-manifest.json').write_text(json.dumps(self.manifest))

    def publish(self, copy_package=False):
        # Fsync on files is real; don't force a global host sync in unit tests.
        with patch('install_sequoia_boot.os.sync'):
            install_stage(self.stage, self.boot, copy_package=copy_package)

    def test_publish_keeps_fallbacks_and_is_idempotent(self):
        self.publish()
        self.publish()
        self.assertEqual((self.boot / 'limine.conf').read_bytes(), self.after)
        self.assertEqual(len(list(self.boot.glob('limine.conf.before-nullarc-*'))), 1)
        for name, hashes in self.manifest['fallbacks'].items():
            self.assertEqual(tree_hashes(self.boot / 'EFI' / name), hashes)

    def test_stale_menu_or_fallback_aborts_before_changes(self):
        for changed in (self.boot / 'limine.conf', self.boot / 'EFI/PHXMACSAFE/usb.asset'):
            data = changed.read_bytes()
            changed.write_bytes(data + b'changed')
            with self.subTest(path=changed), self.assertRaises(ValueError):
                self.publish()
            self.assertFalse((self.boot / 'EFI/PHXNULLARC').exists())
            changed.write_bytes(data)

    def test_existing_tree_and_tampered_stage_are_not_overwritten(self):
        destination = self.boot / 'EFI/PHXNULLARC'
        destination.mkdir()
        with self.assertRaises(ValueError):
            self.publish()
        self.assertTrue(destination.exists())
        destination.rmdir()
        (self.stage / 'EFI/PHXNULLARC/usb.asset').write_bytes(b'bad')
        with self.assertRaises(ValueError):
            self.publish()
        self.assertEqual((self.boot / 'limine.conf').read_bytes(), self.before)

    def test_failed_menu_publication_retains_original_menu(self):
        with patch('install_sequoia_boot.atomic_file', side_effect=OSError('injected write failure')):
            with self.assertRaises(OSError):
                self.publish()
        self.assertEqual((self.boot / 'limine.conf').read_bytes(), self.before)
        self.assertTrue((self.boot / 'EFI/PHXNULLARC/OpenCore.efi').is_file())

    def test_portable_package_is_verified_before_publication(self):
        name = 'nullarc-sequoia-24H32-test.zip'
        package = self.stage / name
        package.write_bytes(b'portable test package')
        docs = self.stage / 'nullarc-sequoia-24H32-test/INSTALL-SEQUOIA.md'
        docs.parent.mkdir()
        docs.write_text('test guide')
        self.manifest.update(package=name, package_sha256=digest(package))
        (self.stage / 'boot-manifest.json').write_text(json.dumps(self.manifest))
        package.write_bytes(b'tampered package')
        with self.assertRaises(ValueError):
            self.publish(copy_package=True)
        self.assertFalse((self.boot / 'EFI/PHXNULLARC').exists())
        package.write_bytes(b'portable test package')
        self.publish(copy_package=True)
        self.assertEqual((self.boot / 'NullArc-24H32' / name).read_bytes(), package.read_bytes())
        self.assertEqual((self.boot / 'limine.conf').read_bytes(), self.after)


class InstallerTests(unittest.TestCase):
    def test_check_mode_os_entry_and_windowserver_guards(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as tmp:
            root = Path(tmp)
            script = root / 'install.sh'
            shutil.copyfile(ROOT / 'install/install.sh', script)
            for relative in ('EFI/OC/Kexts/PhxBringup.kext', 'Library/Extensions/PhxAccel.kext',
                             'PhxGPU/NVMTLDriver.bundle', 'PhxGPU/nvmtl/libvulkan_radeon.dylib',
                             'gpu/phxboot.sh', 'gpu/full.sh', 'gpu/bringup5.sh', 'gpu/kinst.sh', 'gpu/test-metal.sh',
                             'gpu/phxctl/phxctl', 'gpu/phxctl/mesboot', 'gpu/phxctl/mtlprobe', 'gpu/local.phxboot.plist'):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            for i in range(9):
                path = root / f'gpu/fw/{i}.bin'
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            bindir = root / 'bin'
            bindir.mkdir()
            fake = bindir / 'fake'
            fake.write_text('''#!/bin/sh
case "${0##*/}" in
 id) echo 0 ;;
 sw_vers) if [ "$1" = -productVersion ]; then echo 15.8.1; else echo "$MOCK_BUILD"; fi ;;
 nvram) printf 'boot-args\t%s\n' "$MOCK_ARGS" ;;
 csrutil) echo 'System Integrity Protection status: disabled.' ;;
 ioreg) echo '"phx-build" = 33' ;;
 *) echo "unexpected system mutation: $0" >&2; exit 99 ;;
esac
''')
            fake.chmod(0o755)
            for name in ('id', 'sw_vers', 'nvram', 'csrutil', 'ioreg', 'mkdir', 'ditto', 'install', 'rm', 'chown'):
                (bindir / name).symlink_to(fake)
            good = ' '.join(BOOT_ARGS)
            for build, args, ws, expected in (('24H32', good, False, 0), ('25G83', good, False, 1),
                                             ('24H32', 'phxdebug=1', False, 1), ('24H32', good, True, 1)):
                env = dict(os.environ, PATH=str(bindir) + os.pathsep + os.environ['PATH'],
                           MOCK_BUILD=build, MOCK_ARGS=args)
                result = subprocess.run(['bash', str(script), '--check'] + (['--windowserver'] if ws else []),
                                        env=env, text=True, capture_output=True, timeout=5)
                with self.subTest(build=build, args=args, ws=ws):
                    self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                    self.assertNotIn('unexpected system mutation', result.stderr)

    def test_daemon_exits_on_fallback_before_touching_markers(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as tmp:
            root = Path(tmp)
            text = (ROOT / 'tools/phxctl/phxboot.sh').read_text()
            text = text.replace('/var/log/phxboot.log', str(root / 'log'))
            (root / 'phxboot.sh').write_text(text)
            (root / 'nvram').write_text('#!/bin/sh\nprintf "boot-args\\t-v npci=0x2000\\n"\n')
            (root / 'nvram').chmod(0o755)
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'])
            result = subprocess.run(['bash', str(root / 'phxboot.sh')], env=env, timeout=5)
            self.assertEqual(result.returncode, 0)
            self.assertIn('not the NullArc test entry', (root / 'log').read_text())

    def test_auxkc_failures_never_replace_existing_collection(self):
        for failure in ('create', 'inspect', 'missing', ''):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory(dir='/tmp/opencode') as tmp:
                root = Path(tmp)
                extensions = root / 'Extensions'
                (extensions / 'PhxAccel.kext').mkdir(parents=True)
                collections = root / 'KernelCollections'
                collections.mkdir()
                kc = collections / 'AuxiliaryKernelExtensions.kc'
                kc.write_bytes(b'old collection')
                text = (ROOT / 'tools/phxctl/kinst.sh').read_text()
                text = text.replace('/Library/Extensions', str(extensions))
                text = text.replace('/Library/KernelCollections', str(collections))
                text = text.replace('/var/tmp/phx-kmutil.log', str(root / 'kmutil.log'))
                script = root / 'kinst.sh'
                script.write_text(text)
                fake = root / 'kmutil'
                fake.write_text('''#!/bin/bash
operation=$1
[ "$FAIL_STEP" != "$operation" ] || exit 1
if [ "$operation" = create ]; then
 while [ "$#" -gt 0 ]; do if [ "$1" = -A ]; then printf 'new collection' > "$2"; break; fi; shift; done
else
 [ "$FAIL_STEP" = missing ] || echo 'com.luuumi.PhxAccel 0.1'
fi
''')
                fake.chmod(0o755)
                env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
                           FAIL_STEP=failure, BK=str(root / 'backup'))
                result = subprocess.run(['bash', str(script)], env=env, text=True, capture_output=True, timeout=5)
                self.assertEqual(result.returncode, int(bool(failure)), result.stdout + result.stderr)
                self.assertEqual(kc.read_bytes(), b'old collection' if failure else b'new collection')
                if not failure:
                    self.assertEqual(next((root / 'backup').glob('aux.kc.*')).read_bytes(), b'old collection')


if __name__ == '__main__':
    unittest.main()
