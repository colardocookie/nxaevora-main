/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* A heap in guest memory, for SceLibc's malloc and SceLibKernel's sceClibMspace*: a header at the heap's base, then
   blocks to its end, each with two words before its bytes (the previous block's size, its own with bit 0 for in use),
   the free ones on a list through their first two words. A block is freed into its free neighbours, so no two free
   blocks touch. The game's memory holds it all, as dlmalloc's on the console */
#include "vita/hle/hle.h"
#include <string.h>

#define MAGIC    0x50414548u /* "HEAP" */
#define HEADER   32u         /* magic, the free list's first block, the end, the bytes in use */
#define OVERHEAD 8u
#define SMALLEST 16u /* a free block's size and links */
#define USED     1u

static uint32_t rd(VitaProcess* p, uint32_t a) { return vita_read32(p, a); }
static void wr(VitaProcess* p, uint32_t a, uint32_t v) { vita_write32(p, a, v); }
static uint32_t size_of(VitaProcess* p, uint32_t block) { return rd(p, block + 4) & ~7u; }
static bool used(VitaProcess* p, uint32_t block) { return rd(p, block + 4) & USED; }
/* the block's size and state, and the next block's record of its size */
static void set(VitaProcess* p, uint32_t block, uint32_t size, uint32_t in_use) {
    wr(p, block + 4, size | in_use);
    wr(p, block + size, size);
}
static void push(VitaProcess* p, uint32_t heap, uint32_t block) {
    uint32_t head = rd(p, heap + 4);
    wr(p, block + 8, head), wr(p, block + 12, 0);
    if (head) wr(p, head + 12, block);
    wr(p, heap + 4, block);
}
static void unlink_block(VitaProcess* p, uint32_t heap, uint32_t block) {
    uint32_t next = rd(p, block + 8), prev = rd(p, block + 12);
    if (prev)
        wr(p, prev + 8, next);
    else
        wr(p, heap + 4, next);
    if (next) wr(p, next + 12, prev);
}
static bool valid(VitaProcess* p, uint32_t heap) { return heap && vita_mapped(p, heap, HEADER) && rd(p, heap) == MAGIC; }
/* whether address is a block's bytes in use in heap */
static bool owned(VitaProcess* p, uint32_t heap, uint32_t address) {
    return valid(p, heap) && address >= heap + HEADER + OVERHEAD && address < rd(p, heap + 8) && !(address & 7) &&
           used(p, address - OVERHEAD);
}

uint32_t vita_heap_init(VitaProcess* p, uint32_t base, uint32_t size) {
    uint32_t start = (base + 7) & ~7u, end = (uint32_t)(((uint64_t)base + size) & ~7ull);
    if (!base || end < start || end - start < HEADER + SMALLEST + OVERHEAD || !vita_mapped(p, start, end - start))
        return 0;
    uint32_t first = start + HEADER, sentinel = end - OVERHEAD;
    wr(p, start, MAGIC), wr(p, start + 4, 0), wr(p, start + 8, sentinel), wr(p, start + 12, 0);
    wr(p, first, 0);
    set(p, first, sentinel - first, 0);
    wr(p, sentinel + 4, USED);
    push(p, start, first);
    return start;
}
uint32_t vita_heap_alloc(VitaProcess* p, uint32_t heap, uint32_t size, uint32_t align) {
    if (!valid(p, heap) || size > 0x7fffffffu) return 0;
    if (align < 8) align = 8;
    if (align & (align - 1)) return 0;
    uint32_t need = (size + OVERHEAD + 7) & ~7u;
    if (need < SMALLEST) need = SMALLEST;
    for (uint32_t block = rd(p, heap + 4); block; block = rd(p, block + 8)) {
        uint32_t room = size_of(p, block), at = block + OVERHEAD;
        uint64_t aligned = ((uint64_t)at + align - 1) & ~(uint64_t)(align - 1);
        /* a gap before an aligned block must be a block of its own */
        if (aligned != at && aligned - at < SMALLEST) aligned = ((uint64_t)at + SMALLEST + align - 1) & ~(uint64_t)(align - 1);
        uint64_t gap = aligned - at;
        if (gap + need > room) continue;
        unlink_block(p, heap, block);
        if (gap) {
            set(p, block, (uint32_t)gap, 0);
            push(p, heap, block);
            block += (uint32_t)gap, room -= (uint32_t)gap;
        }
        if (room - need >= SMALLEST) {
            set(p, block, need, USED);
            set(p, block + need, room - need, 0);
            push(p, heap, block + need);
        } else {
            set(p, block, room, USED);
        }
        wr(p, heap + 12, rd(p, heap + 12) + size_of(p, block));
        return block + OVERHEAD;
    }
    return 0;
}
void vita_heap_free(VitaProcess* p, uint32_t heap, uint32_t address) {
    if (!owned(p, heap, address)) return;
    uint32_t block = address - OVERHEAD, size = size_of(p, block), next = block + size;
    wr(p, heap + 12, rd(p, heap + 12) - size);
    if (!used(p, next)) {
        unlink_block(p, heap, next);
        size += size_of(p, next);
    }
    if (block != heap + HEADER) {
        uint32_t prev = block - rd(p, block);
        if (!used(p, prev)) {
            unlink_block(p, heap, prev);
            size += size_of(p, prev);
            block = prev;
        }
    }
    set(p, block, size, 0);
    push(p, heap, block);
}
uint32_t vita_heap_usable(VitaProcess* p, uint32_t heap, uint32_t address) {
    return owned(p, heap, address) ? size_of(p, address - OVERHEAD) - OVERHEAD : 0;
}
uint32_t vita_heap_realloc(VitaProcess* p, uint32_t heap, uint32_t address, uint32_t size, uint32_t align) {
    if (!address) return vita_heap_alloc(p, heap, size, align);
    if (!owned(p, heap, address)) return 0;
    uint32_t block = address - OVERHEAD, room = size_of(p, block), need = (size + OVERHEAD + 7) & ~7u;
    if (size > 0x7fffffffu) return 0;
    if (need < SMALLEST) need = SMALLEST;
    bool aligned = align <= 8 || !(address & (align - 1));
    uint32_t next = block + room;
    /* grown into the free block after it */
    if (aligned && need > room && !used(p, next) && room + size_of(p, next) >= need) {
        unlink_block(p, heap, next);
        wr(p, heap + 12, rd(p, heap + 12) + size_of(p, next));
        room += size_of(p, next);
        set(p, block, room, USED);
    }
    if (aligned && need <= room) {
        if (room - need >= SMALLEST) { /* the rest given back */
            set(p, block, need, USED);
            set(p, block + need, room - need, USED);
            vita_heap_free(p, heap, block + need + OVERHEAD);
        }
        return address;
    }
    uint32_t moved = vita_heap_alloc(p, heap, size, align);
    if (!moved) return 0;
    memmove(vita_ptr(p, moved), vita_ptr(p, address), room - OVERHEAD < size ? room - OVERHEAD : size);
    vita_heap_free(p, heap, address);
    return moved;
}
uint32_t vita_heap_in_use(VitaProcess* p, uint32_t heap) { return valid(p, heap) ? rd(p, heap + 12) : 0; }
