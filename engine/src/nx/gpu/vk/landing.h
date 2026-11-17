/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_PERF_LANDING=1 (PERF-1, research 11:03): the landing of storage results with word compares. The same
   runs (buffer.c out_changed_runs) and the same bytes written (writeback.c vk_write_changed) as the byte loops, which
   stay the path with it unset. 2 adds landing_blocks, 3 landing_masked. No runtime header, so the host test links it
   alone. */
#ifndef SNX_GPU_VK_LANDING_H
#define SNX_GPU_VK_LANDING_H
#include <stdint.h>
#include <string.h>

extern int g_perf_landing;   /* the level: 0 off, 1, 2 or 3 */
extern int g_landing_masked; /* level 3 and landing_masked_supported() */

/* whether the w bytes at a and b differ: one 64-bit compare for a whole word */
static inline int landing_differs(const uint8_t* a, const uint8_t* b, uint32_t w) {
    if (w == 8) {
        uint64_t x, y;
        memcpy(&x, a, 8);
        memcpy(&y, b, 8);
        return x != y;
    }
    return memcmp(a, b, w) != 0;
}

/* Called for each run of bytes to land, in order: [from, from + n) */
typedef void (*LandingPut)(void* ctx, uint32_t from, uint32_t n);
/* The maximal runs of the bytes where res differs from org and cur still holds org, the byte rule of
   vk_write_changed, found a word at a time. cur may be the memory put writes: a run is put once the walk has passed
   its end, and the walk never reads a byte put wrote. The bytes put in all */
uint64_t landing_runs(const uint8_t* res, const uint8_t* org, const uint8_t* cur, uint32_t size, LandingPut put,
                      void* ctx);

#define LANDING_BLOCK 1024u
/* SNX_ARCEUS_PERF_LANDING=2, a measurement for the unattended bench only, never a PR (research 16:28): by block of
   LANDING_BLOCK bytes, none where res equals org, the whole block where cur equals org, else landing_runs' runs. Not
   exact: a guest write to a byte the shader left alone, between the block's compare and its copy, is undone. The
   bytes put in all */
uint64_t landing_blocks(const uint8_t* res, const uint8_t* org, const uint8_t* cur, uint32_t size, LandingPut put,
                        void* ctx);

/* whether the CPU has AVX-512F and BW and the OS saves their state (CPUID and XGETBV). 0 off x86 */
int landing_masked_supported(void);
/* SNX_ARCEUS_PERF_LANDING=3 (PERF-1c, research 16:28): the bytes of landing_runs with cur being dst, 64 a step, with
   a byte-masked store of res under (res != org) and (cur == org). The tail under 64 bytes goes to landing_runs' loop
   and put, which must write dst. Only where landing_masked_supported(): off x86 it is landing_runs. The bytes put */
uint64_t landing_masked(const uint8_t* res, const uint8_t* org, uint8_t* dst, uint32_t size, LandingPut put,
                        void* ctx);

#endif
