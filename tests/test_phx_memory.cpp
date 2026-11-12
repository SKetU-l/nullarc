#include "../src/phxbringup/PhxMemoryLayout.h"
#include <cassert>
#include <cstdio>

int main() {
    static_assert(PHX_VRAM_TMR_OFF >= PHX_LAYOUT_GART_BYTES, "TMR must not alias mapped queues/page tables");
    static_assert(PHX_VRAM_FWBUF_OFF + PHX_VRAM_FWBUF_SIZE <= PHX_VRAM_TMR_OFF, "staging/TMR overlap");
    phx_layout l{0x9000000000ull, 512ull << 20, 0x480000000ull, 256ull << 20};
    assert(!phx_layout_validate(&l));
    assert(phx_system_aperture_low(&l) == 0x240000);
    assert(phx_system_aperture_high(&l) == 0x2407ff);
    for (unsigned field = 0; field < 4; field++) {
        phx_layout bad = l;
        switch (field) {
        case 0: bad.vram_mc = 0; break;
        case 1: bad.vram_size = 0; break;
        case 2: bad.vram_pa = 0; break;
        case 3: bad.bar0_size = 0; break;
        }
        assert(phx_layout_validate(&bad));
    }
    phx_layout bad = l;
    bad.vram_size = PHX_VRAM_TMR_OFF;
    assert(phx_layout_validate(&bad));
    bad = l; bad.vram_mc++;
    assert(phx_layout_validate(&bad));
    bad = l; bad.vram_pa = 0xffffffffff000000ull;
    assert(phx_layout_validate(&bad));
    bad = l; bad.vram_mc = 1ull << 50;
    assert(phx_layout_validate(&bad));
    assert(phx_tmr_size_valid(0x4000000));
    assert(phx_tmr_size_valid(0x1000));
    assert(!phx_tmr_size_valid(0));
    assert(!phx_tmr_size_valid(0x4000001));
    assert(!phx_tmr_size_valid(0x8000000));
    puts("[PASS] detected layout bounds and disjoint TMR reservations");
}
