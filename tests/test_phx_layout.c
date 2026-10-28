#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../tools/phxctl/phx_layout.h"

static void target_layout(void)
{
   struct phx_layout l = {0x8000000000ull, 512ull << 20, 0x480000000ull, 256ull << 20};
   assert(phx_layout_validate(&l) == 0);
   assert(phx_system_aperture_low(&l) == 0x00200000u);
   assert(phx_system_aperture_high(&l) == 0x002007ffu);
   assert(l.vram_pa + 0x23ff000ull == 0x4823ff000ull);
}

static void reference_layout(void)
{
   struct phx_layout l = {0x8000000000ull, 6ull << 30, 0x6a0000000ull, 256ull << 20};
   assert(phx_layout_validate(&l) == 0);
   assert(phx_system_aperture_low(&l) == 0x00200000u);
   assert(phx_system_aperture_high(&l) == 0x00205fffu);
}

static void rejects_unsafe_layouts(void)
{
   struct phx_layout small_vram = {0x8000000000ull, 64ull << 20, 0x480000000ull, 256ull << 20};
   struct phx_layout small_bar = {0x8000000000ull, 512ull << 20, 0x480000000ull, 32ull << 20};
   struct phx_layout unaligned = {0x8000001000ull, 512ull << 20, 0x480000000ull, 256ull << 20};
   struct phx_layout overflow = {0xffffffffff000000ull, 512ull << 20, 0x480000000ull, 256ull << 20};
   assert(phx_layout_validate(&small_vram) != 0);
   assert(phx_layout_validate(&small_bar) != 0);
   assert(phx_layout_validate(&unaligned) != 0);
   assert(phx_layout_validate(&overflow) != 0);
}

int main(void)
{
   target_layout();
   reference_layout();
   rejects_unsafe_layouts();
   puts("ok   PhxBringup runtime GPU layout calculations");
   return 0;
}
