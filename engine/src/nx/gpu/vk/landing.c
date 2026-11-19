/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/vk/landing.h"
/* SNX_LANDING_PORTABLE: the code off x86 on an x86 host, for host tests on a CPU without AVX-512BW */
#if (defined(__x86_64__) || defined(__i386__)) && !defined(SNX_LANDING_PORTABLE)
#include <cpuid.h>
#include <immintrin.h>
#define LANDING_X86 1
#else
#define LANDING_X86 0
#endif

int g_perf_landing;
int g_landing_masked;

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

static void word_at(LandingWords* w, uint32_t at, int changed) {
    if (changed && !w->open) {
        w->from = at;
        w->open = 1;
    } else if (!changed && w->open) {
        w->chg(w->ctx, w->from, at - w->from);
        w->open = 0;
    }
}
void landing_words_step(LandingWords* w, uint64_t m, uint32_t k) {
    if (!m || m == ~0ull) { /* no word changed, or all eight */
        word_at(w, k, m != 0);
        return;
    }
    for (uint32_t j = 0; j < 8; j++) word_at(w, k + 8 * j, (m >> (8 * j) & 0xFF) != 0);
}
void landing_words_tail(LandingWords* w, const uint8_t* res, const uint8_t* org, uint32_t k, uint32_t size) {
    for (; k < size; k += 8) word_at(w, k, landing_differs(res + k, org + k, size - k < 8 ? size - k : 8));
    word_at(w, size, 0);
}

void landing_note_mask(uint64_t m, uint32_t at, LandingPut note, void* ctx) {
    if (!m) return;
    const unsigned lo = (unsigned)__builtin_ctzll(m);
    note(ctx, at + lo, 64 - (unsigned)__builtin_clzll(m) - lo);
}

int landing_masked_supported(void) {
#if LANDING_X86
    unsigned a, b, c, d, lo, hi;
    if (!__get_cpuid(1, &a, &b, &c, &d) || !(c & bit_OSXSAVE)) return 0;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    (void)hi;
    if ((lo & 0xE6) != 0xE6) return 0; /* XCR0: the OS saves the XMM, YMM, opmask and both ZMM states */
    if (!__get_cpuid_count(7, 0, &a, &b, &c, &d)) return 0;
    return (b & bit_AVX512F) && (b & bit_AVX512BW);
#else
    return 0;
#endif
}

#if LANDING_X86
/* w NULL (landing_masked): no word is told, and the loop is the one without it */
__attribute__((target("avx512f,avx512bw"), always_inline)) static inline uint64_t
masked(const uint8_t* res, const uint8_t* org, uint8_t* dst, uint32_t size, LandingPut put, LandingPut note, void* ctx,
       LandingWords* w) {
    uint64_t total = 0;
    uint32_t k = 0;
    for (; size - k >= 64; k += 64) {
        const __m512i r = _mm512_loadu_si512(res + k), o = _mm512_loadu_si512(org + k);
        __mmask64 m = _mm512_cmpneq_epi8_mask(r, o);
        if (w) landing_words_step(w, m, k); /* before dst's mask: a changed word whatever the guest holds */
        if (!m) continue; /* the guest's bytes are read only in a step the shader changed */
        m &= _mm512_cmpeq_epi8_mask(_mm512_loadu_si512(dst + k), o);
        if (!m) continue;
        _mm512_mask_storeu_epi8(dst + k, m, r); /* the bytes of a clear mask bit are not stored */
        total += (uint64_t)__builtin_popcountll(m);
        if (note) landing_note_mask(m, k, note, ctx);
    }
    if (w) landing_words_tail(w, res, org, k, size);
    Runs r = {put, ctx, 0, 0, 0};
    runs_in(&r, res, org, dst, k, size);
    return total + r.total;
}
__attribute__((target("avx512f,avx512bw"))) uint64_t landing_masked(const uint8_t* res, const uint8_t* org,
                                                                    uint8_t* dst, uint32_t size, LandingPut put,
                                                                    LandingPut note, void* ctx) {
    return masked(res, org, dst, size, put, note, ctx, NULL);
}
__attribute__((target("avx512f,avx512bw"))) uint64_t landing_masked_chg(const uint8_t* res, const uint8_t* org,
                                                                        uint8_t* dst, uint32_t size, LandingPut put,
                                                                        LandingPut note, void* ctx, LandingPut chg,
                                                                        void* chg_ctx) {
    LandingWords w = {chg, chg_ctx, 0, 0};
    return masked(res, org, dst, size, put, note, ctx, &w);
}
#else
uint64_t landing_masked(const uint8_t* res, const uint8_t* org, uint8_t* dst, uint32_t size, LandingPut put,
                        LandingPut note, void* ctx) {
    (void)note; /* every run goes to put */
    return landing_runs(res, org, dst, size, put, ctx);
}
uint64_t landing_masked_chg(const uint8_t* res, const uint8_t* org, uint8_t* dst, uint32_t size, LandingPut put,
                            LandingPut note, void* ctx, LandingPut chg, void* chg_ctx) {
    LandingWords w = {chg, chg_ctx, 0, 0};
    landing_words_tail(&w, res, org, 0, size); /* res and org only: landing_runs below changes dst alone */
    return landing_masked(res, org, dst, size, put, note, ctx);
}
#endif
