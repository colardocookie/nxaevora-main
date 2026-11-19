/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* SceFiber: user-mode coroutines, each a set of registers and a stack (the context the game gives) that a thread runs
   in place of its own code until the fiber returns to it. sceFiberRun keeps the thread's registers and loads the
   fiber's (at its entry the first time, after its last sceFiberReturnToThread since); sceFiberReturnToThread does the
   reverse. The game's SceFiber holds the slot's index, the registers stay here. As the vitasdk headers (MIT) declare */
#include "vita/hle/hle.h"
#include <string.h>

#define SCE_FIBER_ERROR_NULL       0x80590001u
#define SCE_FIBER_ERROR_ALIGNMENT  0x80590002u
#define SCE_FIBER_ERROR_RANGE      0x80590003u
#define SCE_FIBER_ERROR_INVALID    0x80590004u
#define SCE_FIBER_ERROR_PERMISSION 0x80590005u
#define SCE_FIBER_ERROR_STATE      0x80590006u
#define SCE_FIBER_ERROR_BUSY       0x80590007u
#define MAGIC 0x52424946u /* "FIBR", then the slot's index */

static uint32_t arg(VitaProcess* p, VitaThread* t, unsigned n) { return vita_arg(p, t, n); }
static VitaFiber* fiber_of(VitaProcess* p, uint32_t address) {
    if (!address || !vita_mapped(p, address, 8) || vita_read32(p, address) != MAGIC) return NULL;
    uint32_t index = vita_read32(p, address + 4);
    return index < VITA_FIBERS && p->fibers[index].address == address ? &p->fibers[index] : NULL;
}
/* _sceFiberInitializeImpl(fiber, name, entry, argOnInitialize, addrContext, sizeContext, params) */
static uint32_t initialize(VitaProcess* p, VitaThread* t) {
    uint32_t address = arg(p, t, 0), entry = arg(p, t, 2), context = arg(p, t, 4), size = arg(p, t, 5);
    if (!address || !entry || !context) return SCE_FIBER_ERROR_NULL;
    if ((address & 7) || (context & 7) || (size & 7)) return SCE_FIBER_ERROR_ALIGNMENT;
    if (size < 512 || !vita_mapped(p, context, size)) return SCE_FIBER_ERROR_RANGE;
    unsigned slot = VITA_FIBERS;
    for (unsigned i = 0; i < VITA_FIBERS && slot == VITA_FIBERS; ++i)
        if (!p->fibers[i].address) slot = i;
    if (slot == VITA_FIBERS) return SCE_FIBER_ERROR_INVALID;
    p->fibers[slot] = (VitaFiber){.address = address, .entry = entry, .argument = arg(p, t, 3), .context = context,
                                  .context_size = size};
    memset(vita_ptr(p, address), 0, 128);
    vita_write32(p, address, MAGIC), vita_write32(p, address + 4, slot);
    return 0;
}
static uint32_t finalize(VitaProcess* p, VitaThread* t) {
    VitaFiber* f = fiber_of(p, arg(p, t, 0));
    if (!f) return arg(p, t, 0) ? SCE_FIBER_ERROR_INVALID : SCE_FIBER_ERROR_NULL;
    if (f->running) return SCE_FIBER_ERROR_STATE;
    memset(vita_ptr(p, f->address), 0, 8);
    memset(f, 0, sizeof *f);
    return 0;
}
/* sceFiberRun(fiber, argOnRunTo, argOnRun*): the thread's registers kept, the fiber's loaded, the value the fiber
   sees returned in its own r0 (the fiber's sceFiberReturnToThread returns 0, its entry gets them as arguments) */
static uint32_t run(VitaProcess* p, VitaThread* t) {
    uint32_t to = arg(p, t, 1);
    VitaFiber* f = fiber_of(p, arg(p, t, 0));
    if (!f) return arg(p, t, 0) ? SCE_FIBER_ERROR_INVALID : SCE_FIBER_ERROR_NULL;
    if (t->fiber) return SCE_FIBER_ERROR_PERMISSION;
    if (f->running) return SCE_FIBER_ERROR_BUSY;
    t->own = t->cpu;
    t->own.r[0] = 0;
    t->fiber_out = arg(p, t, 2);
    t->fiber = (uint32_t)(f - p->fibers) + 1;
    f->running = t->uid;
    if (!f->started) {
        SnxArmCPU* c = &t->cpu;
        memset(c->r, 0, sizeof c->r);
        c->r[0] = f->argument, c->r[1] = to;
        c->r[13] = (f->context + f->context_size) & ~7u;
        c->r[14] = VITA_FIBER_RETURN;
        c->cpsr = (c->cpsr & ~SNX_ARM_T) | (f->entry & 1 ? SNX_ARM_T : 0);
        c->r[15] = f->entry & ~1u;
        f->started = 1;
        return c->r[0];
    }
    t->cpu = f->cpu;
    if (f->in_out) vita_write32(p, f->in_out, to);
    return 0;
}
/* the thread's own registers back, what the fiber returns written where sceFiberRun asked */
static void back(VitaProcess* p, VitaThread* t, uint32_t value) {
    VitaFiber* f = &p->fibers[t->fiber - 1];
    f->running = 0;
    if (t->fiber_out) vita_write32(p, t->fiber_out, value);
    t->fiber = 0;
    t->cpu = t->own;
}
/* sceFiberReturnToThread(argOnReturn, argOnRun*): the fiber's registers kept, at its return, for its next run */
static uint32_t return_to_thread(VitaProcess* p, VitaThread* t) {
    if (!t->fiber) return SCE_FIBER_ERROR_PERMISSION;
    VitaFiber* f = &p->fibers[t->fiber - 1];
    f->cpu = t->cpu;
    f->cpu.r[0] = 0;
    f->in_out = arg(p, t, 1);
    back(p, t, arg(p, t, 0));
    return 0; /* sceFiberRun's */
}
void vita_fiber_ended(VitaProcess* p, VitaThread* t) {
    if (!t->fiber) {
        vita_thread_end(p, t, 0);
        return;
    }
    p->fibers[t->fiber - 1].started = 0; /* run again from its entry */
    back(p, t, 0);
}
static uint32_t get_self(VitaProcess* p, VitaThread* t) {
    uint32_t out = arg(p, t, 0);
    if (!out) return SCE_FIBER_ERROR_NULL;
    if (!t->fiber) return SCE_FIBER_ERROR_PERMISSION;
    vita_write32(p, out, p->fibers[t->fiber - 1].address);
    return 0;
}

const VitaHle vita_hle_fiber[] = {
    {0xf24a298cu, "_sceFiberInitializeImpl", initialize},
    {0xe160f844u, "sceFiberFinalize", finalize},
    {0x7df23243u, "sceFiberRun", run},
    {0x3b42921fu, "sceFiberReturnToThread", return_to_thread},
    {0x414d8ca5u, "sceFiberGetSelf", get_self},
    {0, NULL, NULL},
};
