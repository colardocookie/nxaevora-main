/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The submit probe (gpu/nvn/diag/submit_probe.c and its pure part): ops counted through CALLs to the replay's depth,
   the share submitted 1, 2 and 3 ms before the post, a frame posted by a flush counted apart, other queues apart,
   and the ranges a submitted list reads hashed after the submit and before the post, with the frame's own writes
   (transform feedback too) kept apart from the game's. Built with stand-ins for the lock, the clock and the hash. */
#include "nx/gpu/nvn/diag/submit_probe.c"
#include "nx/gpu/nvn/nvn_params.h"
#include <stdio.h>

static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

char g_test_line[4096];
static uint64_t clock_ns;
static int vk_depth, vk_takes, bad_order;
static PMutex lists = PMUTEX_INIT;
uint64_t plat_ns(void) { return clock_ns; }
void pm_lock(PMutex* m) {
    if (m == &g_lk && (lists.held || vk_depth)) bad_order = 1; /* the probe's lock is taken under no other */
    m->held++;
}
void pm_unlock(PMutex* m) { m->held--; }
void vk_lock(void) {
    if (lists.held) bad_order = 1;
    vk_depth++;
    vk_takes++;
}
void vk_unlock(void) { vk_depth--; }
/* the guest bytes: one value a range, by its address */
static uint64_t memory[16];
uint64_t perf_reuse_hash(uint64_t gva, uint64_t len, uint8_t* owned) {
    (void)len;
    *owned = 0;
    return memory[(gva >> 12) & 15];
}

static NvnListStore store;
static const NvnOpList* find(void* ctx, uint64_t handle) {
    (void)ctx;
    return nvn_list_find(&store, handle);
}
static void bind(NvnOpList* l, uint32_t kind, uint64_t addr, uint64_t size) {
    const NvnOpBindBuffer op = {kind, 0, 0, 0, addr, size};
    CHECK(nvn_oplist_append(l, NVN_OP_BIND_BUFFER, &op, sizeof op));
}
static void call(NvnOpList* l, uint64_t handle) {
    const NvnOpCall op = {handle};
    CHECK(nvn_oplist_append(l, NVN_OP_CALL, &op, sizeof op));
}
static uint64_t publish(NvnOpList* l) {
    const uint64_t h = nvn_list_publish(&store, l);
    CHECK(h);
    return h;
}

static void test_ops(void) {
    /* a chain of CALLs deeper than the replay follows: each list has a bind and a call */
    uint64_t next = 0;
    for (unsigned i = 0; i <= NVN_CALL_DEPTH_MAX + 2; i++) {
        NvnOpList l = {0};
        bind(&l, NVN_BIND_UNIFORM, 0x1000, 0x10);
        if (next) call(&l, next);
        next = publish(&l);
    }
    /* the top list at depth 0, then NVN_CALL_DEPTH_MAX more followed: 2 ops each, the last one's call not followed */
    CHECK(submit_list_ops(find(NULL, next), find, NULL, 0) == 2ull * (NVN_CALL_DEPTH_MAX + 1));
    CHECK(submit_list_ops(NULL, find, NULL, 0) == 0);
}

static void test_leads(void) {
    static SubmitFrame f;
    SubmitTotals t = {0};
    CHECK(submit_frame_add(&f, 1000000, 1, 10));  /* 3 ms before the post at 4 ms, exactly */
    CHECK(submit_frame_add(&f, 2000001, 2, 20));  /* just under 2 ms */
    CHECK(submit_frame_add(&f, 3000000, 1, 40));  /* 1 ms exactly */
    CHECK(submit_frame_add(&f, 5000000, 1, 80));  /* after the post: leads by 0 */
    submit_frame_close(&f, 4000000, &t);
    CHECK(t.posts == 1 && t.calls == 4 && t.lists == 5 && t.ops == 150);
    CHECK(t.ops_lead[0] == 70 && t.ops_lead[1] == 10 && t.ops_lead[2] == 10);
    CHECK(t.lead_ns_max == 3000000 && t.lead_ns_sum == 3000000 + 1999999 + 1000000);
    CHECK(f.n == 0);
    for (unsigned i = 0; i < SUBMIT_PROBE_CALLS; i++) CHECK(submit_frame_add(&f, 0, 1, 1));
    CHECK(!submit_frame_add(&f, 0, 1, 1) && f.dropped == 1);
    submit_frame_close(&f, 0, &t);
    CHECK(t.dropped == 1 && f.dropped == 0 && f.n == 0);
}

#define Q 0x7000ull
#define MS 1000000ull
static void submit_at(uint64_t queue, uint64_t ns, uint64_t* handles, uint32_t n) {
    clock_ns = ns;
    const uint64_t flushes = submit_probe_flushes();
    submit_probe_submit(queue, handles, n, ns, flushes, find, NULL, &lists);
}
static void present_at(uint64_t ns) {
    clock_ns = ns;
    submit_probe_present(Q);
}

static void test_runtime(void) {
    g_submit_probe = 1;
    NvnOpList a = {0}, b = {0};
    bind(&a, NVN_BIND_UNIFORM, 0x1000, 0x100);            /* read: the game changes it */
    bind(&a, NVN_BIND_VERTEX, 0x2000, 0x100);             /* read: unchanged */
    bind(&b, NVN_BIND_VERTEX, 0x3000, 0x100);             /* read as vertices and written by the capture */
    bind(&b, NVN_BIND_TRANSFORM_FEEDBACK, 0x3000, 0x100);
    uint64_t ha = publish(&a), hb = publish(&b);
    /* before the first present: not the present queue yet */
    submit_at(Q, 1 * MS, &ha, 1);
    CHECK(g_early_calls == 1 && g_frame.n == 0);
    present_at(2 * MS); /* now Q is the present queue */
    /* a frame: the wait returns at 9 ms, a submits at 10, b at 12, the present at 13.5 */
    clock_ns = 9 * MS;
    submit_probe_wait_return();
    submit_at(Q, 10 * MS, &ha, 1);
    memory[1] = 11; /* the game writes the uniform range after its submit */
    submit_at(Q, 12 * MS, &hb, 1);
    memory[3] = 33; /* a capture lands where b's vertices are read */
    submit_at(0x9000, 12 * MS, &ha, 1); /* another queue */
    present_at(13500000);
    CHECK(g_tot.posts == 2 && g_tot.calls == 2 && g_tot.ops == 4);
    CHECK(g_tot.ops_lead[0] == 4 && g_tot.ops_lead[1] == 2 && g_tot.ops_lead[2] == 2);
    CHECK(g_first_n == 1 && g_first_sum == 1 * MS);
    CHECK(g_other_calls == 1);
    CHECK(g_game_changed[0] == 1 && g_game_changed[1] == 0 && g_self_changed == 1);
    CHECK(g_win.kept == 3 && g_win.changed == 2);
    /* a frame with b alone and no new wait: a's range is not kept from the frame before, no first-submit time */
    submit_at(Q, 15 * MS, &hb, 1);
    memory[1] = 14;
    present_at(16 * MS);
    CHECK(g_tot.calls == 3 && g_game_changed[0] == 1 && g_first_n == 1);
    /* a flush posts a early, then b is submitted: a counted apart, b measured against the present */
    submit_at(Q, 20 * MS, &ha, 1);
    submit_probe_flush_posted(Q);
    memory[1] = 12;
    submit_at(Q, 21 * MS, &hb, 1);
    present_at(22 * MS);
    CHECK(g_flushed.calls == 1 && g_flushed.ops == 2 && g_tot.calls == 4 && g_game_changed[0] == 1);
    /* a call whose own flush posts it: the flush lands between the call's entry and the probe */
    clock_ns = 30 * MS;
    const uint64_t at_entry = submit_probe_flushes();
    submit_probe_flush_posted(Q);
    submit_probe_submit(Q, &ha, 1, 30 * MS, at_entry, find, NULL, &lists);
    CHECK(g_flushed.calls == 2 && g_frame.n == 0);
    memory[1] = 13;
    present_at(31 * MS);
    CHECK(g_game_changed[0] == 1);
    submit_probe_flush_posted(0x9000); /* another queue's flush: nothing */
    CHECK(submit_probe_flushes() == 2);
    /* the window line after 600 presents, and everything reset */
    while (g_win.frames) present_at(clock_ns + MS);
    CHECK(strstr(g_test_line, "perf-submit-probe: 600 presents") != NULL);
    CHECK(g_tot.posts == 0 && g_flushed.calls == 0 && g_game_changed[0] == 0 && g_other_calls == 0);
    CHECK(!bad_order && !g_lk.held && !lists.held && !vk_depth && vk_takes > 0);
}

int main(void) {
    test_ops();
    test_leads();
    test_runtime();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("submit_probe: ok\n");
    return 0;
}
