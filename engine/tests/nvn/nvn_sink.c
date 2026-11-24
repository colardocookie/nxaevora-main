/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* PERF-30 (gpu/nvn/nvn_params.h's NvnWordSink): a builder's words decoded from its array as the replay did, decoded
   by the sink as they are written (SNX_ARCEUS_PERF_REPLAY=2) and taken by the direct helpers and the inline run
   branch (3) make the same calls, in the same order, with the same result and word count. The file includes
   nvn_params.c to reach its static helpers. Checks: the inline branch against nvn_word_stream_next for every mode and
   remaining count, each helper over its argument ranges with data sequences around its count, a helper with a run
   open or after a decode error, and every per-draw builder over random inputs. */
#include "nx/gpu/nvn/nvn_params.c"
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

typedef struct {
    uint32_t sub, method, value;
    int last;
} Call;
#define CALLS 20000u
typedef struct {
    Call c[CALLS];
    uint32_t n;
    int result;    /* replay_bind_send's: 1, 0 refused, -1 the builder wrote nothing */
    uint32_t words; /* the builder's count */
} Rec;
static Rec rec[3];
static Rec* cur;
static void note(void* ctx, uint32_t sub, uint32_t method, uint32_t value, int last) {
    (void)ctx;
    if (cur->n < CALLS) cur->c[cur->n] = (Call){sub, method, value, last};
    cur->n++;
}
/* the replay's array path: replay_bind_words */
static int decode(const uint32_t* words, uint32_t count) {
    NvnWordStream s = {0};
    for (uint32_t i = 0; i < count; i++) {
        uint32_t method, value;
        int last;
        const int r = nvn_word_stream_next(&s, words[i], &method, &value, &last);
        if (r < 0) return 0;
        if (r == 1) note(NULL, s.subchannel, method, value, last);
    }
    return nvn_word_stream_complete(&s);
}
static NvnWordSink sink;
/* the three paths of one build: 0 the array, 1 the sink, 2 the sink with its direct forms */
typedef uint32_t (*Build)(const void* a, uint32_t* out);
static void three(Build build, const void* a) {
    static uint32_t out[0x2100]; /* the longest data sequence, 0x2002 words, and its header */
    for (int p = 0; p < 3; p++) {
        cur = &rec[p];
        cur->n = 0;
        if (p) sink = (NvnWordSink){note, NULL, {0}, 0, p == 2}, nvn_word_sink = &sink;
        cur->words = build(a, out);
        nvn_word_sink = NULL;
        if (!p)
            cur->result = cur->words ? decode(out, cur->words) : -1;
        else
            cur->result = cur->words ? nvn_word_sink_take(&sink) : (nvn_word_sink_take(&sink), -1);
    }
    for (int p = 1; p < 3; p++) {
        CHECK(rec[p].words == rec[0].words && rec[p].result == rec[0].result && rec[p].n == rec[0].n);
        CHECK(!memcmp(rec[p].c, rec[0].c, (rec[0].n < CALLS ? rec[0].n : CALLS) * sizeof(Call)));
    }
}

static uint32_t rnd_state = 7;
static uint32_t rnd(void) {
    rnd_state = rnd_state * 1103515245u + 12345u;
    return (rnd_state >> 8) ^ (rnd_state << 13);
}

/* the inline branch against the decoder's remaining branch: the yield and the whole state after, every mode, every
   remaining count up to the 13-bit bound, and mode 5 across its switch to mode 3 for runs up to 64 words */
static Call one;
static void keep(void* ctx, uint32_t sub, uint32_t method, uint32_t value, int last) {
    (void)ctx;
    one = (Call){sub, method, value, last};
}
static void test_inline(void) {
    static const uint32_t modes[] = {NVN_WORD_MODE_INCREASING, NVN_WORD_MODE_NON_INCREASING,
                                     NVN_WORD_MODE_INCREASE_ONCE};
    for (uint32_t m = 0; m < 3; m++)
        for (uint32_t remaining = 1; remaining <= NVN_WORD_COUNT_MASK; remaining++) {
            const uint32_t words = remaining <= 64 ? remaining : 2;
            NvnWordStream s = {modes[m], rnd() & NVN_WORD_METHOD_MASK, remaining, rnd() & 7};
            NvnWordSink k = {keep, NULL, s, 0, 1};
            for (uint32_t i = 0; i < words; i++) {
                const uint32_t word = rnd();
                uint32_t method, value;
                int last;
                CHECK(nvn_word_stream_next(&s, word, &method, &value, &last) == 1);
                sink_data(&k, word);
                CHECK(one.sub == s.subchannel && one.method == method && one.value == value && one.last == last);
                CHECK(k.s.mode == s.mode && k.s.method == s.method && k.s.remaining == s.remaining &&
                      k.s.subchannel == s.subchannel);
            }
        }
}

/* each helper, its arguments in and out of its header's fields, then data words around its count */
typedef struct {
    int helper; /* 0 run, 1 fixed, 2 value, 3 macro, 4 macro_first, 5 a run open before a run, 6 a bad word before a
                   run, 7 a run open before a value */
    uint32_t method, count, data;
} HelperArgs;
static uint32_t helper_build(const void* p, uint32_t* out) {
    const HelperArgs* a = p;
    NvnBindWriter w = {out, 0};
    if (a->helper == 5 || a->helper == 7) bind_run(&w, 0x123, 3), bind_word(&w, 1);
    if (a->helper == 6) bind_word(&w, 0x00000001u); /* mode 0: the decoder refuses it */
    switch (a->helper) {
    case 0: case 5: case 6: bind_run(&w, a->method, a->count); break;
    case 1: bind_fixed(&w, a->method, a->count); break;
    case 2: case 7: bind_value(&w, a->method, a->count); break;
    case 3: bind_macro(&w, a->method, a->count); break;
    case 4: bind_macro_first(&w, a->method, a->count, 0xABCD0000u); break;
    }
    for (uint32_t i = 0; i < a->data; i++) bind_word(&w, 0x5000u + i);
    return w.at;
}
static void test_helpers(void) {
    static const uint32_t methods[] = {0, 1, 0x40, 0x7FF, 0xE00, 0xE20, 0xFFF, 0x1000, 0x1001, 0x1FFF, 0x2000, 0xFFFF};
    static const uint32_t counts[] = {0, 1, 2, 3, 0x1FFE, 0x1FFF, 0x2000, 0x2001, 0x3FFF, 0xFFFFFFFFu};
    uint32_t cases = 0;
    for (int h = 0; h < 8; h++)
        for (uint32_t mi = 0; mi < sizeof methods / sizeof *methods; mi++)
            for (uint32_t ci = 0; ci < sizeof counts / sizeof *counts; ci++) {
                const uint32_t c = counts[ci] > 0x2001 ? 3 : counts[ci]; /* data around it, the long ones capped */
                const uint32_t datas[] = {0, 1, c ? c - 1 : 0, c, c + 1};
                for (uint32_t di = 0; di < 5; di++) {
                    const HelperArgs a = {h, methods[mi], counts[ci], datas[di]};
                    three(helper_build, &a);
                    cases++;
                }
            }
    CHECK(cases == 8 * 12 * 10 * 5);
}

/* every per-draw builder of replay_bind_send_all and the draw, random inputs */
typedef struct {
    int which;
    uint32_t v[8];
    uint32_t s[16], f[16];
    uint64_t h[8];
} BuilderArgs;
static uint32_t builder_build(const void* p, uint32_t* out) {
    const BuilderArgs* a = p;
    int uses = 0;
    switch (a->which) {
    case 0: return nvn_bind_color_words(a->v[0], out);
    case 1: return nvn_bind_channel_mask_words(a->v[0], out);
    case 2: return nvn_bind_depth_stencil_words(a->v[0], a->v[1], out);
    case 3: return nvn_bind_polygon_words(a->v[0], out);
    case 4: return nvn_bind_blend_words(a->v[0], a->v[1], out);
    case 5: return nvn_patch_size_words(a->v[0] & 0x3F, out);
    case 6: return nvn_inner_tessellation_levels_words(a->v, out);
    case 7: return nvn_outer_tessellation_levels_words(a->v, out);
    case 8: return nvn_primitive_restart_words(a->v[0] & 1, a->v[1], out);
    case 9: return nvn_render_enable_words(a->v[0] & 3, out);
    case 10: return nvn_render_enable_conditional_words(a->v[0] % 3, a->h[0], out);
    case 11: return nvn_bind_vertex_attrib_words(a->v[0] % 17, a->s, a->f, out);
    case 12: return nvn_bind_vertex_stream_words(a->v[0] % 17, a->s, a->f, out);
    case 13: return nvn_bind_vertex_buffer_words(a->v[0] % 34, a->h[0], a->h[1] >> (a->v[1] % 64), out);
    case 14: return nvn_bind_uniform_buffer_words(a->v[0] % 7, a->v[1] % 20, a->h[0], a->v[2] >> (a->v[3] % 32), out);
    case 15: return nvn_bind_storage_buffer_words(a->v[0] % 7, a->v[1] % 20, a->h[0], a->v[2] >> (a->v[3] % 32), out);
    case 16: return nvn_bind_storage_buffers_words(a->v[0] % 7, a->v[1] % 20, 1 + a->v[2] % 3, a->h, out);
    case 17: return nvn_bind_texture_words(a->v[0] % 7, a->v[1] % 40, a->h[0], (int)(a->v[2] & 1), out);
    case 18: return nvn_bind_textures_words(a->v[0] % 7, a->v[1] % 40, 1 + a->v[2] % 4, a->h, (int)(a->v[3] & 1), out);
    default: /* the draw, every kind and some out of range */
        return nvn_draw_words(a->v[0] % 7, a->v[1] % 16, a->v[2] % 5, a->v[3], a->h[0], a->v[4],
                              a->v[5] % 3 ? a->v[5] : 0, a->v[6] % 3 ? a->v[6] : 0, a->v[7] % 4, out, &uses);
    }
}
static void test_builders(void) {
    uint32_t built = 0;
    for (int which = 0; which < 20; which++)
        for (int round = 0; round < 3000; round++) {
            BuilderArgs a = {which, {0}, {0}, {0}, {0}};
            for (int i = 0; i < 8; i++) a.v[i] = round % 5 ? rnd() : rnd() % 3, a.h[i] = (uint64_t)rnd() << 32 | rnd();
            for (int i = 0; i < 16; i++) a.s[i] = rnd(), a.f[i] = rnd() % 5;
            three(builder_build, &a);
            built += rec[0].words != 0;
        }
    CHECK(built > 20 * 3000 / 2);
}

int main(void) {
    test_inline();
    test_helpers();
    test_builders();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("nvn_sink: ok\n");
    return 0;
}
