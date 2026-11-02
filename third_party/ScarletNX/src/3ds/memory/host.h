/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_CTR_HOST_MEMORY_H
#define SNX_CTR_HOST_MEMORY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* The host's side of the 3DS's flat address space (memory.c), on common/memory/host.h: the board's RAM as one host
   memory object, a 4 GiB reservation the guest's addresses are offsets into, and its pages mapped there as views of
   that object, so every guest address of a physical page (a heap page and its MAP mirror, shared memory and its
   service, the linear heap) is the same memory; and what a fault in that space does. */

/* whether the host can: 4 KiB pages (the 3DS's, the granularity of every mapping), and views of them */
bool ctr_host_flat(void);
/* the guest address space's size, every 32-bit address, and the reservation's, with an inaccessible guard after it */
#define CTR_HOST_SPACE (1ull << 32)
#define CTR_HOST_GUARD (1ull << 16)
/* A host fault in [base, base + CTR_HOST_SPACE + CTR_HOST_GUARD) goes to resolve first (write tracking's,
   snx_ctr_track_fault): when it says the page was a tracked one, now open enough, the access is made again. Otherwise
   it is the guest's data abort: the run cannot go on from the middle of a compiled block, so the handler reports the
   guest address, and *pc (the block the CPU entered) when pc is not NULL, and ends the process. Called on the thread
   that runs the guest, which faults on its own stack: on POSIX hosts (Linux, FreeBSD, the others) the handler runs on
   an alternate stack this gives the thread, large enough for a resolution that waits for the GPU. One space at a
   time; base NULL takes the handler away */
void ctr_host_faults(unsigned char* base, const uint32_t* pc, bool (*resolve)(void* context, uint32_t va),
                     void* context);
/* a line to the standard error, from a fault handler (no stdio) */
void ctr_host_say(const char* text, size_t size);
#endif
