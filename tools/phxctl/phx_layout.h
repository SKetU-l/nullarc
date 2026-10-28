#ifndef PHX_LAYOUT_H
#define PHX_LAYOUT_H

#include <stdint.h>

#define PHX_LAYOUT_GART_BYTES (128ull << 20)
#define PHX_LAYOUT_BAR_REQUIRED 0x2590000ull

struct phx_layout {
   uint64_t vram_mc;
   uint64_t vram_size;
   uint64_t vram_pa;
   uint64_t bar0_size;
};

static inline int phx_layout_validate(const struct phx_layout *l)
{
   if (!l || !l->vram_mc || !l->vram_size || !l->vram_pa)
      return -1;
   if ((l->vram_mc | l->vram_size | l->vram_pa) & 0xffffffull)
      return -1;
   if (l->vram_size < PHX_LAYOUT_GART_BYTES || l->bar0_size < PHX_LAYOUT_BAR_REQUIRED)
      return -1;
   if (l->vram_mc + l->vram_size < l->vram_mc || l->vram_pa + l->vram_size < l->vram_pa)
      return -1;
   if ((l->vram_mc + l->vram_size - 1) >> 18 > UINT32_MAX)
      return -1;
   return 0;
}

static inline uint32_t phx_system_aperture_low(const struct phx_layout *l)
{
   return (uint32_t)(l->vram_mc >> 18);
}

static inline uint32_t phx_system_aperture_high(const struct phx_layout *l)
{
   return (uint32_t)((l->vram_mc + l->vram_size - 1) >> 18);
}

#endif
