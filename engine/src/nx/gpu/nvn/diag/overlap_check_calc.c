/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn/diag/overlap_check_calc.h"
#include "nx/gpu/nvn/nvn_params.h"
#include <string.h>

void overlap_clear(OverlapRanges* r) {
    perf_reuse_clear(&r->set);
    r->nw = 0;
    r->w_overflow = 0;
}

static void write_add(OverlapRanges* r, uint64_t gva, uint64_t len) {
    if (!gva || !len) return;
    if (r->nw == OVERLAP_WRITES) {
        r->w_overflow++;
        return;
    }
    r->w[r->nw++] = (OverlapWrite){gva, len};
}

void overlap_collect(OverlapRanges* r, const NvnOpList* list, OverlapFind find, void* ctx, uint32_t depth) {
    size_t pos = 0;
    NvnOpHeader header;
    const void* payload;
    while (list && nvn_oplist_next(list, &pos, &header, &payload)) {
        switch (header.code) {
        case NVN_OP_BIND_BUFFER: {
            const NvnOpBindBuffer* op = payload;
            if (!op->addr || !op->size) break;
            if (op->kind == NVN_BIND_UNIFORM) perf_reuse_add(&r->set, op->addr, op->size, PERF_REUSE_CB);
            if (op->kind == NVN_BIND_VERTEX) perf_reuse_add(&r->set, op->addr, op->size, PERF_REUSE_VERTEX);
            if (op->kind == NVN_BIND_STORAGE) {
                perf_reuse_add(&r->set, op->addr, op->size, PERF_REUSE_STORAGE);
                write_add(r, op->addr, op->size); /* a shader may write it */
            }
            break;
        }
        case NVN_OP_DRAW: {
            const NvnOpDraw* op = payload;
            /* NVN's index types in order: 8, 16 and 32 bits */
            const int indexed = op->kind >= NVN_DRAW_ELEMENTS && op->kind <= NVN_DRAW_ELEMENTS_INSTANCED;
            if (indexed && op->type <= 2 && op->address)
                perf_reuse_add(&r->set, op->address, (uint64_t)op->count << op->type, PERF_REUSE_INDEX);
            break;
        }
        case NVN_OP_UPDATE_UNIFORM: {
            const NvnOpUpdateUniform* op = payload;
            write_add(r, op->address + op->offset, (uint64_t)op->words * 4u);
            break;
        }
        case NVN_OP_COPY_BUFFER: {
            const NvnOpCopyBuffer* op = payload;
            write_add(r, op->dst, op->size);
            break;
        }
        case NVN_OP_CLEAR_BUFFER: {
            const NvnOpClearBuffer* op = payload;
            write_add(r, op->dst, op->size);
            break;
        }
        case NVN_OP_REPORT_COUNTER: {
            const NvnOpReportCounter* op = payload;
            write_add(r, op->address, 16);
            break;
        }
        case NVN_OP_REPORT_VALUE: {
            const NvnOpReportValue* op = payload;
            write_add(r, op->address, 16);
            break;
        }
        case NVN_OP_CALL: {
            const NvnOpCall* op = payload;
            if (depth < NVN_CALL_DEPTH_MAX) overlap_collect(r, find(ctx, op->handle), find, ctx, depth + 1);
            break;
        }
        default: break;
        }
    }
}

int overlap_self_written(const OverlapRanges* r, uint64_t gva, uint64_t len) {
    if (r->w_overflow) return 1;
    for (uint32_t i = 0; i < r->nw; i++)
        if (gva < r->w[i].gva + r->w[i].len && r->w[i].gva < gva + len) return 1;
    return 0;
}
