/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The QEMU reference's host side (qemu_ref.h): writes a batch, runs the runner once under qemu-aarch64, reads the
   results */
#include "qemu_ref.h"

#include "common/utilities/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int reserve(SnxQrefBatch* b, size_t more) {
    size_t cap = b->in_cap ? b->in_cap : 1u << 16;
    uint8_t* p;
    if (b->in_used + more <= b->in_cap) return 0;
    while (cap < b->in_used + more) {
        if (cap > SIZE_MAX / 2) return -1;
        cap *= 2;
    }
    p = realloc(b->in, cap);
    if (!p) return -1;
    b->in = p;
    b->in_cap = cap;
    return 0;
}

/* little-endian on any host: the guest's order */
static void put64(SnxQrefBatch* b, uint64_t v) {
    unsigned i;
    for (i = 0; i < 8; ++i) b->in[b->in_used++] = (uint8_t)(v >> (8 * i));
}
static uint64_t get64(const uint8_t* p) {
    uint64_t v = 0;
    unsigned i;
    for (i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

void snx_qref_case_init(SnxQrefCase* c) {
    static const SnxQrefCase zero = {0};
    *c = zero;
    c->window = SNX_QREF_WINDOW;
    c->window_size = SNX_QREF_WINDOW_SIZE;
    c->seed_kind = SNX_QREF_SEED_RANDOM;
    c->code = SNX_QREF_WORD_PC;
    c->code_size = 2 * SNX_QREF_PAGE;
    c->pc = SNX_QREF_WORD_PC;
    c->steps = 1;
}

static void put_state(SnxQrefBatch* b, const SnxQrefState* s) {
    unsigned i;
    for (i = 0; i < 31; ++i) put64(b, s->x[i]);
    put64(b, s->sp);
    put64(b, s->nzcv & 0xF0000000u);
    put64(b, s->fpcr);
    put64(b, s->fpsr);
    for (i = 0; i < 32; ++i) {
        put64(b, s->v[i][0]);
        put64(b, s->v[i][1]);
    }
}

int snx_qref_add(SnxQrefBatch* b, const SnxQrefCase* c, const uint32_t* words, uint32_t nwords,
                 const SnxQrefPatch* patches, uint32_t npatches) {
    size_t size = 8 * (SNX_QREF_CASE_WORDS + SNX_QREF_STATE_WORDS + (nwords + 1) / 2), patch_total = 0;
    uint32_t i;
    if (!nwords || (uint64_t)nwords * 4 + 8 > c->code_size || c->code_size > SNX_QREF_MAX_CODE ||
        c->window_size > SNX_QREF_MAX_WINDOW || c->seed_kind >= SNX_QREF_SEED_COUNT)
        return -1;
    for (i = 0; i < npatches; ++i) {
        size_t padded = 8 * (2 + ((size_t)patches[i].size + 7) / 8);
        patch_total += padded;
        size += padded;
    }
    if (patch_total > SNX_QREF_MAX_PATCH || reserve(b, size + (b->in_used ? 0 : 16))) return -1;
    if (!b->in_used) { /* the header: the case count is written by snx_qref_run */
        put64(b, SNX_QREF_MAGIC);
        put64(b, 0);
    }
    put64(b, c->flags);
    put64(b, nwords);
    put64(b, npatches);
    put64(b, c->window);
    put64(b, c->window_size);
    put64(b, c->seed_kind);
    put64(b, c->seed);
    put64(b, c->code);
    put64(b, c->code_size);
    put64(b, c->pc);
    put64(b, c->steps);
    put_state(b, &c->state);
    for (i = 0; i < nwords; i += 2) put64(b, words[i] | (i + 1 < nwords ? (uint64_t)words[i + 1] << 32 : 0));
    for (i = 0; i < npatches; ++i) {
        const uint8_t* p = patches[i].bytes;
        uint32_t k;
        put64(b, patches[i].address);
        put64(b, patches[i].size);
        for (k = 0; k < patches[i].size; k += 8) {
            uint64_t v = 0;
            uint32_t j;
            for (j = 0; j < 8 && k + j < patches[i].size; ++j) v |= (uint64_t)p[k + j] << (8 * j);
            put64(b, v);
        }
    }
    return (int)b->cases++;
}

static void get_state(const uint8_t* p, SnxQrefState* s) {
    unsigned i;
    for (i = 0; i < 31; ++i) s->x[i] = get64(p + 8 * (SNX_QREF_S_X + i));
    s->sp = get64(p + 8 * SNX_QREF_S_SP);
    s->nzcv = (uint32_t)get64(p + 8 * SNX_QREF_S_NZCV);
    s->fpcr = (uint32_t)get64(p + 8 * SNX_QREF_S_FPCR);
    s->fpsr = (uint32_t)get64(p + 8 * SNX_QREF_S_FPSR);
    for (i = 0; i < 32; ++i) {
        s->v[i][0] = get64(p + 8 * (SNX_QREF_S_V + 2 * i));
        s->v[i][1] = get64(p + 8 * (SNX_QREF_S_V + 2 * i + 1));
    }
}

/* walks the result file: counts its changes and bytes (fill 0), or fills the results (fill 1); -1 when malformed */
static int parse(SnxQrefBatch* b, const uint8_t* file, size_t size, int fill, size_t* changes, size_t* bytes) {
    size_t at = 16, nc = 0, nb = 0;
    uint32_t i;
    if (size < 16 || get64(file) != SNX_QREF_MAGIC || get64(file + 8) != b->cases) return -1;
    for (i = 0; i < b->cases; ++i) {
        const uint8_t* r = file + at;
        SnxQrefResult* out = fill ? &b->results[i] : NULL;
        if (size - at < 8 * (SNX_QREF_RESULT_WORDS + SNX_QREF_STATE_WORDS)) return -1;
        if (out) {
            out->outcome = (uint32_t)get64(r + 8 * SNX_QREF_R_OUTCOME);
            out->signo = (uint32_t)get64(r + 8 * SNX_QREF_R_SIGNO);
            out->sicode = (uint32_t)get64(r + 8 * SNX_QREF_R_SICODE);
            out->pc = get64(r + 8 * SNX_QREF_R_PC);
            out->fault = get64(r + 8 * SNX_QREF_R_FAULT);
            out->imm = get64(r + 8 * SNX_QREF_R_IMM);
            out->steps = get64(r + 8 * SNX_QREF_R_STEPS);
            out->svcs = get64(r + 8 * SNX_QREF_R_SVCS);
            out->pstate = get64(r + 8 * SNX_QREF_R_PSTATE);
            get_state(r + 8 * SNX_QREF_RESULT_WORDS, &out->state);
            out->first_change = (uint32_t)nc;
        }
        at += 8 * (SNX_QREF_RESULT_WORDS + SNX_QREF_STATE_WORDS);
        for (;;) {
            uint64_t address, len;
            if (size - at < 8) return -1;
            address = get64(file + at);
            at += 8;
            if (address == ~0ull) break;
            if (size - at < 8) return -1;
            len = get64(file + at);
            at += 8;
            if (len > SNX_QREF_MAX_CHANGED || size - at < (len + 7) / 8 * 8) return -1;
            if (fill) {
                SnxQrefChange* c = &b->changes[nc];
                c->address = address;
                c->offset = (uint32_t)nb;
                c->size = (uint32_t)len;
                memcpy(b->bytes + nb, file + at, (size_t)len);
            }
            at += (size_t)(len + 7) / 8 * 8;
            nb += (size_t)len;
            ++nc;
        }
        if (size - at < 8) return -1;
        if (out) {
            out->truncated = get64(file + at) != 0;
            out->changes = (uint32_t)(nc - out->first_change);
        }
        at += 8;
    }
    *changes = nc;
    *bytes = nb;
    return at == size ? 0 : -1;
}

int snx_qref_run(SnxQrefBatch* b, const char* qemu, const char* runner, const char* cpu, const char* dir, char* error,
                 size_t error_size) {
    char *in = snx_path_join(dir, "qemu_ref.batch"), *out = snx_path_join(dir, "qemu_ref.results");
    char* output = NULL;
    uint8_t* file = NULL;
    size_t size = 0, changes = 0, bytes = 0, results_size;
    int status = -1, i;
    const char* argv[10];
    free(b->results);
    b->results = NULL;
    b->changes = NULL;
    b->bytes = NULL;
    if (!in || !out || !b->cases) {
        snprintf(error, error_size, "%s", b->cases ? "out of memory" : "no cases");
        goto done;
    }
    for (i = 0; i < 8; ++i) b->in[8 + i] = (uint8_t)((uint64_t)b->cases >> (8 * i));
    if (snx_mkdirs(dir) || snx_write_file(in, b->in, b->in_used)) {
        snprintf(error, error_size, "cannot write %s", in);
        goto done;
    }
    remove(out);
    i = 0;
    argv[i++] = qemu;
    argv[i++] = "-B";
    argv[i++] = SNX_QREF_GUEST_BASE;
    argv[i++] = "-cpu";
    argv[i++] = cpu;
    argv[i++] = runner;
    argv[i++] = in;
    argv[i++] = out;
    argv[i] = NULL;
    status = snx_run(argv, NULL, NULL, &output);
    if (status) {
        snprintf(error, error_size, "%s -cpu %s exited %d: %s", qemu, cpu, status, output ? output : "");
        status = -1;
        goto done;
    }
    status = -1;
    file = snx_read_file(out, &size);
    if (!file || parse(b, file, size, 0, &changes, &bytes)) {
        snprintf(error, error_size, "%s is not the runner's results", out);
        goto done;
    }
    /* the results, their changes and the changed bytes: one block */
    results_size = (size_t)b->cases * sizeof(SnxQrefResult);
    if (changes > (SIZE_MAX - results_size - bytes) / sizeof(SnxQrefChange)) goto done;
    b->results = malloc(results_size + changes * sizeof(SnxQrefChange) + bytes + 1);
    if (!b->results) {
        snprintf(error, error_size, "out of memory");
        goto done;
    }
    b->changes = (SnxQrefChange*)((uint8_t*)b->results + results_size);
    b->bytes = (uint8_t*)(b->changes + changes);
    if (parse(b, file, size, 1, &changes, &bytes)) goto done;
    status = 0;
done:
    free(file);
    free(output);
    free(in);
    free(out);
    return status;
}

void snx_qref_clear(SnxQrefBatch* b) {
    free(b->results);
    b->results = NULL;
    b->changes = NULL;
    b->bytes = NULL;
    b->in_used = 0;
    b->cases = 0;
}

void snx_qref_free(SnxQrefBatch* b) {
    snx_qref_clear(b);
    free(b->in);
    b->in = NULL;
    b->in_cap = 0;
}

uint8_t snx_qref_seed_byte(uint32_t kind, uint64_t seed, uint64_t base, uint64_t address) {
    const uint64_t offset = address - base;
    return (uint8_t)(snx_qref_seed64(kind, seed, base, offset & ~7ull) >> (8 * (offset & 7)));
}

const char* snx_qref_outcome_name(uint32_t outcome) {
    static const char* const names[SNX_QREF_OUTCOMES] = {"next",  "branch", "UNDEF",   "BRK",
                                                         "SVC",   "fault",  "timeout", "error"};
    return outcome < SNX_QREF_OUTCOMES ? names[outcome] : "?";
}

int snx_qref_switch_sysreg(uint32_t word, uint64_t* value, const char** name) {
    /* MRS Xt, <reg>: the word without Rt; values as a Switch's Cortex-A57 cores and its kernel give them */
    static const struct {
        uint32_t mrs;
        int kind;
        uint64_t value;
        const char* name;
    } regs[] = {
        {0xD53BE040u, SNX_QREF_SYSREG_VARIES, 0, "CNTVCT_EL0"},
        {0xD53BE000u, SNX_QREF_SYSREG_FIXED, 19200000, "CNTFRQ_EL0"},
        {0xD53BD060u, SNX_QREF_SYSREG_VARIES, 0, "TPIDRRO_EL0"}, /* the thread's TLS, the runtime's to choose */
        {0xD53B0020u, SNX_QREF_SYSREG_FIXED, 0x8444C004u, "CTR_EL0"},
        {0xD53B00E0u, SNX_QREF_SYSREG_FIXED, 4, "DCZID_EL0"},
    };
    size_t i;
    /* MRS of the ID registers (op0 3, op1 0, CRn 0: MIDR_EL1, MPIDR_EL1, the ID_* ones) is UNDEF at EL0, and Horizon
       doesn't emulate it as Linux, and so QEMU's user mode, does */
    if ((word & 0xFFFFF000u) == 0xD5380000u) {
        if (value) *value = 0;
        if (name) *name = "ID register";
        return SNX_QREF_SYSREG_UNDEF;
    }
    for (i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i) {
        if ((word & ~0x1Fu) != regs[i].mrs) continue;
        if (value) *value = regs[i].value;
        if (name) *name = regs[i].name;
        return regs[i].kind;
    }
    return SNX_QREF_SYSREG_NONE;
}
