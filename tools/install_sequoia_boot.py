#!/usr/bin/env python3
"""Publish a reviewed Sequoia stage to /boot, without overwriting fallback trees.

Usage: pkexec /usr/bin/python3 tools/install_sequoia_boot.py STAGE
Validates live snapshot hashes, backs up Limine, atomically renames the new
directory before publishing the menu, then verifies all fallback files again.
An already-existing PHXNULLARC tree is never overwritten.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time

from stage_sequoia_test import add_menu, digest, tree_hashes


def atomic_file(path, data):
    fd, temporary = tempfile.mkstemp(prefix=f'.{path.name}.', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def install_stage(stage, boot, copy_package=False):
    manifest = json.loads((stage / 'boot-manifest.json').read_text())
    if manifest.get('schema') != 1 or manifest.get('build') != '24H32':
        raise ValueError('Not a supported Sequoia test stage')
    if set(manifest['fallbacks']) != {'PHXMAC', 'PHXMACSAFE'}:
        raise ValueError('Both independent fallback manifests are required')
    source = stage / 'EFI/PHXNULLARC'
    if tree_hashes(source) != manifest['nullarc']:
        raise ValueError('Staged NullArc assets changed since review')
    before = (stage / 'limine.before.conf').read_bytes()
    after = (stage / 'limine.after.conf').read_bytes()
    if (hashlib.sha256(before).hexdigest() != manifest['limine_before_sha256'] or
            hashlib.sha256(after).hexdigest() != manifest['limine_after_sha256'] or
            after != add_menu(before)):
        raise ValueError('Staged Limine edit is not the reviewed additive entry')
    for name, expected in manifest['fallbacks'].items():
        if tree_hashes(boot / 'EFI' / name) != expected:
            raise ValueError(f'Live fallback changed since snapshot: {name}')
    conf = boot / 'limine.conf'
    current = conf.read_bytes()
    destination = boot / 'EFI/PHXNULLARC'
    if current == after and destination.is_dir() and tree_hashes(destination) == manifest['nullarc']:
        print('Reviewed NullArc boot entry already installed; fallbacks verified.')
        return
    if current != before:
        raise ValueError('Live Limine config changed since review; capture a new snapshot')
    if destination.exists():
        raise ValueError('PHXNULLARC already exists; never overwriting an unreviewed boot tree')
    if copy_package:
        package_name = manifest['package']
        if package_name != 'nullarc-sequoia-24H32-test.zip':
            raise ValueError('Unexpected portable package name')
        package_source = stage / package_name
        if digest(package_source) != manifest['package_sha256']:
            raise ValueError('Portable package hash changed since staging')
        portable = boot / 'NullArc-24H32'
        if portable.exists():
            raise ValueError('Portable package folder already exists; not overwriting it')
        pending = Path(tempfile.mkdtemp(prefix='.NullArc-24H32.', dir=boot))
        try:
            shutil.copy2(package_source, pending / package_name)
            (pending / 'SHA256SUMS').write_text(f'{manifest["package_sha256"]}  {package_name}\n')
            shutil.copy2(stage / 'nullarc-sequoia-24H32-test/INSTALL-SEQUOIA.md', pending / 'INSTALL-SEQUOIA.md')
            if digest(pending / package_name) != manifest['package_sha256']:
                raise ValueError('Copied portable package failed hash verification')
            os.sync()
            os.rename(pending, portable)
        finally:
            if pending.exists():
                shutil.rmtree(pending)
        print(f'Portable macOS package: {portable / package_name}')
    backup = boot / f'limine.conf.before-nullarc-{time.time_ns()}'
    with backup.open('xb') as stream:
        stream.write(current)
        stream.flush()
        os.fsync(stream.fileno())
    temporary = Path(tempfile.mkdtemp(prefix='.PHXNULLARC.', dir=boot / 'EFI'))
    try:
        shutil.copytree(source, temporary, dirs_exist_ok=True)
        if tree_hashes(temporary) != manifest['nullarc']:
            raise ValueError('Copied boot assets failed hash verification')
        os.sync()
        os.rename(temporary, destination)
        atomic_file(conf, after)
        os.sync()
        if digest(conf) != manifest['limine_after_sha256']:
            raise ValueError('Published Limine config failed verification')
        for name, expected in manifest['fallbacks'].items():
            if tree_hashes(boot / 'EFI' / name) != expected:
                raise ValueError(f'Fallback verification failed: {name}')
    except Exception:
        # Keep any published tree for investigation, but restore the prior menu.
        if conf.read_bytes() == after:
            atomic_file(conf, before)
            os.sync()
        raise
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    print(f'Installed: {destination}\nLimine backup: {backup}\nPHXMAC and PHXMACSAFE unchanged')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', type=Path)
    parser.add_argument('--copy-package', action='store_true',
                        help='Also copy the verified ZIP to a new /boot/NullArc-24H32 folder')
    args = parser.parse_args()
    if os.geteuid() != 0:
        sys.exit('Root privileges required; run the reviewed helper through pkexec')
    try:
        install_stage(args.stage.resolve(), Path('/boot'), copy_package=args.copy_package)
    except (ValueError, OSError, KeyError) as exc:
        sys.exit(f'Boot publication failed: {exc}')
