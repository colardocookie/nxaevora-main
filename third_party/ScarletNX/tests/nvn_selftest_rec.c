/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The recording Renderer of the NVN selftest (gpu/nvn_selftest_rec.c): the engine's calls land in a record in order,
   the same calls give equal records, a changed argument or a changed order is a fail of the right kind, and calls outside
   a recording are not kept. */
#include "nx/gpu/nvn_selftest_rec.h"
#include <stdio.h>

static int failures;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static int snapshots;
static void snapshot(GpuChannel* ch, NvnStRec* r) {
    (void)ch;
    snapshots++;
    nvn_st_add_state(r, "snap", 5);
}

/* a clear, a draw, a barrier and a fence (in one of two orders), a semaphore and a wait: the calls of a small frame */
static void script(NvnStRec* r, u32 count, int swap, NvnStSnapshotFn snap) {
    const Renderer* g = &nvn_st_renderer;
    nvn_st_renderer_begin(r, snap);
    g->clear(NULL, 0xff);
    g->draw(NULL, 0, 4, 0, count, 0, 0, 1);
    if (swap) {
        g->stream_incr(9);
        g->method_barrier(1);
    } else {
        g->method_barrier(1);
        g->stream_incr(9);
    }
    g->release_write(0x7000, NULL, 16);
    g->before_syncwait();
}

static void test_same_script(void) {
    static NvnStRec a, b;
    NvnStDiff d;
    script(&a, 3, 0, NULL);
    CHECK(nvn_st_renderer_end() == 6);
    script(&b, 3, 0, NULL);
    nvn_st_renderer_end();
    CHECK(nvn_st_compare(&a, &b, &d) == 0);
    CHECK(a.n_state == 10); /* clear 2 entries, draw 8 */
    CHECK(a.n_sync == 5);   /* barrier, fence, semaphore 2, wait */
}

static void test_changed_argument(void) {
    static NvnStRec a, b;
    NvnStDiff d;
    script(&a, 3, 0, NULL);
    nvn_st_renderer_end();
    script(&b, 4, 0, NULL);
    nvn_st_renderer_end();
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_S);
}

static void test_changed_order(void) {
    static NvnStRec a, b;
    NvnStDiff d;
    script(&a, 3, 0, NULL);
    nvn_st_renderer_end();
    script(&b, 3, 1, NULL);
    nvn_st_renderer_end();
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_Y);
}

static void test_snapshot_and_idle(void) {
    static NvnStRec a;
    snapshots = 0;
    script(&a, 3, 0, snapshot);
    nvn_st_renderer_end();
    CHECK(snapshots == 2); /* the clear and the draw */
    CHECK(a.n_state == 12);
    nvn_st_renderer.draw(NULL, 0, 4, 0, 3, 0, 0, 1); /* no recording: nothing is kept, nothing crashes */
    nvn_st_renderer.stream_incr(1);
    CHECK(a.n_state == 12 && a.n_sync == 5);
}

int main(void) {
    test_same_script();
    test_changed_argument();
    test_changed_order();
    test_snapshot_and_idle();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("nvn_selftest_rec: ok\n");
    return 0;
}
