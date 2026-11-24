/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_NVN_BLEND_TARGETS: a blend state object belongs to one colour target, and a game drawing into
   several targets binds one per target before a draw. The replay keeps a slot per target and the next draw sends every
   slot bound since the last one, in bind order, so the methods every bind writes end with the last bind's values as
   on the console. No runtime header, so the host test links it alone. */
#ifndef SNX_GPU_NVN_BLEND_SLOTS_H
#define SNX_GPU_NVN_BLEND_SLOTS_H
#include <stdint.h>

#define NVN_BLEND_SLOT_COUNT 8u /* the targets a blend state names, bits 0-2 of its first word */

typedef struct {
    uint32_t w[2];  /* the state's two words */
    uint32_t order; /* the bind's number in the frame job, the lowest sent first */
    uint8_t dirty;  /* bound since the last draw */
} NvnBlendSlot;
typedef struct {
    NvnBlendSlot slot[NVN_BLEND_SLOT_COUNT];
    uint32_t next_order;
    uint32_t binds[NVN_BLEND_SLOT_COUNT]; /* the binds of each target */
    uint32_t replaced;                    /* binds stored while another target's slot was dirty */
} NvnBlendSlots;

/* stores a bind in its target's slot as the newest. A bind while another target waits for the draw is one that a
   single slot would have replaced, and is counted */
static inline void nvn_blend_slots_store(NvnBlendSlots* s, uint32_t w0, uint32_t w1) {
    uint32_t target = w0 & (NVN_BLEND_SLOT_COUNT - 1u);
    for (uint32_t i = 0; i < NVN_BLEND_SLOT_COUNT; i++)
        if (i != target && s->slot[i].dirty) {
            s->replaced++;
            break;
        }
    NvnBlendSlot* slot = &s->slot[target];
    slot->w[0] = w0;
    slot->w[1] = w1;
    slot->order = ++s->next_order;
    slot->dirty = 1;
    s->binds[target]++;
}
/* The dirty slot bound first: cleared, its target in *target, and 1. 0 when none is left */
static inline int nvn_blend_slots_take(NvnBlendSlots* s, uint32_t* target) {
    int found = 0;
    for (uint32_t i = 0; i < NVN_BLEND_SLOT_COUNT; i++)
        if (s->slot[i].dirty && (!found || s->slot[i].order < s->slot[*target].order)) {
            *target = i;
            found = 1;
        }
    if (found) s->slot[*target].dirty = 0;
    return found;
}

#endif
