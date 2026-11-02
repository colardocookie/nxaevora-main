/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The QEMU reference: a batch of A64 cases (a word or a few at a pc, a starting state, a seeded memory window) run in
   one qemu-aarch64 process by the AArch64 runner (runner.c), which gives back each case's end state, how it ended and
   the memory it changed. Linux hosts with qemu-aarch64; cmake/qemu_ref.cmake builds the runner. */
#ifndef SNX_QEMU_REF_H
#define SNX_QEMU_REF_H

#include "qemu_ref_proto.h"

#include <stddef.h>
#include <stdint.h>

/* the A64 state a case starts from and ends with */
typedef struct {
    uint64_t x[31], sp;
    uint32_t nzcv; /* bits 31-28 */
    uint32_t fpcr, fpsr;
    uint64_t v[32][2]; /* low and high halves */
} SnxQrefState;

/* a case. The window is mapped at its address with the seed's bytes; the code region holds the case's words from
   its first slot and a brk pad in every other slot, and pages of it inside the window replace the seed there. A
   step runs the word at pc; steps 0 runs the words in place from pc until the code leaves them. The runner's own
   image is at SNX_QREF_RUNNER_IMAGE: a state whose stores reach it corrupts the runner */
typedef struct {
    uint64_t window, window_size; /* page-aligned; size 0: no window */
    uint32_t seed_kind;           /* SNX_QREF_SEED_* */
    uint64_t seed;
    uint64_t code, code_size; /* page-aligned, inside the window or apart from it */
    uint64_t pc, steps;
    uint32_t flags; /* SNX_QREF_FULL_DIFF */
    SnxQrefState state;
} SnxQrefCase;

/* bytes written over a case's seed */
typedef struct {
    uint64_t address;
    const void* bytes;
    uint32_t size;
} SnxQrefPatch;

/* a run of memory the case changed: size bytes from address, which are now batch->bytes[offset...] */
typedef struct {
    uint64_t address;
    uint32_t offset, size;
} SnxQrefChange;

typedef struct {
    uint32_t outcome; /* SNX_QREF_NEXT ... */
    uint32_t signo, sicode;
    uint64_t pc;     /* where the last step went (see the outcomes in qemu_ref_proto.h) */
    uint64_t fault;  /* SNX_QREF_FAULT: the address */
    uint64_t imm;    /* BRK, SVC: the immediate; SNX_QREF_ERROR: SNX_QREF_ERR_* */
    uint64_t steps;  /* steps completed (0 for a free run) */
    uint64_t svcs;   /* the SVCs the case passed (a step ends on one; a free run goes on after it) */
    uint64_t pstate; /* the whole of PSTATE as the frame gave it */
    SnxQrefState state;
    uint32_t first_change, changes; /* batch->changes[first_change...] */
    int truncated;                  /* more changed than SNX_QREF_MAX_CHANGED */
} SnxQrefResult;

/* the cases being built, and once run, their results: results, changes and bytes are one allocation */
typedef struct {
    uint8_t* in;
    size_t in_used, in_cap;
    uint32_t cases;
    SnxQrefResult* results;
    SnxQrefChange* changes;
    uint8_t* bytes;
} SnxQrefBatch;

/* the single-word layout: the word at SNX_QREF_WORD_PC inside a random-seeded window at SNX_QREF_WINDOW, one step,
   every register zero */
void snx_qref_case_init(SnxQrefCase* c);
/* adds a case; its index, or -1 when it doesn't fit the runner's limits */
int snx_qref_add(SnxQrefBatch* batch, const SnxQrefCase* c, const uint32_t* words, uint32_t nwords,
                 const SnxQrefPatch* patches, uint32_t npatches);
/* runs the cases under qemu (its path) -cpu cpu with the runner, through files in dir; 0 when it ran, else -1 with
   why in error. The results replace the last run's */
int snx_qref_run(SnxQrefBatch* batch, const char* qemu, const char* runner, const char* cpu, const char* dir,
                 char* error, size_t error_size);
/* forgets the cases and results, keeping the memory for the next */
void snx_qref_clear(SnxQrefBatch* batch);
void snx_qref_free(SnxQrefBatch* batch);

/* the byte a seed gives at address of a window at base */
uint8_t snx_qref_seed_byte(uint32_t kind, uint64_t seed, uint64_t base, uint64_t address);
const char* snx_qref_outcome_name(uint32_t outcome);

/* System registers QEMU can't read as the Switch does (MRS Xt of them): their name, and the Switch's value when it
   is a constant. A comparison takes the Switch's value for Xt, leaves Xt out when it varies, and takes UNDEF for
   QEMU's outcome when the Switch raises it (UNDEF) */
enum { SNX_QREF_SYSREG_NONE, SNX_QREF_SYSREG_FIXED, SNX_QREF_SYSREG_VARIES, SNX_QREF_SYSREG_UNDEF };
int snx_qref_switch_sysreg(uint32_t word, uint64_t* value, const char** name);

#endif
