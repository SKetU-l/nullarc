# EliteBook / Sequoia compatibility status

## Gate: blocked (2026-10-28)

`compat/elitebook-sequoia` is an experimental source branch based on upstream
`cbb08dd`. It is **not an installable Sequoia release**. The exact-24H32 checker
exits 1 because two `IOAccelLegacySurface` pure methods remain unresolved.
Passing the linkage/layout checks alone must not authorize packaging or installation.

Target: HP EliteBook 845 G10, Ryzen 7 PRO 7840U, Radeon 780M `1002:15bf`,
subsystem `103c:8b6e`, macOS 15.8.1 Sequoia build `24H32`.
The independently bootable PhxGPU/PhxFB fallback is maintained separately in
[`SKetU-l/phx-macos`](https://github.com/SKetU-l/phx-macos).

## Changes and evidence

- Add `1002:15bf` to PhxBringup and both PhxAccel personalities.
- PhxBringup build 33 exposes MC base, VRAM size, physical base and BAR0 size
  through debug selector 114. `mesboot` derives GART and aperture addresses
  from that layout; PSP TOC/TMR commands are assembled by the kext.
- The EliteBook layout is 512 MiB UMA, 256 MiB BAR0, MC
  `0x8000000000–0x801fffffff`, physical base `0x480000000`.
  This branch reserves TMR at relative 128–192 MiB and checks it fits VRAM and
  BAR0. The fallback's observed TMR at MC `0x8018000000`, physical `0x498000000`,
  is a separate allocation, not a constant to copy into this driver.
- Four named enums correct seven imported C++ symbol manglings.
- `NM_SEQUOIA` selects 61 tail-call shims for 27 non-exported Apple methods.
  The checker inspects the linked instructions, GOT targets and slot offsets.
- Six GL/2D pure declarations were recovered from exact-24H32 AMD/Intel
  implementations. The unsupported GL surface-mode stub returns `false`;
  allocation-info outputs are initialized. These stubs do not implement GL/2D.
- Bring-up, queue validation/adoption and accelerator-claim failures propagate
  to the boot script instead of allowing it to continue as if successful.
  `mesboot` also aborts on register, VRAM, doorbell or system-memory transport
  errors and short replies. Hardware-free fault injection covers these paths
  and verifies that script failures retain the active recovery watchdog.

The current checker verifies:

| Evidence | Coverage |
|---|---|
| Final linked private imports | 463 accelerator + 17 AGDC = **480**, externally exported |
| Apple base-vtable tail calls | **61** compiled shims for **27** local methods |
| Vtable lengths and non-pure method/parameter order | **15** classes |
| Pure method/parameter signatures | **47** exact-24H32 vendor references |
| AGDC pure callback | Separate call-site/argument-forwarding check |
| Apple metaclass sizes | **26**, including accelerator `0xdd8`, AGDC `0x110` |
| Selected private offsets | Accelerator `0xc78`/`0xcb0`, display machine `0x108`/`0x88`, pipe `0x98` |

The 50 pure slots comprise 47 vendor-corroborated slots, the AGDC callback and
the two unresolved surface slots. Ordinary C++ mangling does not establish
return types; call-site/implementation evidence is needed separately.
These are static compatibility checks, not proof of GPU or Metal functionality,
and do not validate every private field used by the upstream driver.

## The two unresolved slots

Slot numbers below are zero-based function entries, excluding the two Itanium
vtable header words. Exact-24H32 `IOAccelSurface` has 327 entries;
`IOAccelLegacySurface` has 334.

### Slot 327: incorrect upstream `setSyncCommand`

The source still declares a four-argument `setSyncCommand(...) const` here.
Exact-24H32 `IOAccelLegacySurface::submit_scanout_swap(unsigned int, unsigned int)`
and `submit_swap(unsigned int, unsigned int)` call `*0xa38(%r10)` at `0x1570b`
and `0x15ab7`. Both pass **eight explicit arguments** in addition to `this`:

- `%rsi`: event at surface + `0x10e8`;
- `%rdx` / `%rcx`: two resources;
- `%r8`: record at surface + `0x1350`;
- `%r9d`: scalar;
- three stack arguments, distinct from alignment padding.

`check_legacy_swap_callers()` reproduces this calling-shape check. It disproves
the current declaration but cannot recover every source-level type or the name.

Historical Apple `GeForce-beta6` has a genuine `nvSurface : IOAccelLegacySurface`
and identifies slot 327 as:

```cpp
submitSwapFlush(IOAccelEvent*, IOAccelResource2*, IOAccelResource2*,
                IOAccelSwapFlushRec const*, unsigned int, unsigned int,
                unsigned int, IOAccelBounds const*)
```

This is a strong lead, **not an independently established exact-24H32 signature**.
No return type is claimed from its mangled name.

### Slot 333: unidentified upstream `_pure335()`

Exact-24H32 contains a real `___cxa_pure_virtual` relocation at `0x728e8`,
inside the vtable starting at `0x71e70`. Padding follows; the MetaClass vtable
begins at `0x728f8`. This slot is not padding or a vtable-length mistake.

Historical Apple GeForce identifies it as `canTmpAlloc(unsigned int)`. Its
implementation writes `1` to `%al`, supporting a `bool` return. The older
NVIDIA WebDriver also supplies this name and bool-return evidence, but has an
older `IOAccelSurface2` hierarchy and different slot numbers.

No exact-24H32 concrete LegacySurface implementation or direct caller of slot
333 was found in the inspected accelerator, AMD X4000/X5000/X6000, Intel KBL/ICL,
AppleVirtualGraphics or AppleParavirtGPU binaries. AppleParavirtGPUIOGPUFamily
has no x86_64 slice. The complete KDK payload listing contains neither GeForce
nor an IOAcceleratorFamily2 debug-symbol bundle. Absence of a caller is not ABI proof.

### Avoid the sibling-vtable false positive

Exact-24H32 AMD X4000/X5000 surfaces inherit `IOAccelSurface`, not
`IOAccelLegacySurface`. AMD's `setSyncCommand` at 327 and `canTmpAlloc` at 328
are sibling-specific additions. Intel KBL/ICL similarly append `canTmpAlloc`
at 327. None identifies LegacySurface's appended slots.

The checker establishes both superclass chains, uses named non-pure entries
as alignment anchors and bounds comparisons by the verified shared ancestor.
Regression tests exercise sibling-tail rejection, false inheritance, reordered
prefixes, multilevel inheritance and the complete closed gate.

## Reproduce from Linux

Toolchain cache: `$HOME/.cache/phx-toolchain` (MacKernelSDK, Apple cctools ld64,
complete `MacOSX15.5.sdk`), plus Clang, `ld64.lld`, Python, `c++filt`,
`llvm-objdump` and Make. Build scripts also accept explicit toolchain paths.

```sh
sh tools/build_sequoia_kexts.sh
sh tools/build_sequoia_tools.sh
make test
python3 tests/test_sequoia_abi.py "$HOME/.cache/phx-research/kdk-24H32/target"
python3 -u tools/check_sequoia_abi.py "$HOME/.cache/phx-research/kdk-24H32/target"
```

The regression suite is expected to pass; the final command is expected to
**exit 1**, ending with `ABI gate closed:` and both `NMXSurface[327]` and
`NMXSurface[333]`. `build/sequoia` contains experimental analysis binaries only.
`make test` reports skipped firmware-fixture cases if fixtures are absent;
`make fixtures` and `REQUIRE_FIXTURES=1 make test` enable those cases.

Latest local verification: both kexts and userspace clients cross-built;
`REQUIRE_FIXTURES=1 make test` passed including all firmware fixtures and the
three fault-injection tests (21 scenarios); all five exact-KDK regression
tests passed. The ABI checker still exits 1 on exactly the two slots above.
Shell syntax, personality plist checks and `git diff --check` passed.

## Source provenance

Exact authority: [KDK 24H32](https://github.com/dortania/KdkSupportPkg/releases/tag/24H32),
archive SHA-256 `658a7905a47f6c788264b7aaac7a8f6f9e1f387cc41ae8ce227244471ef6def1`.
Per-binary SHA-256 values are enforced by `SOURCES` and `REFERENCE_SOURCES` in
`tools/check_sequoia_abi.py`. Versions: IOAcceleratorFamily2 `485.2`,
IOGraphicsFamily `599`, AppleGraphicsDeviceControl `8.1.10`.

Historical evidence (never used to satisfy the exact-build gate):

| Binary | Source | SHA-256 |
|---|---|---|
| GeForce-beta6 | [Pinned Apple Monterey beta 6 binary](https://raw.githubusercontent.com/dortania/PatcherSupportPkg/b0a1fa26db9ba6d64ac3f8c42174176e5a8a7e0f/Universal-Binaries/12.0%20Beta%206/System/Library/Extensions/GeForce.kext/Contents/MacOS/GeForce) | `1aefaa5253c775907074b5d2cb4108435d4f5beb620c73a1ea66c9059247a0aa` |
| GeForce (patched) | Same pinned revision, `12.0 Beta 6-23.4` directory | `dc666a02220d8d196a426e0e23d5a2d4c44719b70cee0ac9464d0b20845ed846` |
| GeForceWeb | [WebDriver 387.10.10.10.40.140](https://raw.githubusercontent.com/dortania/PatcherSupportPkg/master/Universal-Binaries/WebDriver-387.10.10.10.40.140/Library/Extensions/GeForceWeb.kext/Contents/MacOS/GeForceWeb) | `31d43ab12631418b1841860ea8ebdabd3ccb138dd6e8889216bd23bab531daf5` |

Durable research cache: `$HOME/.cache/phx-research`. `kdk-24H32` holds the
archive, extracted `target`, `payload-files.txt`, `accel-disassembly.txt` and
`submit-swap-disassembly.txt`; `monterey-geforce` and
`nvidia-webdriver-387.10.10.10.40.140` hold the historical binaries.
Apple/NVIDIA research binaries are not part of this source repository.

## Remaining work

1. Obtain exact-24H32 type/symbol evidence or a concrete LegacySurface
   implementation for both unresolved methods. Pure-vtable relocations alone
   carry no method name or parameter types. Historical continuity alone does
   not meet the current gate.
2. Correct declarations and stub behavior using that evidence, update the
   regression expectations, rebuild and pass the gate on the resulting binary.
3. Complete a Sequoia-specific installer and recoverable test staging. The
   existing installer still targets Tahoe; its instructions are not Sequoia
   installation instructions.
4. Validate bring-up and Metal on macOS using a separately bootable test
   configuration, preserving the working PhxGPU/PhxFB fallback. Start with
   explicit test applications before enabling WindowServer acceleration.
