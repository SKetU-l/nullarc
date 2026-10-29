# EliteBook / Sequoia compatibility status

## Gate: passed (2026-10-29)

`compat/elitebook-sequoia` is an experimental source branch based on upstream
`cbb08dd`. The complete exact-24H32 static ABI checker now passes, including the
two recovered `IOAccelLegacySurface` pure methods. Runtime GPU functionality is
still untested. The Sequoia staging/publishing tools enforce the gate and keep
the independent PhxGPU/PhxFB and Safe boot paths unchanged; see
[INSTALL-SEQUOIA.md](INSTALL-SEQUOIA.md).

Target: HP EliteBook 845 G10, Ryzen 7 PRO 7840U, Radeon 780M `1002:15bf`,
subsystem `103c:8b6e`, macOS 15.8.1 Sequoia build `24H32`.
Earlier work on PhxGPU/PhxFB remains an independently bootable fallback while
development continues on this NullArc adaptation.

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
- `IOAccelLegacySurface` slot 327 is now the eight-argument
  `submitSwapFlush(...)`; slot 333 is `bool canTmpAlloc(unsigned int)`.
  The swap stub uses the existing flip/copy scanout path and temporary
  allocation is allowed, matching the pinned concrete LegacySurface behavior.
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
| Pure method/parameter signatures | **47** exact-24H32 vendor references + **2** recovered LegacySurface slots |
| AGDC pure callback | Separate call-site/argument-forwarding check |
| Apple metaclass sizes | **26**, including accelerator `0xdd8`, AGDC `0x110` |
| Selected private offsets | Accelerator `0xc78`/`0xcb0`, display machine `0x108`/`0x88`, pipe `0x98` |

The 50 pure slots comprise 47 vendor-corroborated slots, the AGDC callback and
the two recovered surface slots. Ordinary C++ mangling does not establish
return types; call-site/implementation evidence is checked separately.
These are static compatibility checks, not proof of GPU or Metal functionality,
and do not validate every private field used by the upstream driver.

## Recovered LegacySurface slots

Slot numbers below are zero-based function entries, excluding the two Itanium
vtable header words. Exact-24H32 `IOAccelSurface` has 327 entries;
`IOAccelLegacySurface` has 334.

### Slot 327: `submitSwapFlush(...)`

Exact-24H32 `IOAccelLegacySurface::submit_scanout_swap(unsigned int, unsigned int)`
and `submit_swap(unsigned int, unsigned int)` call `*0xa38(%r10)` at `0x1570b`
and `0x15ab7`. Both pass **eight explicit arguments** in addition to `this`:

- `%rsi`: event at surface + `0x10e8`;
- `%rdx` / `%rcx`: two resources;
- `%r8`: record at surface + `0x1350`;
- `%r9d`: scalar;
- three stack arguments, distinct from alignment padding.

`check_legacy_swap_callers()` reproduces this calling-shape check. It rejects
the former four-argument AMD sibling declaration and locks the exact calling ABI.

Historical Apple `GeForce-beta6` has a genuine `nvSurface : IOAccelLegacySurface`
and identifies slot 327 as:

```cpp
submitSwapFlush(IOAccelEvent*, IOAccelResource2*, IOAccelResource2*,
                IOAccelSwapFlushRec const*, unsigned int, unsigned int,
                unsigned int, IOAccelBounds const*)
```

That genuine subclass has the same seven-entry LegacySurface tail as 24H32:
the five named methods at slots 328–332 remain in the same order, with the two
pure slots at 327 and 333. Historical continuity supplies the method name and
source-level pointer types; the exact callers remain authoritative for the
24H32 argument count, order and slot. The caller ignores a return value, and
the recovered declaration uses the historical `void` behavior.

### Slot 333: `bool canTmpAlloc(unsigned int)`

Exact-24H32 contains a real `___cxa_pure_virtual` relocation at `0x728e8`,
inside the vtable starting at `0x71e70`. Padding follows; the MetaClass vtable
begins at `0x728f8`. This slot is not padding or a vtable-length mistake.

Every inspected exact-24H32 x86 vendor surface appends
`canTmpAlloc(unsigned int)`: AMD X4000/X5000/X6000 place it after their sibling
`setSyncCommand`, while Intel KBL/ICL append it directly to `IOAccelSurface`.
Intel returns through `%al` while the upper part of `%eax` still contains the
unsigned argument, establishing a one-byte `bool` ABI rather than an `int`.

Historical Apple GeForce binds that same method to genuine LegacySurface slot
333 and returns `true`; the older NVIDIA WebDriver independently supplies the
same name and bool-return evidence despite its older hierarchy. The NullArc
stub therefore returns `true`.

No exact-24H32 concrete LegacySurface implementation or direct caller of slot
333 exists in the inspected KDK binaries. The identity therefore rests on the
combination of the exact pure slot/tail layout, exact same-build sibling method
ABI and pinned direct-subclass continuity—not on absence of a caller.

### Avoid the sibling-vtable false positive

Exact-24H32 AMD X4000/X5000 surfaces inherit `IOAccelSurface`, not
`IOAccelLegacySurface`. AMD's `setSyncCommand` at 327 is a sibling-specific
addition and still cannot identify LegacySurface slot 327. Intel and AMD
`canTmpAlloc` entries establish the same-build method ABI but do not alone bind
it to LegacySurface slot 333; the genuine historical subclass provides that link.

The checker establishes both superclass chains, uses named non-pure entries
as alignment anchors and bounds comparisons by the verified shared ancestor.
Regression tests exercise sibling-tail rejection, false inheritance, reordered
prefixes, multilevel inheritance, both recovered signatures and the complete gate.

## Reproduce from Linux

Toolchain cache: `$HOME/.cache/phx-toolchain` (MacKernelSDK, Apple cctools ld64,
complete `MacOSX15.5.sdk`), plus Clang, `ld64.lld`, Python, `c++filt`,
`llvm-objdump` and Make. Build scripts also accept explicit toolchain paths.

```sh
sh tools/build_sequoia_kexts.sh
sh tools/build_sequoia_tools.sh
make test
KDK_ROOT=/run/media/sketu/HHD/phx-research/kdk-24H32/target
python3 tests/test_sequoia_abi.py "$KDK_ROOT"
python3 -u tools/check_sequoia_abi.py "$KDK_ROOT"
```

Both commands must pass. `build/sequoia` remains experimental until hardware
testing succeeds and the separate recoverable OpenCore test path is staged.
`make test` reports skipped firmware-fixture cases if fixtures are absent;
`make fixtures` and `REQUIRE_FIXTURES=1 make test` enable those cases.

Latest ABI verification: both kexts rebuilt; all seven exact-KDK regression
tests passed; the complete exact-24H32 checker passed, including 480 private
imports, 15 vtables, 50 pure slots, 61 shims, 26 class sizes and selected
private offsets. Runtime GPU functionality remains untested.

The full firmware-backed suite, bring-up fault injection and thirteen
staging/installer regressions also pass. The USB-preserving cloned plist and
the unchanged Safe boot source both pass `ocvalidate` 1.0.8 (the original
OpenCore loader is retained, not replaced). The initial test entry explicitly
blocks framebuffer takeover with `-phxfboff`; the shared daemon and Sequoia
framebuffer require `nullarc_test=1`, leaving fallback boots inactive.

## Source provenance

Exact authority: [KDK 24H32](https://github.com/dortania/KdkSupportPkg/releases/tag/24H32),
archive SHA-256 `658a7905a47f6c788264b7aaac7a8f6f9e1f387cc41ae8ce227244471ef6def1`.
Per-binary SHA-256 values are enforced by `SOURCES` and `REFERENCE_SOURCES` in
`tools/check_sequoia_abi.py`. Versions: IOAcceleratorFamily2 `485.2`,
IOGraphicsFamily `599`, AppleGraphicsDeviceControl `8.1.10`.

Historical continuity evidence (not substituted for exact-build slot/call checks):

| Binary | Source | SHA-256 |
|---|---|---|
| GeForce-beta6 | [Pinned Apple Monterey beta 6 binary](https://raw.githubusercontent.com/dortania/PatcherSupportPkg/b0a1fa26db9ba6d64ac3f8c42174176e5a8a7e0f/Universal-Binaries/12.0%20Beta%206/System/Library/Extensions/GeForce.kext/Contents/MacOS/GeForce) | `1aefaa5253c775907074b5d2cb4108435d4f5beb620c73a1ea66c9059247a0aa` |
| GeForce (patched) | Same pinned revision, `12.0 Beta 6-23.4` directory | `dc666a02220d8d196a426e0e23d5a2d4c44719b70cee0ac9464d0b20845ed846` |
| GeForceWeb | [WebDriver 387.10.10.10.40.140](https://raw.githubusercontent.com/dortania/PatcherSupportPkg/master/Universal-Binaries/WebDriver-387.10.10.10.40.140/Library/Extensions/GeForceWeb.kext/Contents/MacOS/GeForceWeb) | `31d43ab12631418b1841860ea8ebdabd3ccb138dd6e8889216bd23bab531daf5` |

The verified archive, regular-file KDK target and payload listing are stored at
`/run/media/sketu/HHD/phx-research/kdk-24H32`; the archive hash above was
rechecked after download. `$HOME/.cache/phx-research/monterey-geforce` and
`nvidia-webdriver-387.10.10.10.40.140` hold the historical binaries.
Apple/NVIDIA research binaries are not part of this source repository.

## Remaining work

1. Validate bring-up and Metal on macOS using the separately bootable test
   configuration, preserving the working PhxGPU/PhxFB fallback. Start with
   explicit test applications before enabling WindowServer acceleration.
2. Qualify Data-volume plugin sandbox access, application stability and
   sleep/wake separately; static ABI success does not establish these.
