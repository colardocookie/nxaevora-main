/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_NVN_BLEND_TARGETS (gpu/nvn/nvn_blend_slots.h): the methods a draw sends after blend binds to two targets,
   decoded from nvn_bind_blend_words as the replay sends them. Both targets' factor runs in bind order, the methods
   every bind writes ending with the last bind's values, one run for two binds of one target, and the count of binds a
   single slot would have replaced. The single slot of today's path sends only the last bind's words */
#include "nx/gpu/nvn/nvn_blend_slots.h"
#include "nx/gpu/nvn/nvn_params.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

#define FLAGS 0xD35u
#define FUNC0 0x781u
#define FUNC1 0x789u
/* target 0, colour SRC_ALPHA, ONE_MINUS_SRC_ALPHA for both, blend flags 3 */
#define T0_W0 0x110C0000u
#define T0_W1 0x06050605u
/* target 1, colour ONE, ZERO for both, blend flags 4 */
#define T1_W0 0x11010001u
#define T1_W1 0x01020102u

typedef struct {
    uint32_t method[64], value[64], n;
} Sent;
static void send_words(Sent* out, uint32_t w0, uint32_t w1) {
    uint32_t words[NVN_BIND_BLEND_WORDS_MAX];
    uint32_t count = nvn_bind_blend_words(w0, w1, words);
    NvnWordStream stream = {0};
    CHECK(count > 0);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t m, v;
        int last;
        if (nvn_word_stream_next(&stream, words[i], &m, &v, &last) == 1 && out->n < 64) {
            out->method[out->n] = m;
            out->value[out->n++] = v;
        }
    }
    CHECK(nvn_word_stream_complete(&stream));
}
/* the draw: every slot the replay takes, as replay_bind_send_blend_slots sends them */
static void draw(NvnBlendSlots* s, Sent* out) {
    uint32_t target;
    memset(out, 0, sizeof *out);
    while (nvn_blend_slots_take(s, &target)) send_words(out, s->slot[target].w[0], s->slot[target].w[1]);
}
static int first(const Sent* s, uint32_t method) {
    for (uint32_t i = 0; i < s->n; i++)
        if (s->method[i] == method) return (int)i;
    return -1;
}
static int count(const Sent* s, uint32_t method) {
    int n = 0;
    for (uint32_t i = 0; i < s->n; i++) n += s->method[i] == method;
    return n;
}
static uint32_t last_value(const Sent* s, uint32_t method) {
    uint32_t v = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < s->n; i++)
        if (s->method[i] == method) v = s->value[i];
    return v;
}

int main(void) {
    NvnBlendSlots s;
    Sent sent, one;
    /* the shared flags of each bind alone */
    memset(&one, 0, sizeof one);
    send_words(&one, T0_W0, T0_W1);
    const uint32_t flags0 = last_value(&one, FLAGS);
    CHECK(first(&one, FUNC0) >= 0 && first(&one, FUNC1) < 0 && last_value(&one, FUNC0 + 1u) == 5u);
    memset(&one, 0, sizeof one);
    send_words(&one, T1_W0, T1_W1);
    const uint32_t flags1 = last_value(&one, FLAGS);
    /* this is also today's single slot after binds to target 0 then 1: only target 1's run is sent */
    CHECK(flags0 != flags1 && first(&one, FUNC1) >= 0 && count(&one, FUNC0) == 0);

    /* target 0 then 1: both runs, 0x781 first, the shared methods end with target 1's, one bind replaced */
    memset(&s, 0, sizeof s);
    nvn_blend_slots_store(&s, T0_W0, T0_W1);
    nvn_blend_slots_store(&s, T1_W0, T1_W1);
    draw(&s, &sent);
    CHECK(first(&sent, FUNC0) >= 0 && first(&sent, FUNC1) > first(&sent, FUNC0));
    CHECK(last_value(&sent, FUNC0 + 1u) == 5u && last_value(&sent, FUNC1 + 1u) == 2u);
    CHECK(last_value(&sent, FLAGS) == flags1 && count(&sent, FLAGS) == 2 && count(&sent, 0x374u) == 2);
    CHECK(count(&sent, 0x4B9u) == 2 && last_value(&sent, 0x4B9u) == 1u && last_value(&sent, 0x374u) == 0u);
    CHECK(s.replaced == 1 && s.binds[0] == 1 && s.binds[1] == 1);
    /* the slots are clean after the draw: a second draw sends nothing */
    draw(&s, &sent);
    CHECK(sent.n == 0);

    /* target 1 then 0: 0x789 first, the shared methods end with target 0's (sending by target number fails here) */
    memset(&s, 0, sizeof s);
    nvn_blend_slots_store(&s, T1_W0, T1_W1);
    nvn_blend_slots_store(&s, T0_W0, T0_W1);
    draw(&s, &sent);
    CHECK(first(&sent, FUNC1) >= 0 && first(&sent, FUNC0) > first(&sent, FUNC1));
    CHECK(last_value(&sent, FLAGS) == flags0 && s.replaced == 1);

    /* two binds of one target: only the second is sent, and nothing was replaced */
    memset(&s, 0, sizeof s);
    nvn_blend_slots_store(&s, T1_W0 & ~7u, T1_W1);
    nvn_blend_slots_store(&s, T0_W0, T0_W1);
    draw(&s, &sent);
    CHECK(count(&sent, FUNC0) == 1 && count(&sent, FLAGS) == 1 && last_value(&sent, FUNC0 + 1u) == 5u);
    CHECK(last_value(&sent, FLAGS) == flags0 && s.replaced == 0 && s.binds[0] == 2);

    /* 0, 1, then 0 again: target 0 is the newest, so 1 goes first and the shared methods end with target 0's */
    memset(&s, 0, sizeof s);
    nvn_blend_slots_store(&s, T0_W0, T0_W1);
    nvn_blend_slots_store(&s, T1_W0, T1_W1);
    nvn_blend_slots_store(&s, T0_W0, T0_W1);
    draw(&s, &sent);
    CHECK(first(&sent, FUNC0) > first(&sent, FUNC1) && count(&sent, FUNC0) == 1 && last_value(&sent, FLAGS) == flags0);
    CHECK(s.replaced == 2);

    /* a draw between two binds: the second is not a replaced one, and target 7 has its own slot */
    memset(&s, 0, sizeof s);
    nvn_blend_slots_store(&s, T0_W0, T0_W1);
    draw(&s, &sent);
    nvn_blend_slots_store(&s, T0_W0 | 7u, T0_W1);
    draw(&s, &sent);
    CHECK(s.replaced == 0 && s.binds[7] == 1 && first(&sent, FUNC0 + 7u * 8u) >= 0 && count(&sent, FUNC0) == 0);
    printf("%s nvn_blend_slots\n", failures ? "FAIL" : "PASS");
    return failures != 0;
}
