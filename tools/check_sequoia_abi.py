#!/usr/bin/env python3
"""Check PhxAccel's private x86_64 ABI against the exact 24H32 KDK binaries.

Usage: check_sequoia_abi.py KDK_ROOT [build/sequoia/PhxAccel.kext/Contents/MacOS/PhxAccel]
Requires c++filt and llvm-objdump. KDK_ROOT contains System/Library/Extensions.
This checks linkage, layout and recovered method signatures, not GPU execution.
Unidentified pure slots fail the gate even when all linkage/layout checks pass.
"""
import argparse
import hashlib
import re
import struct
import subprocess
import sys
from pathlib import Path


SOURCES = {
    'accel': ('IOAcceleratorFamily2.kext/Contents/MacOS/IOAcceleratorFamily2',
              '0e73f4e1e293211d094993fa67ee074787444a443cc3aef425a17cef55e05218'),
    'graphics': ('IOGraphicsFamily.kext/IOGraphicsFamily',
                 'c212376eb19631d02db29928042560d05532f2c0bc421270b35856791c97ac9f'),
    'agdc': ('AppleGraphicsControl.kext/Contents/PlugIns/AppleGraphicsDeviceControl.kext/Contents/MacOS/AppleGraphicsDeviceControl',
             '0df49f75d1860207f6015561e4fad9e1fbfa63dcfe56b28b134a91ef12e5a153'),
}
# Supporting implementations from the same KDK. Never use their exports to
# resolve PhxAccel imports: they are not PhxAccel dependencies.
REFERENCE_SOURCES = {
    'amd4000': ('AMDRadeonX4000.kext/Contents/MacOS/AMDRadeonX4000',
                '07f8ab5bbe189cd93f992069d403ae1834d540796b01de0e83e736532d2aeead'),
    'amd5000': ('AMDRadeonX5000.kext/Contents/MacOS/AMDRadeonX5000',
                'f83cb082e9bd11587133a50671b4e0b4042dfbf3a493b08be27716d5afa84d0b'),
    'intel': ('AppleIntelKBLGraphics.kext/Contents/MacOS/AppleIntelKBLGraphics',
              'e0356ba762a909360976a91090803057b9acbf708d048954002b263e16d42a72'),
}
PURE_REFERENCES = {
    # Each reference is bounded by its verified shared ancestor. A sibling's
    # appended vendor methods cannot identify slots added by our Apple base.
    'IOAccelResource2': [('amd4000', 'AMDRadeonX4000_AMDAccelResource', 'IOAccelResource2')],
    'IOAccelVidMemory': [('amd4000', 'AMDRadeonX4000_AMDAccelVidMemory', 'IOAccelVidMemory')],
    'IOAccelLegacySurface': [('amd5000', 'AMDRadeonX5000_AMDAccelSurface', 'IOAccelSurface')],
    'IOAccelGLContext2': [('amd4000', 'AMDRadeonX4000_AMDSIGLContext', 'IOAccelGLContext2'),
                         ('intel', 'IGAccelGLContext', 'IOAccelGLContext2')],
    'IOAccel2DContext2': [('amd4000', 'AMDRadeonX4000_AMDAccel2DContext', 'IOAccel2DContext2'),
                         ('intel', 'IGAccel2DContext', 'IOAccel2DContext2')],
    'IOAccelEventMachineFast2': [('amd4000', 'AMDRadeonX4000_AMDAccelEventMachine', 'IOAccelEventMachineFast2')],
    'IOAccelLegacyDisplayMachine': [('amd4000', 'AMDRadeonX4000_AMDAccelDisplayMachine', 'IOAccelDisplayMachine')],
    'IOAccelMemoryMap': [('amd4000', 'AMDRadeonX4000_AMDAccelMemoryMap', 'IOAccelMemoryMap')],
    'IOGraphicsAccelerator2': [('amd4000', 'AMDRadeonX4000_AMDEllesmereGraphicsAccelerator', 'IOGraphicsAccelerator2')],
}
# No exact-24H32 concrete LegacySurface implementation has been found. The
# four-argument AMD setSyncCommand at 327 belongs to a sibling, not this class.
# Historical NVIDIA submitSwapFlush/canTmpAlloc are research leads only.
UNRECOVERED_PURE = {
    ('IOAccelLegacySurface', 327): 'Apple callers pass eight explicit arguments; exact signature unresolved',
    ('IOAccelLegacySurface', 333): 'no recovered exact-24H32 Apple signature (historical canTmpAlloc lead only)',
}
PAIRS = {
    'IOAccelResource2': 'NMXResource',
    'IOAccelSysMemory': 'NMXSysMemory',
    'IOAccelVidMemory': 'NMXVidMemory',
    'IOAccelLegacySurface': 'NMXSurface',
    'IOAccelLegacyDisplayPipe': 'NMXDisplayPipe',
    'IOAccelGLContext2': 'NMXGLContext',
    'IOAccel2DContext2': 'NMX2DContext',
    'IOAccelCLContext2': 'NMXCLContext',
    'IOAccelVideoContext2': 'NMXVideoContext',
    'IOAccelEventMachineFast2': 'NMXEventMachine',
    'IOAccelLegacyDisplayMachine': 'NMXDisplayMachine',
    'IOAccelTask': 'NMXTask',
    'IOAccelMemoryMap': 'NMXMemoryMap',
    'IOGraphicsAccelerator2': 'NMXAccel',
    'AppleGraphicsDeviceControl': 'PhxDeviceControl',
}
# These upstream slots deliberately use a public ancestor or a vendor override,
# rather than inheriting the family's local implementation. All other local
# entries must be forwarded, and the private-import check above remains strict.
LOCAL_OVERRIDES = {
    ('IOAccelResource2', 3): '__ZNK8OSObject14getRetainCountEv',
    ('IOAccelResource2', 57): '__ZN11NMXResource7pageoffEP12IOAccelEventbPby',
    ('IOGraphicsAccelerator2', 209): '__ZNK9IOService11getWorkLoopEv',
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def encoded(name):
    return f'{len(name)}{name}'


class MachO:
    """Read symbols, segments and classic kext external relocations."""
    def __init__(self, path):
        self.path = Path(path)
        data = self.path.read_bytes()
        if data[:4] == b'\xca\xfe\xba\xbe':
            for i in range(struct.unpack_from('>I', data, 4)[0]):
                cpu, _, off, size, _ = struct.unpack_from('>IIIII', data, 8 + 20 * i)
                if cpu == 0x1000007:
                    data = data[off:off + size]
                    break
        require(data[:4] == b'\xcf\xfa\xed\xfe', f'{path}: not 64-bit Mach-O')
        require(struct.unpack_from('<I', data, 4)[0] == 0x1000007, f'{path}: not x86_64')
        self.data = data
        self.segments = []
        symtab = dysymtab = None
        pos = 32
        for _ in range(struct.unpack_from('<I', data, 16)[0]):
            cmd, size = struct.unpack_from('<II', data, pos)
            require(size >= 8 and pos + size <= len(data), 'invalid load command')
            if cmd == 0x19:
                self.segments.append(struct.unpack_from('<QQQQ', data, pos + 24))
            elif cmd == 2:
                symtab = struct.unpack_from('<IIII', data, pos + 8)
            elif cmd == 0xb:
                dysymtab = struct.unpack_from('<18I', data, pos + 8)
            pos += size
        require(symtab is not None and dysymtab is not None, f'{path}: missing symbol tables')
        symoff, count, stroff, _ = symtab
        self.symbols = []
        self.defined = {}
        self.exported = set()
        self.imported = set()
        self.byaddr = {}
        for i in range(count):
            strx, typ, sect, _, value = struct.unpack_from('<IBBHQ', data, symoff + 16 * i)
            end = data.index(b'\0', stroff + strx)
            name = data[stroff + strx:end].decode()
            self.symbols.append(name)
            if typ & 0xe0:  # debugger STABS are not linkable definitions
                continue
            if (typ & 0xe) == 0xe and sect:
                self.defined[name] = value
                self.byaddr.setdefault(value, name)
                if typ & 1:
                    self.exported.add(name)
            elif (typ & 0xe) == 0 and typ & 1:
                self.imported.add(name)
        self.relocs = {}
        off, count = dysymtab[14:16]
        for i in range(count):
            addr, info = struct.unpack_from('<iI', data, off + 8 * i)
            require(info & (1 << 27), 'non-external relocation in external table')
            self.relocs[addr] = self.symbols[info & 0xffffff]

    def bytes_at(self, addr, size):
        for vm, _, off, length in self.segments:
            if vm <= addr and addr + size <= vm + length:
                return self.data[off + addr - vm:off + addr - vm + size]
        raise ValueError(f'{self.path}: unmapped address {addr:#x}')

    def pointer(self, addr, allow_unnamed=False):
        if addr in self.relocs:
            return self.relocs[addr]
        value, = struct.unpack('<Q', self.bytes_at(addr, 8))
        if not value:
            return None
        require(allow_unnamed or value in self.byaddr, f'{self.path}: unnamed vtable target {value:#x}')
        return self.byaddr.get(value, value)

    def vtable(self, cls, count=None, allow_unnamed=False):
        start = self.defined['__ZTV' + encoded(cls)]
        end = min(v for v in self.defined.values() if v > start)
        require((end - start) % 8 == 0, f'{cls}: unaligned vtable extent')
        if count is not None:
            require(0 <= count and start + 16 + count * 8 <= end, f'{cls}: truncated vtable prefix')
            end = start + 16 + count * 8
        slots = [self.pointer(a, allow_unnamed) for a in range(start + 16, end, 8)]
        if count is None:
            while slots and slots[-1] is None:
                slots.pop()
        require(all(slots), f'{cls}: empty interior slot')
        return slots

    def slot(self, cls, index):
        start = self.defined['__ZTV' + encoded(cls)]
        end = min(v for v in self.defined.values() if v > start)
        addr = start + 16 + index * 8
        require(start + 16 <= addr and addr + 8 <= end, f'{cls}[{index}]: outside vtable')
        target = self.pointer(addr)
        require(target and target != '___cxa_pure_virtual', f'{cls}[{index}]: no concrete reference')
        return target

    def superclass(self, cls):
        symbol = '__ZN' + encoded(cls) + '10superClassE'
        parent = self.pointer(self.defined[symbol])
        match = re.fullmatch(r'__ZN(\d+)(.+)10gMetaClassE', parent or '')
        require(match and len(match[2]) == int(match[1]), f'{cls}: unrecognized superclass {parent}')
        return match[2]

    def require_ancestor(self, cls, ancestor):
        seen = set()
        while cls != ancestor:
            require(cls not in seen, f'{cls}: cyclic superclass chain')
            seen.add(cls)
            symbol = '__ZN' + encoded(cls) + '10superClassE'
            require(symbol in self.defined, f'{cls}: cannot establish inheritance from {ancestor}')
            cls = self.superclass(cls)


def disassemble(binary, symbol):
    return subprocess.check_output([
        'llvm-objdump', '--macho', '--arch=x86_64', '--disassemble',
        '--dis-symname', symbol, str(binary.path),
    ], text=True)


def validate_reference(apple, base, reference, cls, shared, signature):
    """Establish inheritance and slot alignment before reading a pure target."""
    apple.require_ancestor(base, shared)
    reference.require_ancestor(cls, shared)
    common = apple.vtable(shared)
    for binary, derived in ((apple, base), (reference, cls)):
        # Some vendor non-pure overrides have stripped names. Use the named
        # entries as alignment anchors; reference_slot still requires a named
        # concrete target for every pure signature used as evidence.
        slots = binary.vtable(derived, len(common), allow_unnamed=binary is reference)
        for i, parent in enumerate(common):
            if parent != '___cxa_pure_virtual' and isinstance(slots[i], str):
                require(signature(slots[i]) == signature(parent),
                        f'{derived}[{i}]: slot does not match shared ancestor {shared}')
    return len(common)


def reference_slot(reference, cls, index, shared, limit):
    require(0 <= index < limit,
            f'{cls}[{index}]: outside shared ancestor {shared} ({limit} slots); vendor tail is not ABI evidence')
    return reference.slot(cls, index)


def check_legacy_swap_callers(apple):
    # At both call sites RDI is the surface, RSI its event at +0x10e8,
    # RDX/RCX the two resources, R8 the flush record at +0x1350, and R9D
    # a scalar. Three more arguments are pushed, separate from stack padding.
    # The old four-argument setSyncCommand declaration cannot describe this.
    callers = (
        ('__ZN20IOAccelLegacySurface19submit_scanout_swapEjj', 'r14', 'r12', 'r15', 'r13'),
        ('__ZN20IOAccelLegacySurface11submit_swapEjj', 'rbx', 'r14', 'r15', 'r11'),
    )
    for symbol, surface, src, dst, index in callers:
        code = disassemble(apple, symbol)
        patterns = (
            rf'leaq\s+0x10e8\(%{surface}\), %rsi',
            rf'leaq\s+0x1350\(%{surface}\), %r8',
            rf'movq\s+\(%{surface}\), %r10',
            rf'movq\s+%{surface}, %rdi',
            rf'movq\s+%{src}, %rdx',
            rf'movq\s+%{dst}, %rcx',
            r'movl\s+%r\d+d, %r9d',
            r'pushq\s+%rax\n[^\n]*pushq\s+\$0x0\n'
            rf'[^\n]*pushq\s+%{index}\n[^\n]*callq\s+\*0xa38\(%r10\)',
        )
        require(all(re.search(pattern, code) for pattern in patterns),
                f'{symbol}: LegacySurface[327] eight-argument call evidence changed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kdk_root', type=Path)
    parser.add_argument('kext', nargs='?', type=Path,
                        default=Path('build/sequoia/PhxAccel.kext/Contents/MacOS/PhxAccel'))
    args = parser.parse_args()
    sources = {}
    for name, (relpath, digest) in (SOURCES | REFERENCE_SOURCES).items():
        path = args.kdk_root / 'System/Library/Extensions' / relpath
        require(hashlib.sha256(path.read_bytes()).hexdigest() == digest,
                f'{path}: SHA-256 differs from 24H32')
        sources[name] = MachO(path)
    ours = MachO(args.kext)
    exports = set().union(*(sources[name].exported for name in SOURCES))
    imports = {s for s in ours.imported if any(k in s for k in
               ('IOAccel', 'IOGraphicsAccelerator', 'IOAccelerator', 'AppleGraphicsDeviceControl'))}
    missing = sorted(imports - exports)
    require(not missing, 'Private imports are not exported:\n' + '\n'.join(missing))
    require(len(imports) == 480, f'Unexpected private import count: {len(imports)} (expected 480)')
    print('PASS: 463 accelerator + 17 AGDC imports resolve to external 24H32 symbols')

    all_names = sorted(set(ours.defined) | set(ours.imported) |
                       set().union(*(set(b.defined) | b.imported for b in sources.values())))
    text = '\n'.join(s[1:] if s.startswith('__Z') else s for s in all_names)
    demangled = dict(zip(all_names, subprocess.check_output(['c++filt'], input=text, text=True).splitlines()))

    def signature(symbol):
        text = re.sub(r'^[^:]+::', '', demangled[symbol])
        return re.sub(r'^~\w+\(\)', '~Destructor()', text)

    reference_limits = {}
    for base, references in PURE_REFERENCES.items():
        for source, cls, shared in references:
            reference_limits[base, source, cls] = validate_reference(
                sources['accel'], base, sources[source], cls, shared, signature)
    print('PASS: pure-slot references have verified superclass chains and shared-base slot bounds')

    thunks = set()
    local_targets = set()
    pure_count = 0
    named_pure_count = 0
    unresolved_pure = []
    for base, vendor in PAIRS.items():
        apple = sources['agdc' if base == 'AppleGraphicsDeviceControl' else 'accel']
        a, b = apple.vtable(base), ours.vtable(vendor)
        require(len(a) == len(b), f'{vendor}: {len(b)} slots, Apple has {len(a)}')
        for i, (parent, child) in enumerate(zip(a, b)):
            if parent == '___cxa_pure_virtual':
                require(child in ours.defined, f'{vendor}[{i}]: unimplemented pure virtual')
                pure_count += 1
                if (base, i) in UNRECOVERED_PURE:
                    unresolved_pure.append(f'{vendor}[{i}]: {demangled[child]}: {UNRECOVERED_PURE[base, i]}')
                elif base == 'AppleGraphicsDeviceControl':
                    require(i == 266 and signature(child) ==
                            'vendor_doDeviceAttribute(unsigned int, unsigned long*, unsigned long, '
                            'unsigned long*, unsigned long*, IOExternalMethodArguments*)',
                            f'{vendor}[{i}]: unexpected AGDC callback declaration')
                else:
                    for source, cls, shared in PURE_REFERENCES[base]:
                        reference = reference_slot(sources[source], cls, i, shared,
                                                   reference_limits[base, source, cls])
                        require(signature(child) == signature(reference),
                                f'{vendor}[{i}]: {demangled[child]} != {demangled[reference]}')
                    named_pure_count += 1
                continue
            require(signature(parent) == signature(child),
                    f'{vendor}[{i}]: {demangled[child]} != {demangled[parent]}')
            if parent in apple.defined and parent not in apple.exported:
                if (base, i) in LOCAL_OVERRIDES:
                    require(child == LOCAL_OVERRIDES[base, i], f'{vendor}[{i}]: unexpected local override')
                    continue
                require(child in ours.defined, f'{vendor}[{i}]: local Apple method needs a shim')
                addr = ours.defined[child]
                code = ours.bytes_at(addr, 14)
                require(code[:3] == b'\x4c\x8b\x1d' and code[7:10] == b'\x41\xff\xa3',
                        f'{vendor}[{i}]: shim must be exactly mov GOT -> r11; jmp [r11 + offset]')
                got = addr + 7 + struct.unpack_from('<i', code, 3)[0]
                offset, = struct.unpack_from('<i', code, 10)
                require(ours.relocs.get(got) == '__ZTV' + encoded(base), f'{child}: incorrect base vtable')
                require(offset == 16 + i * 8, f'{child}: incorrect slot offset {offset}')
                thunks.add(child)
                local_targets.add(parent)
        print(f'PASS: {vendor}: {len(b)} slots; non-pure signatures in Apple order')
    require(len(thunks) == 61 and len(local_targets) == 27,
            f'Unexpected shim coverage: {len(thunks)} entries / {len(local_targets)} methods')
    require(pure_count == 50, f'Unexpected pure-slot count: {pure_count}')
    print('PASS: all 61 shims reach the correct Apple slots for 27 local methods; 50 pure slots have vendor entries')
    require(named_pure_count == 47, f'Unexpected named pure-slot coverage: {named_pure_count}')
    print('PASS: 47 pure-slot method/parameter signatures corroborated by exact 24H32 vendor binaries')

    check_legacy_swap_callers(sources['accel'])
    print('PASS: both exact-24H32 LegacySurface[327] callers pass eight explicit arguments')

    # The six-parameter AGDC overload preserves register arguments, replaces
    # its last stack argument with state->args and calls pure slot 266.
    code = disassemble(sources['agdc'],
                       '__ZN26AppleGraphicsDeviceControl24vendor_doDeviceAttributeEjPmmS0_S0_P17AGDCClientState_t')
    require(re.search(r'movq\s+0x38\(%rax\), %rax', code) and
            re.search(r'movq\s+%rax, \(%rsp\)', code) and
            re.search(r'callq\s+\*0x850\(%r10\)', code), 'AGDC callback forwarding changed')
    print('PASS: AGDC pure callback forwarding: slot 266 and final stack argument')

    # Ordinary C++ mangling omits return types. Intel's concrete method
    # explicitly returns true/false in AL; get_status consumes the two output
    # pointers of get_temp_allocation_info, not a return register.
    code = disassemble(sources['intel'], '__ZN16IGAccelGLContext24setCompatibleSurfaceModeEPyyi')
    require(re.search(r'movb\s+\$0x1, %al', code) and re.search(r'xorl\s+%eax, %eax', code),
            'GL surface-mode bool return evidence changed')
    code = disassemble(ours, '__ZN12NMXGLContext24setCompatibleSurfaceModeEPyyi')
    require(re.search(r'xorl\s+%eax, %eax', code), 'Unsupported GL mode must return false')
    print('PASS: unsupported GL surface-mode stub returns false with the recovered bool ABI')

    root = Path(__file__).resolve().parents[1] / 'src/phxaccel/accel'
    sizes = {'AppleGraphicsDeviceControl': 0x110}
    for header in list((root / 'iofam').glob('*.h')) + list((root / 're').glob('*.h')):
        match = re.search(r'k' + re.escape(header.stem) + r'Size\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)', header.read_text())
        if match:
            sizes[header.stem] = int(match[1], 0)
    for cls, size in sizes.items():
        apple = sources['agdc' if cls == 'AppleGraphicsDeviceControl' else
                        'graphics' if cls == 'IOAccelerator' else 'accel']
        code = disassemble(apple, '__ZN' + encoded(cls) + '9MetaClassC1Ev')
        match = re.search(r'movl\s+\$(0x[0-9a-f]+), %ecx', code)
        require(match and int(match[1], 0) == size, f'{cls}: metaclass size does not match {size:#x}')
    print(f'PASS: {len(sizes)} private class sizes, including IOGraphicsAccelerator2=0xdd8 and AGDC=0x110')
    code = disassemble(sources['accel'], '__ZN22IOGraphicsAccelerator217enableAcceleratorEv')
    require(re.search(r'orb\s+\$0x2, 0xc78\(%rbx\)', code), 'Accelerator enable flag moved')
    code = disassemble(sources['accel'], '__ZN21IOAccelDisplayMachine17found_framebufferEP13IOFramebuffer')
    require(re.search(r'cmpl\s+0xcb0\(%rdi\), %edx', code), 'Accelerator framebuffer limit moved')
    require(re.search(r'cmpl\s+\$0x0, 0x108\(%rdi\)', code), 'Display-machine pipe count moved')
    require(re.search(r'movq\s+0x88\(%rbx,%r15,8\), %rdi', code), 'Display-machine pipe table moved')
    code = disassemble(sources['accel'], '__ZNK18IOAccelDisplayPipe14getFramebufferEv')
    require(re.search(r'movq\s+0x98\(%rdi\), %rax', code), 'Display-pipe framebuffer moved')
    print('PASS: private offsets: accelerator 0xc78/0xcb0, display machine 0x108/0x88, display pipe 0x98')
    print('24H32 linkage/layout checks passed')
    require(not unresolved_pure, 'ABI gate closed:\n' + '\n'.join(unresolved_pure))
    print('24H32 private ABI checks passed; runtime functionality remains untested')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, struct.error, subprocess.CalledProcessError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(1)
