#!/usr/bin/env python3
"""Run the real bring-up tools/scripts with only IOKit and host utilities mocked.

No GPU, firmware, EFI, shutdown, NVRAM or sysctl operations are performed.
"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TMPDIR = '/tmp/opencode' if Path('/tmp/opencode').is_dir() else None


class DriverTests(unittest.TestCase):
    def test_real_psp_control_methods(self):
        source = (ROOT / 'src/phxbringup/PhxBringup.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='phx-psp-', dir=TMPDIR) as tmp:
            root = Path(tmp)
            methods = []
            for method in ('layout', 'clearRing', 'pspRing', 'pspCtl', 'loadIpFw', 'claimAccel'):
                signature = ('void' if method == 'clearRing' else 'IOReturn') + ' PhxBringup::' + method + '('
                start = source.index(signature)
                end = source.index('\n}\n', start) + 3
                methods.append(source[start:end])
            inc = root / 'methods.inc'
            inc.write_text('\n'.join(methods))
            binary = root / 'psp-test'
            subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                f'-DPHX_PSP_METHODS_FILE="{inc}"', str(ROOT / 'tests/test_phx_psp.cpp'),
                '-o', str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=5)

IOKIT = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned io_connect_t;
typedef unsigned io_service_t;
typedef int kern_return_t;
#define kIOMainPortDefault 0
static uint32_t regs[0x18000], vram[0x4000000 / 4], sequence;
static uint64_t setting(const char *s, uint64_t fallback) {
    const char *v = getenv(s); return v ? strtoull(v, NULL, 0) : fallback;
}
static inline unsigned mach_task_self(void) { return 1; }
static inline const char *IOServiceMatching(const char *s) { return s; }
static inline io_service_t IOServiceGetMatchingService(unsigned p, const char *s) { return 1; }
static inline int IOServiceOpen(io_service_t s, unsigned t, unsigned type, io_connect_t *c) { *c = 1; return 0; }
static inline int IOObjectRelease(io_service_t s) { return 0; }
static inline int IOServiceClose(io_connect_t c) { puts("CLOSE"); return 0; }
static inline int mock_io(unsigned selector) {
    if (selector != 109 || setting("FAIL_SELECTOR", UINT32_MAX) == selector) printf("IO %u\n", selector);
    return selector == setting("FAIL_SELECTOR", UINT32_MAX);
}
static inline kern_return_t IOConnectCallScalarMethod(io_connect_t c, unsigned sel,
    const uint64_t *in, unsigned ni, uint64_t *out, unsigned *no) {
    if (mock_io(sel)) {
        if (getenv("SHORT_REPLY") && no) { *no = 0; return 0; }
        return 5;
    }
    if (no) for (unsigned i = 0; i < *no; ++i) out[i] = 0;
    if (sel == 0) out[0] = setting("HELLO_VERSION", 1);
    if (sel == 114) {
        out[0] = setting("MC", 0x9000000000ull); out[1] = setting("SIZE", 512ull << 20);
        out[2] = setting("PA", 0x480000000ull); out[3] = setting("BAR", 256ull << 20);
    }
    if (sel == 100) out[0] = setting("SMU_RESP", 1);
    if (sel == 101) out[0] = 0x80000000;
    if (sel == 103 || sel == 105 || sel == 106 || sel == 107) {
        out[0] = setting("PSP_STATUS", 0);
        if (sel == 106 && in[0] == 3) out[2] = setting("TMR_SIZE", 0x4000000);
    }
    if (sel == 108) {
        regs[in[0]] = in[1]; out[0] = in[1];
        printf("W %llx %llx\n", (unsigned long long)in[0], (unsigned long long)in[1]);
    }
    if (sel == 109) {
        vram[in[0] / 4] = in[1];
        if (in[0] == 0x2400000 || in[0] == 0x2400004)
            printf("V %llx %llx\n", (unsigned long long)in[0], (unsigned long long)in[1]);
    }
    if (sel == 110 && !getenv("NO_EXEC")) {
        if (in[0] == 0x16 * 4) { vram[0x2511020 / 4] = ++sequence; vram[0x2511040 / 4] = 1; }
        if (in[0] == 6 * 4 && !getenv("NO_COMPUTE")) {
            regs[0xc040] = 0xdeadbeef; vram[0x2520000 / 4] = 0xfeedf00d;
        }
        if (in[0] == 0x116 * 4 && !getenv("NO_GFX")) {
            regs[0xc041] = 0xdeadbeef; vram[0x2520040 / 4] = 0x6f7a6f7a;
        }
    }
    return 0;
}
static inline kern_return_t IOConnectCallMethod(io_connect_t c, unsigned sel,
    const uint64_t *in, unsigned ni, const void *data, size_t size,
    uint64_t *out, unsigned *no, void *reply, size_t *nr) {
    if (mock_io(sel)) {
        if (getenv("SHORT_REPLY") && nr) { *nr = 0; return 0; }
        return 5;
    }
    if (sel == 2) {
        uint32_t value = regs[in[0]];
        if (in[0] == 0x1260 + 0x16bd + 17) value = setting("GART_ACK", 1);
        if (in[0] == 0xa000 + 0x2807 && getenv("MES_INACTIVE")) value = 0;
        *(uint32_t *)reply = value;
    }
    if (sel == 102) memcpy(reply, &vram[in[0] / 4], *nr);
    return 0;
}
'''


class ToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='phx-io-', dir=TMPDIR)
        cls.addClassCleanup(cls.tmp.cleanup)
        root = Path(cls.tmp.name)
        (root / 'IOKit').mkdir()
        (root / 'IOKit/IOKitLib.h').write_text(IOKIT)
        # Eliminate only time delays, not the polling/control flow.
        (root / 'delay.h').write_text('#include <unistd.h>\n#define usleep(n) ((void)(n))\n')
        cls.binaries = {}
        for name in ('mesboot', 'phxctl'):
            cls.binaries[name] = root / name
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
                '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                '-include', str(root / 'delay.h'), '-I', str(root),
                str(ROOT / 'tools/phxctl' / (name + '.c')), '-o', str(root / name),
            ], check=True)
        cls.firmware = root / 'synthetic.bin'
        cls.firmware.write_bytes(bytes(32))

    def run_tool(self, tool, args, **settings):
        return subprocess.run([str(self.binaries[tool]), *args], env=dict(os.environ, **settings),
                              capture_output=True, text=True, timeout=5)

    def test_transport_errors_stop_mesboot(self):
        cases = ((2, ['dump'], 'register read', True), (108, ['mes'], 'register write', True),
                 (102, ['gfx'], 'VRAM read', True), (109, ['gart'], 'VRAM write', False),
                 (110, ['db', '24', '1'], 'doorbell write', False),
                 (111, ['sdmasys'], 'system memory I/O', True))
        for selector, args, operation, has_reply in cases:
            for short in (False, True) if has_reply else (False,):
                with self.subTest(selector=selector, short=short):
                    r = self.run_tool('mesboot', args, FAIL_SELECTOR=str(selector),
                                      **({'SHORT_REPLY': '1'} if short else {}))
                    self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
                    self.assertIn(operation + ' failed:', r.stderr)
                    calls = [s for s in r.stdout.splitlines() if s.startswith('IO ')]
                    self.assertEqual(calls[:2], ['IO 0', 'IO 114'])
                    self.assertEqual(calls[-1], f'IO {selector}')
                    self.assertEqual(calls.count(f'IO {selector}'), 1)
                    self.assertTrue(r.stdout.rstrip().endswith('CLOSE'))

    def test_layout_rejected_before_writes(self):
        for tool in self.binaries:
            for setting in ({'FAIL_SELECTOR': '114'}, {'FAIL_SELECTOR': '114', 'SHORT_REPLY': '1'},
                            {'MC': '0'}, {'PA': '1'}, {'SIZE': str(128 << 20)},
                            {'BAR': str(64 << 20)}, {'MC': '0xffffffffff000000'}):
                with self.subTest(tool=tool, setting=setting):
                    r = self.run_tool(tool, ['gart' if tool == 'mesboot' else 'layout'], **setting)
                    self.assertNotEqual(r.returncode, 0)
                    self.assertNotIn('W ', r.stdout)
                    self.assertNotIn('V ', r.stdout)

    def test_gart_uses_detected_bases_and_reserved_default_page(self):
        for mc, pa, size in ((0x9000000000, 0x480000000, 512 << 20),
                             (0xa000000000, 0x760000000, 1024 << 20)):
            r = self.run_tool('mesboot', ['gart'], MC=hex(mc), PA=hex(pa), SIZE=str(size))
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            writes = {int(s.split()[1], 16): int(s.split()[2], 16)
                      for s in r.stdout.splitlines() if s.startswith('W ')}
            self.assertEqual(writes[0x1260 + 0x167d], mc >> 18)
            self.assertEqual(writes[0x1260 + 0x167e], (mc + size - 1) >> 18)
            self.assertEqual(writes[0x1260 + 0x16f3], ((pa + 0x2400000) | 1) & 0xffffffff)
            self.assertEqual(writes[0x1260 + 0x16f4], (pa + 0x2400000) >> 32)
            for low, high in ((0x15a8, 0x15a9), (0x15cb, 0x15cc)):
                self.assertEqual(writes[0x1260 + low], ((pa + 0x23ff000) >> 12) & 0xffffffff)
                self.assertEqual(writes[0x1260 + high], (pa + 0x23ff000) >> 44)
            self.assertIn(f'V 2400000 {(pa | 0x71) & 0xffffffff:x}', r.stdout)
            self.assertIn(f'V 2400004 {pa >> 32:x}', r.stdout)

    def test_execution_and_adoption_failures_are_not_success(self):
        for args, setting in ((['gart'], {'GART_ACK': '0'}), (['mes'], {'MES_INACTIVE': '1'}),
                              (['kiqsched'], {'NO_EXEC': '1'}),
                              (['kiqsched'], {'NO_COMPUTE': '1'}),
                              (['kiqsched'], {'NO_GFX': '1'}),
                              (['kiqsched'], {'FAIL_SELECTOR': '112'}),
                              (['claim'], {'FAIL_SELECTOR': '113'})):
            with self.subTest(args=args, setting=setting):
                r = self.run_tool('mesboot', args, MESBOOT_CS='1', MESBOOT_GFX='1',
                                  ADOPT='1', ADOPT_GFX='1', **setting)
                self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
                if 'NO_COMPUTE' in setting or 'NO_EXEC' in setting:
                    self.assertNotIn('IO 112', r.stdout)
        r = self.run_tool('mesboot', ['kiqsched'], MESBOOT_CS='1', MESBOOT_GFX='1', ADOPT='1', ADOPT_GFX='1')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(r.stdout.count('IO 112'), 2)

    def test_phxctl_returns_transport_and_firmware_failures(self):
        cases = ((100, ['smu', '0x16', '1']), (101, ['psp', 'create', '0x1100000']),
                 (103, ['submit', '0x50']), (104, ['stage', str(self.firmware), '0', '32', '0']),
                 (105, ['loadfw', str(self.firmware), '0', '32', '2', '0']),
                 (106, ['toc']), (106, ['tmr']), (106, ['autoload']),
                 (107, ['raw', '0x20']), (108, ['wreg', '0xde0', '1']), (109, ['vramw', '0', '1']))
        for selector, args in cases:
            with self.subTest(selector=selector, args=args):
                self.assertEqual(self.run_tool('phxctl', args, FAIL_SELECTOR=str(selector)).returncode, 1)
                if selector not in (104, 109):
                    self.assertEqual(self.run_tool('phxctl', args, FAIL_SELECTOR=str(selector), SHORT_REPLY='1').returncode, 1)
        for args in (['toc'], ['tmr'], ['autoload'], ['raw', '0x20'], ['submit', '0x50'],
                     ['loadfw', str(self.firmware), '0', '32', '2', '0']):
            self.assertEqual(self.run_tool('phxctl', args, PSP_STATUS='0xffffffff').returncode, 1)
        for response in ('0', '0xfc', '0xfd', '0xfe', '0xff'):
            self.assertEqual(self.run_tool('phxctl', ['smu', '0x16', '1'], SMU_RESP=response).returncode, 1)

    def test_toc_size_and_short_firmware_fail_closed(self):
        for size in ('0', '4097', '0x8000000'):
            self.assertEqual(self.run_tool('phxctl', ['toc'], TMR_SIZE=size).returncode, 1)
        for size in ('1', '0x8000000', '0x100000000'):
            r = self.run_tool('phxctl', ['tmr', size])
            self.assertEqual(r.returncode, 2)
            self.assertNotIn('IO 106', r.stdout)
        r = self.run_tool('phxctl', ['loadfw', str(self.firmware), '0', '64', '2', '0'])
        self.assertEqual(r.returncode, 1)
        self.assertNotIn('IO 105', r.stdout)
        self.assertEqual(self.run_tool('phxctl', ['stage', str(self.firmware), '0', '64', '0']).returncode, 1)


FAKE = '''#!/bin/sh
name=${0##*/}
if [ "$name" = id ]; then echo 0; exit 0; fi
if [ "$name" = bash ]; then
    if [ "$1" = -c ]; then echo "watchdog arm" >> "$TRACE"; echo 4242; exit 0; fi
    exec /bin/bash "$@"
fi
echo "$name $*" >> "$TRACE"
if [ -n "$FAIL_STEP" ]; then
    case "$name $*" in "$FAIL_STEP"*) exit 1;; esac
fi
if [ "$name" = ioreg ]; then
    case "$*" in
      -l) echo '"phx-build" = 35';;
      *IOFramebuffer*) echo 'class IOFramebuffer';;
      *PhxAMDFB*) [ "$M2_TEST" = 1 ] && echo PhxAMDFB;;
      *NMXAccel*) echo NMXAccel;;
    esac
    exit 0
fi
echo "$name $*"
exit 0
'''


class ScriptTests(unittest.TestCase):
    def sandbox(self, root, failure='', m2=False):
        gpu = root / 'gpu'; (gpu / 'phxctl').mkdir(parents=True)
        bindir = root / 'bin'; bindir.mkdir()
        trace = root / 'trace'; trace.touch()
        fake = root / 'fake'; fake.write_text(FAKE); fake.chmod(0o755)
        for name in ('id', 'bash', 'shutdown', 'sleep', 'sysctl', 'nvram', 'sync', 'ioreg'):
            (bindir / name).symlink_to(fake)
        for name in ('phxctl', 'mesboot'):
            (gpu / 'phxctl' / name).symlink_to(fake)
        for script in ('bringup5.sh', 'full.sh', 'phxboot.sh'):
            shutil.copyfile(ROOT / 'tools/phxctl' / script, gpu / script)
        env = dict(os.environ, HOME=str(root), PATH=str(bindir) + os.pathsep + os.environ['PATH'],
                   TRACE=str(trace), FAIL_STEP=failure, M2_TEST=str(int(m2)))
        # Bash's kill is a builtin: override it, so no real process can be signalled.
        env['BASH_FUNC_kill%%'] = '() { echo "watchdog cancel $*" >> "$TRACE"; }'
        return gpu, trace, env

    def test_full_pipeline_preserves_owned_watchdog_on_failure(self):
        cases = (('phxctl layout', 'phxctl psp create'), ('phxctl toc', 'phxctl tmr'),
                 ('phxctl tmr', 'phxctl loadfw'), ('phxctl loadfw', 'phxctl autoload'),
                 ('phxctl smu', 'mesboot gart'), ('mesboot gart', 'mesboot mes'),
                 ('mesboot mes', 'mesboot kiqsched'), ('mesboot kiqsched', None), ('', None))
        for failure, forbidden in cases:
            with self.subTest(failure=failure), tempfile.TemporaryDirectory(prefix='phx-script-', dir=TMPDIR) as tmp:
                gpu, trace, env = self.sandbox(Path(tmp), failure)
                r = subprocess.run(['/bin/bash', str(gpu / 'full.sh'), 'kiqsched'], env=env,
                                   capture_output=True, text=True, timeout=5)
                self.assertEqual(r.returncode, int(bool(failure)), r.stdout + r.stderr)
                calls = trace.read_text().splitlines()
                self.assertEqual(calls.count('watchdog arm'), 1, calls)
                self.assertEqual(calls.count('watchdog cancel 4242'), int(not failure), calls)
                if forbidden: self.assertFalse(any(s.startswith(forbidden) for s in calls), calls)
                self.assertFalse(any(s.startswith('killall') for s in calls))
                if not failure:
                    self.assertIn('phxctl toc 0x900', calls)
                    self.assertIn('phxctl tmr', calls)
                    self.assertFalse(any(s.startswith('phxctl raw') for s in calls))

    def test_boot_never_claims_or_rearms_after_bringup_failure(self):
        for failure in ('phxctl toc', 'mesboot kiqsched', 'mesboot claim'):
            for m2 in (False, True):
                with self.subTest(failure=failure, m2=m2), tempfile.TemporaryDirectory(prefix='phx-boot-', dir=TMPDIR) as tmp:
                    root = Path(tmp); gpu, trace, env = self.sandbox(root, failure, m2)
                    (gpu / 'phxboot-m2').touch()
                    script = gpu / 'phxboot.sh'
                    # Relocate only production paths; execute the real script's control flow.
                    text = script.read_text().replace('G=/Users/Shared/gpu; export HOME=/Users/Shared',
                                                     f'G={gpu}; export HOME={root}')
                    for old, new in (('/var/log/phxboot.log', root / 'boot.log'),
                                     ('/var/db/phxboot.pending', root / 'pending'),
                                     ('/Library/GPUBundles/nvmtl-allow.txt', root / 'allow'),
                                     ('/Library/Logs/DiagnosticReports', root / 'reports')):
                        text = text.replace(old, str(new))
                    script.write_text(text)
                    subprocess.run(['/bin/bash', str(script)], env=env, capture_output=True, text=True, timeout=5)
                    calls = trace.read_text().splitlines()
                    if failure != 'mesboot claim': self.assertNotIn('mesboot claim', calls)
                    self.assertNotIn('nvram phxfb-boot=1', calls)
                    self.assertNotIn('done ', (root / 'boot.log').read_text())
                    if m2: self.assertIn('shutdown -r now', calls)
                    else:
                        self.assertIn('sysctl -w debug.phxfb_hold=0', calls)
                        self.assertTrue((root / 'pending').exists())
                        self.assertTrue((gpu / 'phxboot-off').exists())


if __name__ == '__main__':
    unittest.main()
