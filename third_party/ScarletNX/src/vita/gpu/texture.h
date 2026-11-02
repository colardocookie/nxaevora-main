/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors
   SceGxmTexture's four control words as Vita3K's gxm/include/gxm/types.h lays them out and its
   modules/SceGxm/SceGxm.cpp (init_texture_base) and gxm/src/textures.cpp fill and read them (GPL-2.0-or-later,
   Copyright (C) 2026 Vita3K team) at Vita3K/Vita3K f68b6ee */
#ifndef SNX_VITA_TEXTURE_H
#define SNX_VITA_TEXTURE_H
#include <stdbool.h>
#include <stdint.h>

/* SceGxmTextureType, in the format's top three bits */
enum {
    GXM_TEXTURE_SWIZZLED = 0u,
    GXM_TEXTURE_CUBE = 2u,
    GXM_TEXTURE_LINEAR = 3u,
    GXM_TEXTURE_TILED = 4u,
    GXM_TEXTURE_LINEAR_STRIDED = 6u,
};
typedef struct {
    uint32_t w[4];
} GxmTexture;

static inline uint32_t gxm_bits(uint32_t word, unsigned at, unsigned count) { return (word >> at) & ((1u << count) - 1); }
static inline void gxm_set_bits(uint32_t* word, unsigned at, unsigned count, uint32_t value) {
    uint32_t mask = ((1u << count) - 1) << at;
    *word = (*word & ~mask) | ((value << at) & mask);
}
static inline uint32_t gxm_texture_type(const GxmTexture* t) { return t->w[1] >> 29; }
static inline bool gxm_texture_pow2(const GxmTexture* t) {
    return gxm_texture_type(t) == GXM_TEXTURE_SWIZZLED || gxm_texture_type(t) == GXM_TEXTURE_CUBE;
}
static inline uint32_t gxm_texture_width(const GxmTexture* t) {
    return gxm_texture_pow2(t) ? 1u << gxm_bits(t->w[1], 16, 4) : gxm_bits(t->w[1], 12, 12) + 1;
}
static inline uint32_t gxm_texture_height(const GxmTexture* t) {
    return gxm_texture_pow2(t) ? 1u << gxm_bits(t->w[1], 0, 4) : gxm_bits(t->w[1], 0, 12) + 1;
}
/* SceGxmTextureFormat: the base format (bits 24 to 28), its swizzle (12 to 14) and bit 31 */
static inline uint32_t gxm_texture_format(const GxmTexture* t) {
    return gxm_bits(t->w[1], 24, 5) << 24 | (t->w[0] >> 31) << 31 | gxm_bits(t->w[3], 28, 3) << 12;
}
static inline uint32_t gxm_texture_data(const GxmTexture* t) { return t->w[2] & ~3u; }
static inline uint32_t gxm_texture_palette(const GxmTexture* t) { return gxm_bits(t->w[3], 0, 26) << 6; }
static inline uint32_t gxm_texture_mip_count(const GxmTexture* t) {
    if (gxm_texture_type(t) == GXM_TEXTURE_LINEAR_STRIDED) return 1;
    uint32_t count = (gxm_bits(t->w[0], 17, 4) + 1) & 15;
    return count ? count : 1;
}
static inline uint32_t gxm_texture_min_filter(const GxmTexture* t) { return gxm_bits(t->w[0], 10, 2); }
static inline uint32_t gxm_texture_mag_filter(const GxmTexture* t) { return gxm_bits(t->w[0], 12, 2); }
static inline uint32_t gxm_texture_mip_filter(const GxmTexture* t) { return gxm_bits(t->w[0], 9, 1); }
static inline uint32_t gxm_texture_uaddr(const GxmTexture* t) { return gxm_bits(t->w[0], 6, 3); }
static inline uint32_t gxm_texture_vaddr(const GxmTexture* t) { return gxm_bits(t->w[0], 3, 3); }
/* a linear strided texture's stride in bytes: its mip filter, min filter, mip count and LOD bias fields hold it, in
   4-byte units less one */
static inline uint32_t gxm_texture_stride(const GxmTexture* t) {
    uint32_t units = gxm_bits(t->w[0], 9, 1) | gxm_texture_min_filter(t) << 1 | gxm_bits(t->w[0], 17, 4) << 3 |
                     gxm_bits(t->w[0], 21, 6) << 7;
    return (units + 1) * 4;
}
#endif
