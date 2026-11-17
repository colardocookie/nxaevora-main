/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/vk/landing.h"

int g_perf_landing;

/* 0xFF in each byte where x's byte is not 0, as rebase_bytes builds its mask */
static uint64_t byte_mask(uint64_t x) {
    x |= x >> 4;
    x |= x >> 2;
    x |= x >> 1;
    return (x & 0x0101010101010101ull) * 0xFF;
}

typedef struct {
    LandingPut put;
    void* ctx;
    uint32_t from;
    int open;
    uint64_t total;
} Runs;
static void run_byte(Runs* r, uint32_t at, int land) {
    if (land && !r->open) {
        r->from = at;
        r->open = 1;
    } else if (!land && r->open) {
        r->put(r->ctx, r->from, at - r->from);
        r->total += at - r->from;
        r->open = 0;
    }
}

/* the runs of [k, to), the last one put at to */
static void runs_in(Runs* r, const uint8_t* res, const uint8_t* org, const uint8_t* cur, uint32_t k, uint32_t to) {
    while (k < to) {
        /* unchanged stretches: the library's vectorized compare, as the byte loop skips them */
        if (!r->open && to - k >= 1024 && !memcmp(res + k, org + k, 1024)) {
            k += 1024;
            continue;
        }
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        if (to - k >= 8) { /* byte j of the word is bits 8j to 8j+7 of the load */
            uint64_t a, b, c;
            memcpy(&a, res + k, 8);
            memcpy(&b, org + k, 8);
            if (a == b) {
                run_byte(r, k, 0);
                k += 8;
                continue;
            }
            memcpy(&c, cur + k, 8);
            const uint64_t land = byte_mask(a ^ b) & ~byte_mask(c ^ b);
            if (land == ~0ull || !land) {
                run_byte(r, k, land != 0);
            } else {
                for (uint32_t j = 0; j < 8; j++) run_byte(r, k + j, (land >> (8 * j) & 0xFF) != 0);
            }
            k += 8;
            continue;
        }
#endif
        run_byte(r, k, res[k] != org[k] && cur[k] == org[k]); /* the tail, or a big-endian host */
        k++;
    }
    run_byte(r, to, 0);
}

uint64_t landing_runs(const uint8_t* res, const uint8_t* org, const uint8_t* cur, uint32_t size, LandingPut put,
                      void* ctx) {
    Runs r = {put, ctx, 0, 0, 0};
    runs_in(&r, res, org, cur, 0, size);
    return r.total;
}

uint64_t landing_blocks(const uint8_t* res, const uint8_t* org, const uint8_t* cur, uint32_t size, LandingPut put,
                        void* ctx) {
    Runs r = {put, ctx, 0, 0, 0};
    for (uint32_t i = 0, n; i < size; i += n) {
        n = size - i < LANDING_BLOCK ? size - i : LANDING_BLOCK;
        if (!memcmp(res + i, org + i, n)) continue; /* the shader changed nothing here */
        if (!memcmp(cur + i, org + i, n)) {         /* nor the guest: the bytes left alone hold res */
            put(ctx, i, n);
            r.total += n;
            continue;
        }
        runs_in(&r, res, org, cur, i, i + n);
    }
    return r.total;
}
