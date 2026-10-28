#!/usr/bin/env python3
"""Regression checks against real KDK tables, including the sibling-tail trap.

Usage: python3 tests/test_sequoia_abi.py KDK_ROOT [PHXACCEL_BINARY]
Requires the exact 24H32 inputs used by tools/check_sequoia_abi.py.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.check_sequoia_abi import (MachO, SOURCES, REFERENCE_SOURCES,
                                    encoded, reference_slot, validate_reference)


class ReferenceRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binaries = {}
        for name, (relpath, digest) in (SOURCES | REFERENCE_SOURCES).items():
            path = args.kdk_root / 'System/Library/Extensions' / relpath
            if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise ValueError(f'{path}: not the exact 24H32 reference')
            cls.binaries[name] = MachO(path)
        names = sorted(set().union(*(b.defined.keys() | b.imported for b in cls.binaries.values())))
        text = '\n'.join(s[1:] if s.startswith('__Z') else s for s in names)
        cls.demangled = dict(zip(names, subprocess.check_output(
            ['c++filt'], input=text, text=True).splitlines()))

    @staticmethod
    def signature(symbol):
        text = ReferenceRegressionTests.demangled[symbol].split('::', 1)[-1]
        return '~Destructor()' if text.startswith('~') else text

    def test_surface_sibling_is_bounded_by_actual_parent(self):
        apple, amd = self.binaries['accel'], self.binaries['amd5000']
        cls = 'AMDRadeonX5000_AMDAccelSurface'
        limit = validate_reference(apple, 'IOAccelLegacySurface', amd, cls,
                                   'IOAccelSurface', self.signature)
        self.assertEqual(limit, 327)
        # A real inherited pure method is valid evidence.
        self.assertIn('submitSwapCopy', reference_slot(amd, cls, 302, 'IOAccelSurface', limit))
        # This exact symbol formerly caused a false positive for LegacySurface.
        self.assertIn('setSyncCommand', amd.slot(cls, 327))
        with self.assertRaisesRegex(ValueError, 'outside shared ancestor'):
            reference_slot(amd, cls, 327, 'IOAccelSurface', limit)
        with self.assertRaisesRegex(ValueError, 'outside shared ancestor'):
            reference_slot(amd, cls, 333, 'IOAccelSurface', limit)

    def test_sibling_cannot_be_claimed_as_legacy_subclass(self):
        with self.assertRaisesRegex(ValueError, 'cannot establish inheritance'):
            validate_reference(self.binaries['accel'], 'IOAccelLegacySurface',
                               self.binaries['amd5000'], 'AMDRadeonX5000_AMDAccelSurface',
                               'IOAccelLegacySurface', self.signature)

    def test_named_prefix_reordering_is_rejected(self):
        amd = self.binaries['amd5000']
        cls = 'AMDRadeonX5000_AMDAccelSurface'
        addr = amd.defined['__ZTV' + encoded(cls)] + 16 + 315 * 8
        wrong = amd.slot(cls, 316)
        with patch.dict(amd.relocs, {addr: wrong}):
            with self.assertRaisesRegex(ValueError, 'slot does not match shared ancestor'):
                validate_reference(self.binaries['accel'], 'IOAccelLegacySurface', amd,
                                   cls, 'IOAccelSurface', self.signature)

    def test_valid_multi_level_inheritance(self):
        amd = self.binaries['amd4000']
        cls = 'AMDRadeonX4000_AMDEllesmereGraphicsAccelerator'
        limit = validate_reference(self.binaries['accel'], 'IOGraphicsAccelerator2', amd,
                                   cls, 'IOGraphicsAccelerator2', self.signature)
        self.assertEqual(limit, 350)

    def test_complete_gate_reports_both_unresolved_slots(self):
        checker = Path(__file__).resolve().parents[1] / 'tools/check_sequoia_abi.py'
        result = subprocess.run([sys.executable, '-u', str(checker), str(args.kdk_root), str(args.kext)],
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('24H32 linkage/layout checks passed', result.stdout)
        self.assertIn('ABI gate closed:', result.stderr)
        self.assertIn('NMXSurface[327]', result.stderr)
        self.assertIn('NMXSurface[333]', result.stderr)
        self.assertNotIn('24H32 private ABI checks passed', result.stdout)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kdk_root', type=Path)
    parser.add_argument('kext', nargs='?', type=Path,
                        default=Path('build/sequoia/PhxAccel.kext/Contents/MacOS/PhxAccel'))
    args = parser.parse_args()
    unittest.main(argv=[sys.argv[0]])
