/* SPDX-License-Identifier: MIT
 * Shared bring-up reservations, not firmware/BIOS-owned memory discovery.
 * MC and system-physical bases must come from MMHUB, never from BAR0's PCI base.
 */
#ifndef PHX_MEMORY_LAYOUT_H
#define PHX_MEMORY_LAYOUT_H
#include <stdint.h>

#define PHX_VRAM_RING_OFF    0x01100000ull
#define PHX_VRAM_FWBUF_OFF   0x04000000ull
#define PHX_VRAM_FWBUF_SIZE  0x01000000ull
#define PHX_VRAM_TMR_OFF     0x08000000ull
#define PHX_VRAM_TMR_CAP     0x04000000ull
#define PHX_LAYOUT_GART_BYTES (128ull << 20)
#define PHX_LAYOUT_BAR_REQUIRED (PHX_VRAM_FWBUF_OFF + PHX_VRAM_FWBUF_SIZE)

struct phx_layout {
    uint64_t vram_mc, vram_size, vram_pa, bar0_size;
};

static inline int phx_layout_validate(const struct phx_layout *l) {
    if (!l || !l->vram_mc || !l->vram_pa ||
        ((l->vram_mc | l->vram_size | l->vram_pa) & 0xffffffull) ||
        l->vram_size < PHX_VRAM_TMR_OFF + PHX_VRAM_TMR_CAP ||
        l->bar0_size < PHX_LAYOUT_BAR_REQUIRED ||
        l->vram_mc > UINT64_MAX - l->vram_size ||
        l->vram_pa > UINT64_MAX - l->vram_size ||
        ((l->vram_mc + l->vram_size - 1) >> 18) > UINT32_MAX)
        return -1;
    return 0;
}

static inline int phx_tmr_size_valid(uint32_t size) {
    return size && !(size & 0xfffu) && size <= PHX_VRAM_TMR_CAP;
}

static inline uint32_t phx_system_aperture_low(const struct phx_layout *l) {
    return (uint32_t)(l->vram_mc >> 18);
}
static inline uint32_t phx_system_aperture_high(const struct phx_layout *l) {
    return (uint32_t)((l->vram_mc + l->vram_size - 1) >> 18);
}
#endif
