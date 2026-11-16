/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Write tracking (snx_ctr_track_*), with a handler standing in for the renderer: an owned page's data "lands" (the
   handler writes it), a watched page's copy is dropped. Through the flat space, as compiled code reaches memory (the
   fault handler of memory/host.h), through this API and by physical address: a read of an owned page lands it once
   and leaves it watched, a write takes it to the CPU, every alias traps, the guest's own permissions stay. */
#include "3ds/memory/ctr_memory.h"
#include "3ds/memory/host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define PAGE   0x20000000u /* FCRAM */
#define LANDED 0x5a
static struct {
    SnxCtrMemory* m;
    /* steps the handler took. Volatile: through the flat space the handler runs in the middle of a load or a store
       (its fault), which the compiler does not take for a call, so it would reuse the counts it read before */
    volatile unsigned owned, watched;
} g;
static void handler(void* context, uint32_t pa, SnxCtrTrackLevel level) {
    (void)context;
    assert(pa == PAGE);
    if (level == SNX_CTR_TRACK_OWN) {
        unsigned char data[4096];
        memset(data, LANDED, sizeof data);
        assert(snx_ctr_phys_write(g.m, pa, data, sizeof data) == SNX_CTR_MEM_OK); /* the handler may, untracked */
        snx_ctr_track_set(g.m, pa, 4096, SNX_CTR_TRACK_WATCH);
        ++g.owned;
    } else {
        snx_ctr_track_set(g.m, pa, 4096, SNX_CTR_TRACK_NONE);
        ++g.watched;
    }
}
static bool resolve(void* context, uint32_t va) { return snx_ctr_track_fault(context, va); }
int main(void) {
    g.m = snx_ctr_memory_new(SNX_CTR_OLD);
    SnxCtrAddressSpace* s = snx_ctr_space_new(g.m);
    assert(g.m && s);
    snx_ctr_track_handler(g.m, handler, NULL);
    assert(snx_ctr_map(s, 0x100000, PAGE, 4096, SNX_CTR_READ | SNX_CTR_WRITE) == SNX_CTR_MEM_OK);
    assert(snx_ctr_map(s, 0x200000, PAGE, 4096, SNX_CTR_READ) == SNX_CTR_MEM_OK); /* a read-only alias */
    unsigned char byte;
    /* the API: a read lands, a write drops */
    snx_ctr_track_set(g.m, PAGE, 4096, SNX_CTR_TRACK_OWN);
    assert(snx_ctr_read_ram(s, 0x100010, &byte, 1) == SNX_CTR_MEM_OK && byte == LANDED);
    assert(g.owned == 1 && !g.watched && snx_ctr_track_level(g.m, PAGE) == SNX_CTR_TRACK_WATCH);
    assert(snx_ctr_read_ram(s, 0x200010, &byte, 1) == SNX_CTR_MEM_OK && g.owned == 1); /* watched: reads pass */
    byte = 7;
    assert(snx_ctr_write_ram(s, 0x100010, &byte, 1) == SNX_CTR_MEM_OK);
    assert(g.watched == 1 && snx_ctr_track_level(g.m, PAGE) == SNX_CTR_TRACK_NONE);
    /* a write to an owned page by physical address: both steps */
    snx_ctr_track_set(g.m, PAGE, 4096, SNX_CTR_TRACK_OWN);
    assert(snx_ctr_phys_write(g.m, PAGE + 8, &byte, 1) == SNX_CTR_MEM_OK);
    assert(g.owned == 2 && g.watched == 2 && snx_ctr_track_level(g.m, PAGE) == SNX_CTR_TRACK_NONE);
    unsigned char* base = snx_ctr_space_base(s);
    unsigned checks = 3;
    if (base) { /* the flat space: compiled code's loads and stores, through the fault handler */
        ctr_host_faults(base, NULL, resolve, s);
        snx_ctr_track_set(g.m, PAGE, 4096, SNX_CTR_TRACK_OWN);
        volatile unsigned char* rw = base + 0x100000;
        volatile unsigned char* ro = base + 0x200000;
        assert(ro[100] == LANDED && g.owned == 3 && g.watched == 2); /* the alias's read lands it */
        assert(rw[5] == LANDED && g.owned == 3);                    /* the other view reads the same, no trap */
        rw[5] = 9;                                                   /* a write: the copy goes */
        assert(g.watched == 3 && snx_ctr_track_level(g.m, PAGE) == SNX_CTR_TRACK_NONE && ro[5] == 9);
        /* a write straight to an owned page: lands, then drops, and the store is the CPU's */
        snx_ctr_track_set(g.m, PAGE, 4096, SNX_CTR_TRACK_OWN);
        rw[6] = 11;
        assert(g.owned == 4 && g.watched == 4 && ro[6] == 11 && ro[7] == LANDED);
        /* unmapped and mapped again: the level is the physical page's, the new view traps */
        assert(snx_ctr_unmap(s, 0x200000, 4096) == SNX_CTR_MEM_OK);
        snx_ctr_track_set(g.m, PAGE, 4096, SNX_CTR_TRACK_WATCH);
        assert(snx_ctr_map(s, 0x300000, PAGE, 4096, SNX_CTR_READ | SNX_CTR_WRITE) == SNX_CTR_MEM_OK);
        volatile unsigned char* again = base + 0x300000;
        again[1] = 3;
        assert(g.watched == 5 && rw[1] == 3);
        /* the read-only alias stays read-only for the guest whatever the level does: its protect is the guest's */
        assert(snx_ctr_map(s, 0x200000, PAGE, 4096, SNX_CTR_READ) == SNX_CTR_MEM_OK);
        unsigned permissions;
        assert(snx_ctr_query(s, 0x200000, NULL, &permissions) == SNX_CTR_MEM_OK && permissions == SNX_CTR_READ);
        ctr_host_faults(NULL, NULL, NULL, NULL);
        checks += 5;
    } else
        printf("no flat space on this host: the API's tracking checked alone\n");
    snx_ctr_space_free(s);
    snx_ctr_memory_free(g.m);
    printf("write tracking: %u checks\n", checks);
    return 0;
}
