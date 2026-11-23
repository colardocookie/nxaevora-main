/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_PERF_SCAN (PERF-33 part 2, gpu/nvn/nvn_cmdlist.h): a list's marks are the offsets of its FENCE_SYNC,
   WAIT_SYNC and CALL ops in list order, taken at each append, also by a copy, moved by publish and freed with the
   list. With the marks off nothing is kept. A mark that cannot grow loses the list's marks and keeps the op. The
   submit's traversal at the marks, following each CALL target's own state, visits what the whole walk visits, in
   the same order. The file includes nvn_cmdlist.c to fail its allocations. */
#include <stdio.h>
#include <stdlib.h>
static int fail_realloc; /* the next realloc fails */
static void* test_realloc(void* p, size_t n) {
    if (fail_realloc) {
        fail_realloc = 0;
        return NULL;
    }
    return realloc(p, n);
}
#define realloc test_realloc
#include "nx/gpu/nvn/nvn_cmdlist.c"
#undef realloc

static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static uint32_t rnd_state = 11;
static uint32_t rnd(void) {
    rnd_state = rnd_state * 1103515245u + 12345u;
    return rnd_state >> 8;
}
static int marked_code(uint32_t code) {
    return code == NVN_OP_FENCE_SYNC || code == NVN_OP_WAIT_SYNC || code == NVN_OP_CALL;
}
/* the offsets of the marked codes by a walk of the whole list */
static uint32_t walk(const NvnOpList* l, uint32_t* out, uint32_t cap) {
    size_t pos = 0, at = 0;
    NvnOpHeader h;
    const void* payload;
    uint32_t n = 0;
    while (at = pos, nvn_oplist_next(l, &pos, &h, &payload))
        if (marked_code(h.code) && n < cap) out[n++] = (uint32_t)at;
    return n;
}
static int marks_equal_walk(const NvnOpList* l) {
    static uint32_t want[4096];
    const uint32_t n = walk(l, want, 4096);
    return l->nmarks == n && (!n || !memcmp(l->marks, want, n * sizeof *want));
}
/* one random op: about a third of them marked codes, the CALLs to calls[] when given */
static int append_random(NvnOpList* l, const uint64_t* calls, uint32_t ncalls) {
    const uint32_t r = rnd() % 9;
    if (r == 0) {
        NvnOpFenceSync op = {rnd(), rnd(), 0, rnd() & 1};
        return nvn_oplist_append(l, NVN_OP_FENCE_SYNC, &op, sizeof op);
    }
    if (r == 1) {
        NvnOpWaitSync op = {rnd(), rnd(), 0};
        return nvn_oplist_append(l, NVN_OP_WAIT_SYNC, &op, sizeof op);
    }
    if (r == 2) {
        NvnOpCall op = {ncalls ? calls[rnd() % ncalls] : rnd()};
        return nvn_oplist_append(l, NVN_OP_CALL, &op, sizeof op);
    }
    uint8_t payload[64];
    for (size_t i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)rnd();
    return nvn_oplist_append(l, r < 6 ? NVN_OP_BARRIER : NVN_OP_SET_VIEWPORT, payload, rnd() % 65);
}

static void test_random_lists(void) {
    g_nvn_oplist_marks = 1;
    for (int round = 0; round < 500; round++) {
        NvnOpList l = {0};
        const int n = (int)(rnd() % 400);
        for (int i = 0; i < n; i++) CHECK(append_random(&l, NULL, 0));
        CHECK(marks_equal_walk(&l) && nvn_oplist_marked(&l) && !l.marks_lost);
        nvn_oplist_free(&l);
        CHECK(!l.marks && !l.nmarks && !l.marks_cap && !l.marks_lost);
    }
}
/* the switch at 0: the same ops and bytes, and no marks at all */
static void test_off(void) {
    NvnOpList on = {0}, off = {0};
    const uint32_t seed = rnd_state;
    g_nvn_oplist_marks = 1;
    for (int i = 0; i < 300; i++) CHECK(append_random(&on, NULL, 0));
    rnd_state = seed;
    g_nvn_oplist_marks = 0;
    for (int i = 0; i < 300; i++) CHECK(append_random(&off, NULL, 0));
    CHECK(on.used == off.used && on.ops == off.ops && !memcmp(on.data, off.data, on.used));
    CHECK(!off.marks && !off.nmarks && !off.marks_cap && !nvn_oplist_marked(&off) && on.nmarks > 0);
    nvn_oplist_free(&on);
    nvn_oplist_free(&off);
}
/* the marks cannot grow: the op is stored, the list's marks are lost, and no growth is tried again */
static void test_lost(void) {
    g_nvn_oplist_marks = 1;
    NvnOpList l = {0};
    for (int i = 0; i < 16; i++) CHECK(nvn_oplist_append(&l, NVN_OP_FENCE_SYNC, &(NvnOpFenceSync){0}, 24));
    CHECK(nvn_oplist_append(&l, NVN_OP_BARRIER, &l, 0)); /* unmarked: grows the data, so only the marks need room */
    CHECK(l.nmarks == 16 && l.marks_cap == 16 && l.cap >= l.used + 64); /* full marks, room in the data */
    const size_t used = l.used;
    fail_realloc = 1;
    CHECK(nvn_oplist_append(&l, NVN_OP_CALL, &(NvnOpCall){7}, sizeof(NvnOpCall)));
    CHECK(!fail_realloc && l.ops == 18 && l.used > used && l.marks_lost && !nvn_oplist_marked(&l));
    fail_realloc = 1; /* lost: the next marked op tries no growth */
    CHECK(nvn_oplist_append(&l, NVN_OP_WAIT_SYNC, &(NvnOpWaitSync){0}, sizeof(NvnOpWaitSync)));
    CHECK(fail_realloc && l.nmarks == 16);
    fail_realloc = 0;
    nvn_oplist_free(&l);
}
/* publish moves the marks with the list and empties the source; a copy marks its own copies */
static void test_publish_copy(void) {
    g_nvn_oplist_marks = 1;
    NvnListStore store = {0};
    NvnOpList a = {0};
    for (int i = 0; i < 200; i++) CHECK(append_random(&a, NULL, 0));
    const uint32_t* marks = a.marks;
    const uint32_t n = a.nmarks;
    const uint64_t ha = nvn_list_publish(&store, &a);
    const NvnOpList* pa = nvn_list_find(&store, ha);
    CHECK(ha && pa && pa->marks == marks && pa->nmarks == n && !a.marks && !a.nmarks && marks_equal_walk(pa));
    NvnOpList b = {0};
    uint64_t failed = 0;
    for (int i = 0; i < 10; i++) CHECK(append_random(&b, NULL, 0));
    NvnOpList calls = {0}; /* a copy whose CALLs name a live list */
    for (int i = 0; i < 40; i++) CHECK(append_random(&calls, &ha, 1));
    CHECK(nvn_oplist_copy(&b, &calls, &store, &failed) && marks_equal_walk(&b));
    nvn_oplist_free(&calls);
    nvn_oplist_free(&b);
    nvn_list_unref(&store, ha);
    nvn_list_store_free(&store);
}

/* The submit's traversal: (list, offset) of each marked op, each CALL followed into its target at once, to the
   replay's depth. by_marks reads a list at its marks when it has them all, as queue_scan_list does */
typedef struct {
    uint64_t list;
    uint32_t pos, code;
} Seen;
static Seen seen[1 << 16];
static uint32_t nseen;
static void visit(const NvnListStore* s, uint64_t handle, uint32_t depth, int by_marks) {
    const NvnOpList* l = nvn_list_find(s, handle);
    if (!l) return;
    size_t pos = 0;
    NvnOpHeader h;
    const void* payload;
    uint32_t i = 0;
    for (;;) {
        size_t at;
        if (by_marks && nvn_oplist_marked(l)) {
            if (i == l->nmarks) break;
            pos = at = l->marks[i++];
            if (!nvn_oplist_next(l, &pos, &h, &payload)) break;
        } else {
            at = pos;
            if (!nvn_oplist_next(l, &pos, &h, &payload)) break;
            if (!marked_code(h.code)) continue;
        }
        if (nseen < sizeof seen / sizeof *seen) seen[nseen++] = (Seen){handle, (uint32_t)at, h.code};
        if (h.code == NVN_OP_CALL && depth < NVN_CALL_DEPTH_MAX) {
            NvnOpCall call;
            memcpy(&call, payload, sizeof call);
            visit(s, call.handle, depth + 1, by_marks);
        }
    }
}
static void test_traversal(void) {
    g_nvn_oplist_marks = 1;
    for (int round = 0; round < 60; round++) {
        NvnListStore store = {0};
        uint64_t h[24];
        uint32_t nh = 0;
        for (int k = 0; k < 24; k++) { /* each list calls earlier ones (and a dead handle), some lose their marks */
            NvnOpList l = {0};
            const int n = (int)(rnd() % 120);
            for (int i = 0; i < n; i++) CHECK(append_random(&l, h, nh));
            if (rnd() % 5 == 0) l.marks_lost = 1;
            h[nh++] = nvn_list_publish(&store, &l);
        }
        for (uint32_t k = 0; k < nh; k++) {
            static Seen whole[1 << 16];
            nseen = 0;
            visit(&store, h[k], 0, 0);
            const uint32_t n = nseen;
            memcpy(whole, seen, n * sizeof *seen);
            nseen = 0;
            visit(&store, h[k], 0, 1);
            CHECK(nseen == n && !memcmp(whole, seen, n * sizeof *seen));
        }
        for (uint32_t k = 0; k < nh; k++) nvn_list_unref(&store, h[k]);
        nvn_list_store_free(&store);
    }
}

/* PERF-34 bit 3: freeing a list through its marks gives up the references of the same CALLs as the whole walk. Two
   identical stores, one freed each way, keep the same lists alive at each step, lost lists walked whole */
static void build_store(NvnListStore* s, uint64_t* h, uint32_t n, uint32_t seed) {
    rnd_state = seed;
    for (uint32_t k = 0; k < n; k++) {
        NvnOpList l = {0};
        const int ops = (int)(rnd() % 80);
        for (int i = 0; i < ops; i++) CHECK(append_random(&l, h, k));
        if (rnd() % 5 == 0) l.marks_lost = 1;
        h[k] = nvn_list_publish(s, &l);
        for (uint32_t j = 0; j < k; j++) /* calls take references: each earlier list called is kept by this one */
            if (rnd() % 3 == 0) CHECK(nvn_list_ref(s, h[j]));
    }
}
static void test_unref_marks(void) {
    g_nvn_oplist_marks = 1;
    for (int round = 0; round < 40; round++) {
        NvnListStore a = {0}, b = {0};
        uint64_t ha[20], hb[20];
        const uint32_t seed = 1000u + (uint32_t)round;
        build_store(&a, ha, 20, seed);
        build_store(&b, hb, 20, seed);
        for (uint32_t k = 20; k-- > 0;) { /* the newest first, as frames let their lists go */
            g_nvn_unref_marks = 0;
            nvn_list_unref(&a, ha[k]);
            g_nvn_unref_marks = 1;
            nvn_list_unref(&b, hb[k]);
            CHECK(nvn_list_live(&a) == nvn_list_live(&b));
        }
        g_nvn_unref_marks = 0;
        for (uint32_t k = 0; k < 20; k++) { /* the references the calls took, gone the same way */
            nvn_list_unref(&a, ha[k]);
            g_nvn_unref_marks = 1;
            nvn_list_unref(&b, hb[k]);
            g_nvn_unref_marks = 0;
            CHECK(nvn_list_live(&a) == nvn_list_live(&b));
        }
        nvn_list_store_free(&a);
        nvn_list_store_free(&b);
    }
}

int main(void) {
    test_random_lists();
    test_unref_marks();
    test_off();
    test_lost();
    test_publish_copy();
    test_traversal();
    g_nvn_oplist_marks = 0;
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("nvn_marks: ok\n");
    return 0;
}
