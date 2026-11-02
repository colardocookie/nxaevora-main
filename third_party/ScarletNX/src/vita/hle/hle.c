/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The imported functions by NID: each import of the image (vita/loader/image.h) is looked up in the modules' tables
   at its first call. A NID names one function wherever it is imported from, so the tables are by NID alone */
#include "vita/hle/hle.h"
#include "snx_settings.h"
#include <inttypes.h>
#include <stdio.h>

uint32_t vita_arg(VitaProcess* p, VitaThread* t, unsigned n) {
    return n < 4 ? t->cpu.r[n] : vita_read32(p, t->cpu.r[13] + (n - 4) * 4);
}
static const VitaHle* find(uint32_t nid) {
    static const VitaHle* const tables[] = {vita_hle_kernel, vita_hle_io,    vita_hle_ctrl, vita_hle_display,
                                            vita_hle_audio,  vita_hle_misc, vita_hle_gxm, vita_hle_libc,
                                            vita_hle_fiber, vita_hle_ngs, vita_hle_mp4, vita_hle_videodec,
                                            vita_hle_audiodec, vita_hle_dialog, vita_hle_net,
                                            vita_hle_trophy};
    for (size_t i = 0; i < sizeof tables / sizeof *tables; ++i)
        for (const VitaHle* h = tables[i]; h->name; ++h)
            if (h->nid == nid) return h;
    return NULL;
}
void vita_hle_call(VitaProcess* p, VitaThread* t, uint32_t index) {
    const VitaImport* import = &p->image.imports[index];
    const VitaHle* h = p->hle[index];
    if (!h && !p->unknown_logged[index]) {
        h = find(import->nid);
        p->hle[index] = h;
        if (!h) p->unknown_logged[index] = 1;
    }
    ++p->hle_calls;
    uint32_t r[4] = {t->cpu.r[0], t->cpu.r[1], t->cpu.r[2], t->cpu.r[3]};
    if (!h) {
        /* the game goes on with 0, success for most of the system's functions, and with the offline console's error
           for the network's (net.c), logged the first time */
        uint32_t error = vita_net_offline_error(import->library);
        if (p->unknown_logged[index] == 1 || g_settings.vita_import_trace) {
            printf("vita: %s's 0x%08" PRIx32 " is not implemented (thread \"%s\", lr 0x%08" PRIx32 ")", import->library,
                   import->nid, t->name, t->cpu.r[14]);
            if (error) printf(": offline, 0x%08" PRIx32, error);
            putchar('\n');
            p->unknown_logged[index] = 2;
        }
        t->cpu.r[0] = error;
        return;
    }
    t->call.redirected = false;
    uint32_t result = h->function(p, t);
    if (t->state == VITA_THREAD_READY && p->current == t && !t->call.redirected) t->cpu.r[0] = result;
    if (g_settings.vita_import_trace > 1)
        printf("vita: \"%s\" %s(0x%" PRIx32 ", 0x%" PRIx32 ", 0x%" PRIx32 ", 0x%" PRIx32 ")%s 0x%08" PRIx32
               " (sp 0x%08" PRIx32 ", lr 0x%08" PRIx32 ")\n",
               t->name, h->name, r[0], r[1], r[2], r[3], t->state == VITA_THREAD_WAITING ? " waits," : " ->",
               t->cpu.r[0], t->cpu.r[13], t->cpu.r[14]);
}
void vita_hle_variables(VitaProcess* p) {
    for (uint32_t i = 0; i < p->image.variable_count; ++i) {
        const VitaImport* v = &p->image.variables[i];
        const VitaHleVariable* h = vita_hle_libc_variables;
        while (h->name && h->nid != v->nid) ++h;
        if (h->name)
            h->init(p, v->stub);
        else
            printf("vita: %s's variable 0x%08" PRIx32 " is not implemented (left zero)\n", v->library, v->nid);
    }
}
void vita_hle_guest_call(VitaProcess* p, VitaThread* t, uint32_t func, const uint32_t* args, unsigned count,
                         VitaHleNext next, void* context) {
    SnxArmCPU* c = &t->cpu;
    if (!t->call.active) { /* the import's own return, kept for the end */
        t->call.active = true;
        t->call.lr = c->r[14], t->call.sp = c->r[13];
    }
    /* the arguments past the fourth on a stack of its own below the import's, 8-byte aligned */
    unsigned extra = count > 4 ? count - 4 : 0;
    uint32_t sp = (t->call.sp - extra * 4) & ~7u;
    for (unsigned i = 0; i < extra; ++i) vita_write32(p, sp + i * 4, args[4 + i]);
    for (unsigned i = 0; i < 4; ++i) c->r[i] = i < count ? args[i] : 0;
    c->r[13] = sp;
    c->r[14] = VITA_CALL_RETURN;
    c->r[15] = func & ~1u;
    c->cpsr = (c->cpsr & ~SNX_ARM_T) | (func & 1 ? SNX_ARM_T : 0);
    t->call.next = next, t->call.context = context, t->call.redirected = true;
}
void vita_hle_call_returned(VitaProcess* p, VitaThread* t) {
    SnxArmCPU* c = &t->cpu;
    if (!t->call.active || !t->call.next) {
        vita_thread_end(p, t, (int32_t)c->r[0]);
        return;
    }
    t->call.redirected = false;
    uint32_t result = t->call.next(p, t, t->call.context);
    if (t->call.redirected || t->state == VITA_THREAD_DEAD) return; /* another call, or the game ended */
    /* the import returns to its caller */
    t->call.active = false, t->call.next = NULL;
    c->r[0] = result;
    c->r[13] = t->call.sp;
    c->r[15] = t->call.lr & ~1u;
    c->cpsr = (c->cpsr & ~SNX_ARM_T) | (t->call.lr & 1 ? SNX_ARM_T : 0);
}
