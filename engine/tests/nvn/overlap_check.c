/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The overlap check's collector (gpu/nvn/diag/overlap_check_calc.c): the ranges a frame's ops read, each kind, the
   index range of each index type, CALLs followed to the replay's depth, and the ranges the replay writes itself. */
#include "nx/gpu/nvn/diag/overlap_check_calc.h"
#include "nx/gpu/nvn/nvn_params.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static NvnListStore store;
static const NvnOpList* find(void* ctx, uint64_t handle) {
    (void)ctx;
    return nvn_list_find(&store, handle);
}
static void bind(NvnOpList* l, uint32_t kind, uint64_t addr, uint64_t size) {
    const NvnOpBindBuffer op = {kind, 0, 0, 0, addr, size};
    CHECK(nvn_oplist_append(l, NVN_OP_BIND_BUFFER, &op, sizeof op));
}
static const PerfReuseRange* range(const OverlapRanges* r, uint64_t gva, uint32_t len) {
    for (uint32_t i = 0; i < r->set.n; i++)
        if (r->set.r[i].gva == gva && r->set.r[i].len == len) return &r->set.r[i];
    return NULL;
}

static void test_kinds_and_writes(void) {
    static OverlapRanges r;
    NvnOpList l = {0};
    bind(&l, NVN_BIND_UNIFORM, 0x1000, 0x100);
    bind(&l, NVN_BIND_VERTEX, 0x2000, 0x40);
    bind(&l, NVN_BIND_STORAGE, 0x3000, 0x80);
    bind(&l, NVN_BIND_TRANSFORM_FEEDBACK, 0x4000, 0x80); /* not read by the draws, written by the capture */
    bind(&l, NVN_BIND_UNIFORM, 0x5000, 0);               /* a disabled slot */
    bind(&l, NVN_BIND_UNIFORM, 0x1000, 0x100);           /* again: one range */
    const NvnOpDraw arrays = {NVN_DRAW_ARRAYS, 4, 0, 30, 0xf000, 0, 0, 0, 1}; /* an address, but no index range */
    const NvnOpDraw bytes = {NVN_DRAW_ELEMENTS, 4, 0, 30, 0x6000, 0, 0, 0, 1};
    const NvnOpDraw shorts = {NVN_DRAW_ELEMENTS_BASE_VERTEX, 4, 1, 30, 0x7000, 0, 5, 0, 1};
    const NvnOpDraw ints = {NVN_DRAW_ELEMENTS_INSTANCED, 4, 2, 30, 0x8000, 0, 0, 0, 3};
    const NvnOpDraw odd = {NVN_DRAW_ELEMENTS, 4, 3, 30, 0x9000, 0, 0, 0, 1}; /* no such index type */
    const NvnOpDraw none = {NVN_DRAW_ELEMENTS, 4, 1, 30, 0, 0, 0, 0, 1};    /* no index buffer */
    CHECK(nvn_oplist_append(&l, NVN_OP_DRAW, &arrays, sizeof arrays));
    CHECK(nvn_oplist_append(&l, NVN_OP_DRAW, &bytes, sizeof bytes));
    CHECK(nvn_oplist_append(&l, NVN_OP_DRAW, &shorts, sizeof shorts));
    CHECK(nvn_oplist_append(&l, NVN_OP_DRAW, &ints, sizeof ints));
    CHECK(nvn_oplist_append(&l, NVN_OP_DRAW, &odd, sizeof odd));
    CHECK(nvn_oplist_append(&l, NVN_OP_DRAW, &none, sizeof none));
    const NvnOpCopyBuffer copy = {0xa000, 0xb000, 0x20};
    const NvnOpClearBuffer clear = {0xc000, 0x10, 0, 0}, empty = {0xc800, 0, 0, 0};
    const NvnOpReportCounter report = {0, 0, 0xd000};
    const NvnOpReportValue value = {7, 0, 0xe000};
    CHECK(nvn_oplist_append(&l, NVN_OP_COPY_BUFFER, &copy, sizeof copy));
    CHECK(nvn_oplist_append(&l, NVN_OP_CLEAR_BUFFER, &clear, sizeof clear));
    CHECK(nvn_oplist_append(&l, NVN_OP_CLEAR_BUFFER, &empty, sizeof empty)); /* writes nothing */
    CHECK(nvn_oplist_append(&l, NVN_OP_REPORT_COUNTER, &report, sizeof report));
    CHECK(nvn_oplist_append(&l, NVN_OP_REPORT_VALUE, &value, sizeof value));
    uint8_t update[sizeof(NvnOpUpdateUniform) + 8] = {0};
    const NvnOpUpdateUniform head = {0x1000, 1, 0x20, 2, 0};
    memcpy(update, &head, sizeof head);
    CHECK(nvn_oplist_append(&l, NVN_OP_UPDATE_UNIFORM, update, sizeof update));
    overlap_clear(&r);
    overlap_collect(&r, &l, find, NULL, 0);
    CHECK(r.set.n == 6);
    CHECK(range(&r, 0x1000, 0x100) && range(&r, 0x1000, 0x100)->kinds == PERF_REUSE_CB);
    CHECK(range(&r, 0x2000, 0x40) && range(&r, 0x2000, 0x40)->kinds == PERF_REUSE_VERTEX);
    CHECK(range(&r, 0x3000, 0x80) && range(&r, 0x3000, 0x80)->kinds == PERF_REUSE_STORAGE);
    CHECK(range(&r, 0x6000, 30) && range(&r, 0x6000, 30)->kinds == PERF_REUSE_INDEX);
    CHECK(range(&r, 0x7000, 60) && range(&r, 0x8000, 120));
    CHECK(!range(&r, 0x4000, 0x80) && !range(&r, 0x9000, 30) && !range(&r, 0x9000, 240));
    CHECK(!range(&r, 0xf000, 30));
    /* written by the replay: the storage and capture binds, the copy's destination, the clear, both reports and the
       update */
    CHECK(r.nw == 7);
    CHECK(overlap_self_written(&r, 0x3040, 4) && overlap_self_written(&r, 0x407f, 1));
    CHECK(!overlap_self_written(&r, 0x4080, 4));
    CHECK(overlap_self_written(&r, 0xb01c, 8) && !overlap_self_written(&r, 0xa000, 0x20)); /* the source is read */
    CHECK(overlap_self_written(&r, 0xc00f, 1) && !overlap_self_written(&r, 0xc010, 4));
    CHECK(overlap_self_written(&r, 0xd000, 16) && overlap_self_written(&r, 0xe00c, 4));
    CHECK(overlap_self_written(&r, 0x1020, 8) && !overlap_self_written(&r, 0x1000, 0x20)); /* at offset 0x20, 8 bytes */
    CHECK(!overlap_self_written(&r, 0x2000, 0x40));
    nvn_oplist_free(&l);
}

/* lists that call the next one, deeper than the replay follows */
static void test_calls(void) {
    static OverlapRanges r;
    uint64_t handle = 0;
    for (uint32_t depth = 0; depth <= NVN_CALL_DEPTH_MAX + 2; depth++) {
        NvnOpList l = {0};
        bind(&l, NVN_BIND_UNIFORM, 0x10000 * (depth + 1), 0x10);
        if (handle) {
            const NvnOpCall call = {handle};
            CHECK(nvn_oplist_append(&l, NVN_OP_CALL, &call, sizeof call));
        }
        handle = nvn_list_publish(&store, &l);
        CHECK(handle);
    }
    const NvnOpCall missing = {0x12345678}; /* a handle that is not live */
    NvnOpList top = {0};
    CHECK(nvn_oplist_append(&top, NVN_OP_CALL, &missing, sizeof missing));
    const NvnOpCall call = {handle};
    CHECK(nvn_oplist_append(&top, NVN_OP_CALL, &call, sizeof call));
    overlap_clear(&r);
    overlap_collect(&r, &top, find, NULL, 0);
    /* top is depth 0, the lists it reaches are 1 to NVN_CALL_DEPTH_MAX: the newest list first */
    CHECK(r.set.n == NVN_CALL_DEPTH_MAX);
    CHECK(range(&r, 0x10000 * (NVN_CALL_DEPTH_MAX + 3), 0x10));
    CHECK(!range(&r, 0x10000 * 3, 0x10));
    overlap_collect(&r, NULL, find, NULL, 0); /* a list that is gone adds nothing */
    CHECK(r.set.n == NVN_CALL_DEPTH_MAX);
    nvn_oplist_free(&top);
}

static void test_write_overflow(void) {
    static OverlapRanges r;
    NvnOpList l = {0};
    for (uint32_t i = 0; i <= OVERLAP_WRITES; i++) {
        const NvnOpClearBuffer clear = {0x100000 + 0x100ull * i, 0x10, 0, 0};
        CHECK(nvn_oplist_append(&l, NVN_OP_CLEAR_BUFFER, &clear, sizeof clear));
    }
    overlap_clear(&r);
    overlap_collect(&r, &l, find, NULL, 0);
    CHECK(r.nw == OVERLAP_WRITES && r.w_overflow == 1);
    CHECK(overlap_self_written(&r, 0x1, 1)); /* with an overflow any range may be written */
    overlap_clear(&r);
    CHECK(!r.nw && !r.w_overflow && !r.set.n);
    nvn_oplist_free(&l);
}

int main(void) {
    test_kinds_and_writes();
    test_calls();
    test_write_overflow();
    nvn_list_store_free(&store);
    if (failures) return 1;
    printf("overlap_check: ok\n");
    return 0;
}
