/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The portable part of the Windows profiler (profiler_win32.c): what a sample's program counter is, and the counts
   of one window per sampled thread. No runtime header, no global state, so the host test links it alone. */
#ifndef SNX_RUNTIME_PROFILE_CALC_H
#define SNX_RUNTIME_PROFILE_CALC_H
#include <stdint.h>

#define PROF_BUCKETS 65536u /* a power of two */
#define PROF_TOP 80u        /* the functions listed for each thread */

/* Game code, the runtime's own image, other host code (the driver, the system), or a system wait function. */
enum { PROF_GAME, PROF_RUNTIME, PROF_HOST, PROF_WAIT, PROF_KINDS };

typedef struct {
    uint64_t lo, hi; /* [lo, hi) */
} ProfRange;

/* key 0 marks a free bucket. A thread's buckets carry its slot and the slot's generation, so a thread that takes a
   freed slot does not inherit the samples of the one before. */
typedef struct {
    uint64_t key;
    uint32_t count;
    uint16_t slot, gen;
} ProfBucket;

typedef struct {
    ProfBucket buckets[PROF_BUCKETS];
    uint64_t unplaced;
} ProfTable;

/* game: the pc is in a game function. image: the runtime's [lo, hi). */
int prof_kind(uint64_t pc, int game, ProfRange image, const ProfRange* waits, uint32_t nwaits);
void prof_count(ProfTable* t, uint16_t slot, uint16_t gen, uint64_t key);
/* The thread's n largest buckets, largest first. The number found. */
uint32_t prof_top(const ProfTable* t, uint16_t slot, uint16_t gen, ProfBucket* out, uint32_t n);
void prof_clear(ProfTable* t);
/* The function that covers rva in an image's exception directory (.pdata): count entries of stride bytes, each
   starting with the function's begin and end RVAs, sorted by begin. 1 with *begin set, 0 when none covers it. */
int prof_function_at(const void* table, uint32_t count, uint32_t stride, uint32_t rva, uint32_t* begin);

#endif
