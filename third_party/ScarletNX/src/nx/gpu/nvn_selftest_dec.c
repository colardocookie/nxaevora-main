/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn_selftest_dec.h"

/* The record ops of the library's interpreter (d839d20). */
#define REC_OP_RANGES 0x03u
#define REC_OP_JUMP 0x04u
#define REC_OP_END_MAX 0x02u
#define REC_HEADER_BYTES 8u
#define REC_JUMP_BYTES 0x10u
#define REC_ENTRY_BYTES 0x18u
#define REC_ENTRY_WORD_COUNT_OFFSET 16u
#define REC_JUMP_NEXT_OFFSET 8u
#define REC_HANDLE_ADDRESS_MASK 0xFFFFFFFFFFFFull
#define REC_HANDLE_TAG_SHIFT 48u
#define REC_HANDLE_ENTRY_BIT 1ull

typedef struct {
    const void* (*read)(void* ctx, uint64_t addr, size_t size);
    void* ctx;
    NvnStRange* out;
    unsigned cap, n;
    NvnStDecodeFault* fault;
} Decoder;

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t* p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

static int read_at(Decoder* d, uint64_t addr, size_t size, const uint8_t** out) {
    const void* p = d->read(d->ctx, addr, size);
    if (!p) {
        d->fault->op = 0;
        d->fault->addr = addr;
        return 0;
    }
    *out = (const uint8_t*)p;
    return 1;
}

/* One entry of an entry table, or one record entry: {u64 gpu_start, u64 cpu_start, u32 word_count, u8 type}. The
   run log of 2026-11-11 shows +8 equal to the command memory storage for the first range, so it is the start. */
static int add_entry(Decoder* d, const uint8_t* entry, uint64_t addr) {
    const uint64_t cpu_start = rd64(entry + 8);
    const uint32_t words = rd32(entry + REC_ENTRY_WORD_COUNT_OFFSET);
    if (!words || cpu_start > UINT64_MAX - (uint64_t)words * 4u) {
        d->fault->op = 0;
        d->fault->addr = addr;
        return NVN_ST_DEC_BAD_ENTRY;
    }
    if (d->n >= d->cap) return NVN_ST_DEC_TOO_MANY_RANGES;
    d->out[d->n].cpu_start = cpu_start;
    d->out[d->n].words = words;
    d->n++;
    return NVN_ST_DEC_OK;
}

/* The handle names tag entries of 0x18 bytes: a 16 bit count of ranges, not a record list. */
static int decode_entries(Decoder* d, uint64_t base, unsigned tag) {
    if (tag > d->cap) {
        d->fault->op = 0;
        d->fault->addr = base;
        return NVN_ST_DEC_TOO_MANY_RANGES;
    }
    const uint8_t* entries;
    if (!read_at(d, base, (size_t)tag * REC_ENTRY_BYTES, &entries)) return NVN_ST_DEC_READ_FAILED;
    for (unsigned i = 0; i < tag; i++) {
        const uint64_t addr = base + (uint64_t)i * REC_ENTRY_BYTES;
        const int status = add_entry(d, entries + (size_t)i * REC_ENTRY_BYTES, addr);
        if (status != NVN_ST_DEC_OK) {
            if (status == NVN_ST_DEC_BAD_ENTRY) d->fault->op = REC_OP_RANGES;
            return status;
        }
    }
    return NVN_ST_DEC_OK;
}

/* The handle names a record list: 03 adds a range entry table, 04 jumps, 0, 1 and 2 end it. */
static int decode_records(Decoder* d, uint64_t addr) {
    unsigned jumps = 0;
    for (;;) {
        const uint8_t* header;
        if (!read_at(d, addr, REC_HEADER_BYTES, &header)) return NVN_ST_DEC_READ_FAILED;
        const uint32_t op = header[0];
        if (op <= REC_OP_END_MAX) return NVN_ST_DEC_OK;
        if (op == REC_OP_JUMP) {
            if (jumps >= NVN_ST_DEC_MAX_JUMPS) {
                d->fault->op = op;
                d->fault->addr = addr;
                return NVN_ST_DEC_TOO_MANY_JUMPS;
            }
            jumps++;
            const uint8_t* record;
            if (!read_at(d, addr, REC_JUMP_BYTES, &record)) return NVN_ST_DEC_READ_FAILED;
            addr = rd64(record + REC_JUMP_NEXT_OFFSET);
            continue;
        }
        if (op == REC_OP_RANGES) {
            const uint32_t count = rd32(header + 4);
            if (count > d->cap - d->n) {
                d->fault->op = op;
                d->fault->addr = addr;
                return NVN_ST_DEC_TOO_MANY_RANGES;
            }
            const uint8_t* entries;
            if (!read_at(d, addr + REC_HEADER_BYTES, (size_t)count * REC_ENTRY_BYTES, &entries))
                return NVN_ST_DEC_READ_FAILED;
            for (uint32_t i = 0; i < count; i++) {
                const uint64_t entry_addr = addr + REC_HEADER_BYTES + (uint64_t)i * REC_ENTRY_BYTES;
                const int status = add_entry(d, entries + (size_t)i * REC_ENTRY_BYTES, entry_addr);
                if (status != NVN_ST_DEC_OK) {
                    if (status == NVN_ST_DEC_BAD_ENTRY) d->fault->op = op;
                    return status;
                }
            }
            addr += REC_HEADER_BYTES + (uint64_t)count * REC_ENTRY_BYTES;
            continue;
        }
        d->fault->op = op;
        d->fault->addr = addr;
        return NVN_ST_DEC_REFUSED_OP;
    }
}

int nvn_st_decode_recording(uint64_t handle, const void* (*read)(void* ctx, uint64_t addr, size_t size), void* ctx,
                            NvnStRange* out, unsigned cap, unsigned* n, NvnStDecodeFault* fault) {
    NvnStDecodeFault local;
    Decoder d = {read, ctx, out, cap, 0, fault ? fault : &local};
    d.fault->op = 0;
    d.fault->addr = 0;
    if (n) *n = 0;
    if (!read || !n || (!out && cap)) return NVN_ST_DEC_READ_FAILED;
    const uint64_t base = (handle & REC_HANDLE_ADDRESS_MASK) & ~REC_HANDLE_ENTRY_BIT;
    const unsigned tag = (unsigned)(handle >> REC_HANDLE_TAG_SHIFT);
    int status;
    if ((handle & REC_HANDLE_ENTRY_BIT) && tag)
        status = decode_entries(&d, base, tag);
    else
        status = decode_records(&d, base);
    if (status == NVN_ST_DEC_OK) d.fault->addr = 0;
    *n = d.n;
    return status;
}
