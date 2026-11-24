/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* A screenshot's hash and PPM file from its RGBA8 pixels (renderer.c's vkr_screenshot). No Vulkan types: the renderer
   makes them after letting go of vk_lock, and tests/arceus_shot.c checks them against the earlier code's output. */
#ifndef SNX_VK_SHOT_H
#define SNX_VK_SHOT_H
#include "common/utilities/bits.h"
#include "common/utilities/endian.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SHOT_HASH_BASIS UINT64_C(1469598103934665603)
/* the pixels as the device wrote them, little-endian words: the hash is the same on every host */
static inline uint64_t shot_hash(const uint8_t* px, uint32_t w, uint32_t h) {
    uint64_t hash = SHOT_HASH_BASIS;
    for (uint64_t i = 0; i < (uint64_t)w * h; i++) hash = snx_fnv_step(hash, snx_ld_le32(px + i * 4));
    return hash;
}
/* the bytes of w x h RGBA8 pixels into *n: 0 when they don't fit a size_t (astra's note on d8c06cfb) */
static inline int shot_bytes(uint32_t w, uint32_t h, size_t* n) {
    if ((uint64_t)w * h > SIZE_MAX / 4) return 0;
    *n = (size_t)((uint64_t)w * h * 4);
    return 1;
}
#define SHOT_CHUNK 1024 /* pixels packed to RGB on the stack and written at a time */
/* the PPM of the pixels into f, 1024 pixels packed to RGB and written at a time, with nothing allocated (so it also
   serves the copy's fallback under memory pressure, the reviewer's finding): 1 when every write went through */
static inline int shot_write_ppm(FILE* f, const uint8_t* px, uint32_t w, uint32_t h) {
    uint8_t chunk[SHOT_CHUNK * 3];
    const uint64_t n = (uint64_t)w * h;
    int ok = fprintf(f, "P6\n%u %u\n255\n", w, h) > 0;
    for (uint64_t at = 0; ok && at < n; at += SHOT_CHUNK) {
        const uint32_t k = n - at < SHOT_CHUNK ? (uint32_t)(n - at) : SHOT_CHUNK;
        for (uint32_t i = 0; i < k; i++) {
            chunk[i * 3] = px[(at + i) * 4];
            chunk[i * 3 + 1] = px[(at + i) * 4 + 1];
            chunk[i * 3 + 2] = px[(at + i) * 4 + 2];
        }
        ok = fwrite(chunk, 1, (size_t)k * 3, f) == (size_t)k * 3;
    }
    return ok;
}
#endif
