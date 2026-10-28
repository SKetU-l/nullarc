#!/usr/bin/env python3
"""Fault-inject the real bring-up CLI and scripts without accessing hardware."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TMPDIR = '/tmp/opencode' if Path('/tmp/opencode').is_dir() else None

# Only the transport is mocked. Compile the actual mesboot.c command paths.
IOKIT = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
typedef unsigned io_connect_t;
typedef unsigned io_service_t;
typedef int kern_return_t;
#define kIOMainPortDefault 0
static inline unsigned mach_task_self(void) { return 1; }
static inline const char *IOServiceMatching(const char *s) { return s; }
static inline io_service_t IOServiceGetMatchingService(unsigned p, const char *s) { return 1; }
static inline int IOServiceOpen(io_service_t s, unsigned t, unsigned type, io_connect_t *c) { *c = 1; return 0; }
static inline int IOObjectRelease(io_service_t s) { return 0; }
static inline int IOServiceClose(io_connect_t c) { puts("CLOSE"); return 0; }
static inline int mock_io(unsigned selector) {
    printf("IO %u\n", selector);
    const char *fail = getenv("FAIL_SELECTOR");
    return fail && selector == strtoul(fail, NULL, 0);
}
static inline kern_return_t IOConnectCallScalarMethod(io_connect_t c, unsigned selector,
    const uint64_t *in, unsigned ni, uint64_t *out, unsigned *no) {
    if (mock_io(selector)) {
        if (getenv("SHORT_REPLY") && no) { *no = 0; return 0; }
        return 5;
    }
    if (no) for (unsigned i = 0; i < *no; ++i) out[i] = 0;
    if (selector == 114) {
        out[0] = 0x8000000000ull; out[1] = 512ull << 20;
        out[2] = 0x480000000ull; out[3] = 256ull << 20;
    }
    return 0;
}
static inline kern_return_t IOConnectCallMethod(io_connect_t c, unsigned selector,
    const uint64_t *in, unsigned ni, const void *data, size_t size,
    uint64_t *out, unsigned *no, void *reply, size_t *nr) {
    if (mock_io(selector)) {
        if (getenv("SHORT_REPLY") && nr) { *nr = 0; return 0; }
        return 5;
    }
    return 0;
}
'''


class MesbootTransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='phx-io-', dir=TMPDIR)
        cls.addClassCleanup(cls.tmp.cleanup)
        root = Path(cls.tmp.name)
        (root / 'IOKit').mkdir()
        (root / 'IOKit/IOKitLib.h').write_text(IOKIT)
        cls.binary = root / 'mesboot'
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
            '-I', str(root), str(ROOT / 'tools/phxctl/mesboot.c'), '-o', str(cls.binary),
        ], check=True)

    def test_io_errors_abort_before_further_hardware_operations(self):
        cases = (
            (2, ['dump'], 'register read', True),
            (108, ['mes'], 'register write', True),
            (102, ['gfx'], 'VRAM read', True),
            (109, ['gart'], 'VRAM write', False),
            (110, ['db', '24', '1'], 'doorbell write', False),
            (111, ['sdmasys'], 'system memory I/O', True),
        )
        for selector, args, operation, has_reply in cases:
            for short in (False, True) if has_reply else (False,):
                with self.subTest(selector=selector, short=short):
                    env = dict(os.environ, FAIL_SELECTOR=str(selector))
                    env.pop('SHORT_REPLY', None)
                    if short:
                        env['SHORT_REPLY'] = '1'
                    result = subprocess.run([str(self.binary), *args], env=env,
                                            capture_output=True, text=True, timeout=5)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertIn(operation + ' failed:', result.stderr)
                    calls = [s for s in result.stdout.splitlines() if s.startswith('IO ')]
                    self.assertEqual(calls[:2], ['IO 0', 'IO 114'])
                    self.assertEqual(calls[-1], f'IO {selector}')
                    self.assertEqual(calls.count(f'IO {selector}'), 1)
                    self.assertTrue(result.stdout.rstrip().endswith('CLOSE'))

    def test_successful_transport_and_claim_status(self):
        for args in (['db', '24', '1'], ['claim']):
            for failed in (False, True):
                with self.subTest(args=args, failed=failed):
                    selector = '110' if args[0] == 'db' else '113'
                    env = dict(os.environ, FAIL_SELECTOR=selector if failed else '-1')
                    env.pop('SHORT_REPLY', None)
                    result = subprocess.run([str(self.binary), *args], env=env,
                                            capture_output=True, text=True, timeout=5)
                    self.assertEqual(result.returncode, int(failed), result.stdout + result.stderr)


class BringupScriptTests(unittest.TestCase):
    def test_pipeline_failures_stop_bringup_and_preserve_watchdog(self):
        cases = (
            ('phxctl toc', 'phxctl tmr', 0),
            ('phxctl loadfw', 'phxctl autoload', 0),
            ('phxctl smu', 'mesboot gart', 0),
            ('mesboot gart', 'mesboot mes', 1),
            ('mesboot mes', 'mesboot kiqsched', 1),
            ('mesboot kiqsched', None, 1),
            ('', None, 2),
        )
        for failure, forbidden, cancellations in cases:
            with self.subTest(failure=failure), tempfile.TemporaryDirectory(prefix='phx-scripts-', dir=TMPDIR) as tmp:
                root = Path(tmp)
                gpu = root / 'gpu'
                (gpu / 'phxctl').mkdir(parents=True)
                bindir = root / 'bin'
                bindir.mkdir()
                trace = root / 'trace'
                trace.touch()
                for script in ('bringup5.sh', 'full.sh'):
                    shutil.copyfile(ROOT / 'tools/phxctl' / script, gpu / script)
                # Override every host-affecting utility used by these scripts.
                fake = root / 'fake'
                fake.write_text('''#!/bin/sh
name=${0##*/}
if [ "$name" = id ]; then echo 0; exit 0; fi
echo "$name $*" >> "$TRACE"
echo "$name $*"
if [ -n "$FAIL_STEP" ]; then
    case "$name $*" in "$FAIL_STEP"*) exit 1;; esac
fi
exit 0
''')
                fake.chmod(0o755)
                for name in ('id', 'shutdown', 'killall', 'sleep'):
                    (bindir / name).symlink_to(fake)
                for name in ('phxctl', 'mesboot'):
                    (gpu / 'phxctl' / name).symlink_to(fake)
                env = dict(os.environ, HOME=str(root), PATH=str(bindir) + os.pathsep + os.environ['PATH'],
                           TRACE=str(trace), FAIL_STEP=failure, MC='1', CS='1', ADOPT='1', GFX='1', AG='1')
                result = subprocess.run(['bash', str(gpu / 'full.sh'), 'kiqsched'], env=env,
                                        capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, int(bool(failure)), result.stdout + result.stderr)
                calls = trace.read_text().splitlines()
                if failure:
                    self.assertTrue(any(s.startswith(failure) for s in calls), calls)
                if forbidden:
                    self.assertFalse(any(s.startswith(forbidden) for s in calls), calls)
                self.assertEqual(calls.count('killall shutdown'), cancellations, calls)


if __name__ == '__main__':
    unittest.main()
