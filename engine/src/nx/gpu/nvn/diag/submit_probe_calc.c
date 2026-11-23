/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn/diag/submit_probe_calc.h"
#include "nx/gpu/nvn/nvn_params.h"

uint64_t submit_list_ops(const NvnOpList* list, OverlapFind find, void* ctx, uint32_t depth) {
    if (!list) return 0;
    uint64_t ops = list->ops;
    size_t pos = 0;
    NvnOpHeader header;
    const void* payload;
    while (depth < NVN_CALL_DEPTH_MAX && nvn_oplist_next(list, &pos, &header, &payload))
        if (header.code == NVN_OP_CALL)
            ops += submit_list_ops(find(ctx, ((const NvnOpCall*)payload)->handle), find, ctx, depth + 1);
    return ops;
}

int submit_frame_add(SubmitFrame* f, uint64_t ns, uint32_t lists, uint32_t ops) {
    if (f->n == SUBMIT_PROBE_CALLS) {
        f->dropped++;
        return 0;
    }
    f->call[f->n++] = (SubmitCall){ns, lists, ops};
    return 1;
}

void submit_frame_close(SubmitFrame* f, uint64_t post_ns, SubmitTotals* t) {
    t->posts++;
    t->dropped += f->dropped;
    for (uint32_t i = 0; i < f->n; i++) {
        const SubmitCall* c = &f->call[i];
        const uint64_t lead = post_ns > c->ns ? post_ns - c->ns : 0;
        t->calls++;
        t->lists += c->lists;
        t->ops += c->ops;
        t->lead_ns_sum += lead;
        if (lead > t->lead_ns_max) t->lead_ns_max = lead;
        for (unsigned k = 0; k < SUBMIT_PROBE_LEADS; k++)
            if (lead >= (k + 1) * 1000000ull) t->ops_lead[k] += c->ops;
    }
    f->n = 0;
    f->dropped = 0;
}
