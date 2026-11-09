/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Differential verifier: each original, compiled from the relocated main module by ScarletNX's LLVM backend, and its
   reconstruction run from the same inputs and must leave the same results, registers and memory (README.md).
     recon-verify MAIN.BIN [--functions TSV] [--runs N] [--seed S] [--only NAME] [--results FILE] [--control]
   --control pairs each original with a wrong variant of its reconstruction (mutants_lua53.c): all must fail. */
#include "llvm_steps.h"
#include "nx/loader/codegen_view.h"
#include "recon/registry.h"
#include "callees.h"
#include "fixtures.h"
#include "imports.h"
#include "mutants.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAIN_BASE 0x8005000ull     /* where the runtime maps main for this game (functions.tsv uses it too) */
#define BSS_ROOM (64ull << 20)     /* zeroed room after the image for its BSS */
#define SCRATCH_SIZE (1ull << 20)  /* fixture memory, compared byte for byte */
#define HOST_AREA 0x10000ull       /* after scratch: data the import models hand out, not compared */
#define RETURN_ADDRESS 0x1000ull   /* X30 on entry: a RET must land here */
#define STACK_TOP (SCRATCH_SIZE - 0x1000) /* SP on entry, as a scratch offset */
#define DEAD_STACK 0x8000ull       /* below SP: the frames of the function and its callees, dead after it returns */
#define R_AARCH64_RELATIVE 1027
#define MAX_CASES 64
#define MAX_COMPILED 1024
#define MAX_CLOSURE 256
#define MAX_IMPORTS 128
#define MAX_LOGGED 64

GAddr recon_main_base = MAIN_BASE;

static uint8_t* g_buffer; /* host memory for guest MAIN_BASE.. */
static uint8_t* g_mem;    /* host address of guest 0 */
static size_t g_image_size;
static GAddr g_scratch;

static void on_undef(CPU* c, uint64_t pc, uint32_t w) {
    fprintf(stderr, "undefined instruction %08x at %llx\n", w, (unsigned long long)pc);
    c->pc = 0;
}

static uint8_t* read_file(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc((size_t)n);
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return data;
}

static uint64_t rd64(const uint8_t* p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}
static uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

/* The module's own dynamic table names its RELA relocations: every R_AARCH64_RELATIVE becomes base + addend, as the
   game's loader does. Returns how many were applied. */
static size_t relocate(uint8_t* image, size_t size, uint64_t base) {
    const uint32_t mod0 = rd32(image + 4);
    const uint64_t dynamic = mod0 + (uint64_t)(int64_t)(int32_t)rd32(image + mod0 + 4);
    uint64_t rela = 0, rela_size = 0, applied = 0;
    for (uint64_t d = dynamic; d + 16 <= size; d += 16) {
        const uint64_t tag = rd64(image + d), value = rd64(image + d + 8);
        if (!tag) break;
        if (tag == 7) rela = value;
        if (tag == 8) rela_size = value;
    }
    for (uint64_t p = rela; p + 24 <= rela + rela_size && p + 24 <= size; p += 24) {
        const uint64_t off = rd64(image + p), info = rd64(image + p + 8);
        const int64_t addend = (int64_t)rd64(image + p + 16);
        if ((info & 0xFFFFFFFF) != R_AARCH64_RELATIVE || off + 8 > size + BSS_ROOM) continue;
        const uint64_t v = base + (uint64_t)addend;
        memcpy(image + off, &v, 8);
        applied++;
    }
    return applied;
}

static uint64_t fnv1a_step(uint64_t h, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}
static uint64_t fnv1a(const uint8_t* p, size_t n) { return fnv1a_step(0xCBF29CE484222325ull, p, n); }

/* ---- the function inventory (functions.tsv), for the sizes of callees ---- */

typedef struct {
    uint64_t offset; /* in main */
    uint32_t size;
} InventoryEntry;

static InventoryEntry* g_inventory;
static size_t g_inventory_count;

static int by_offset(const void* a, const void* b) {
    const uint64_t x = ((const InventoryEntry*)a)->offset, y = ((const InventoryEntry*)b)->offset;
    return x < y ? -1 : x > y;
}

static int load_inventory(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return -1;
    size_t cap = 1 << 18;
    g_inventory = malloc(cap * sizeof(*g_inventory));
    unsigned long long addr;
    unsigned size;
    while (fscanf(f, "%llx %u", &addr, &size) == 2) {
        if (addr < MAIN_BASE || addr - MAIN_BASE >= g_image_size) continue;
        if (g_inventory_count == cap) g_inventory = realloc(g_inventory, (cap *= 2) * sizeof(*g_inventory));
        g_inventory[g_inventory_count].offset = addr - MAIN_BASE;
        g_inventory[g_inventory_count].size = size;
        g_inventory_count++;
    }
    fclose(f);
    qsort(g_inventory, g_inventory_count, sizeof(*g_inventory), by_offset);
    return 0;
}

/* The size of the function starting at the main offset, 0 for none */
static uint32_t inventory_size(uint64_t offset) {
    size_t lo = 0, hi = g_inventory_count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (g_inventory[mid].offset < offset) lo = mid + 1;
        else hi = mid;
    }
    return lo < g_inventory_count && g_inventory[lo].offset == offset ? g_inventory[lo].size : 0;
}

/* ---- originals compiled from the dump: the functions under test and their callees, each once ---- */

typedef struct {
    uint64_t offset;
    uint32_t size;
    char name[24];
    SnxCodegen codegen;
    SnxProgram program;
    SnxModuleImage module;
    SnxFde fde;
    size_t step;
    GuestFn fn;
    int analyzed;
} Compiled;

static Compiled g_compiled[MAX_COMPILED];
static size_t g_compiled_count;

static Compiled* find_compiled(uint64_t offset) {
    for (size_t i = 0; i < g_compiled_count; i++)
        if (g_compiled[i].offset == offset) return &g_compiled[i];
    return NULL;
}

static Compiled* add_compiled(uint64_t offset, uint32_t size) {
    Compiled* f = find_compiled(offset);
    if (f || g_compiled_count == MAX_COMPILED) return f;
    f = &g_compiled[g_compiled_count++];
    memset(f, 0, sizeof(*f));
    f->offset = offset;
    f->size = size;
    snprintf(f->name, sizeof(f->name), "main+%llx", (unsigned long long)offset);
    return f;
}

/* The original's bytes as a one-function module from their page on, so the code generator sees the real pcs. A
   branch out of that module is a call through snx_lookup, which the harness answers (on_miss). */
static int analyze(Compiled* f) {
    const uint64_t page = f->offset & ~0xFFFull;
    f->module.name = f->name;
    f->module.image = g_buffer + page;
    f->module.image_size = (size_t)(f->offset + f->size - page);
    f->module.base = MAIN_BASE + page;
    f->module.size = (f->module.image_size + 0xFFF) & ~0xFFFull;
    f->module.text_size = (SnxU32)f->module.image_size;
    f->fde.start = f->offset - page;
    f->fde.size = f->size;
    f->module.fde = &f->fde;
    f->module.fde_count = 1;
    f->program.modules = &f->module;
    f->program.module_count = 1;
    f->program.npdm.title_id = 0x01001F5010DFA000ull;
    f->program.npdm.stack = 0x100000;
    f->program.npdm.prio = 44;
    f->program.npdm.core = 0;
    return snx_nx_codegen_analyze(&f->codegen, &f->program, NULL, 0, 1);
}

/* ---- imports, reached through main's PLT stubs ---- */

typedef struct {
    uint64_t offset; /* the stub's */
    char name[48];
    ImportModel model; /* NULL: calling it fails the run */
} Import;

static Import g_imports[MAX_IMPORTS];
static size_t g_import_count;

static const Import* find_import(uint64_t offset) {
    for (size_t i = 0; i < g_import_count; i++)
        if (g_imports[i].offset == offset) return &g_imports[i];
    return NULL;
}

/* ---- cases ---- */

typedef struct {
    const ReconFunction* recon;
    const Fixture* fixture;
    const CalleePlan* plan;
    GuestFn mutant;
    Compiled* original;
    uint64_t closure[MAX_CLOSURE]; /* the main functions it may call, compiled, sorted */
    size_t closure_count;
    uint64_t closure_hash; /* over the closure's offsets and bytes */
    char error[192];       /* why it cannot run */
} Case;

static const Stub* find_stub(const Case* k, uint64_t offset) {
    for (size_t i = 0; k->plan && i < k->plan->stub_count; i++)
        if (k->plan->stubs[i].main_offset == offset) return &k->plan->stubs[i];
    return NULL;
}

static int in_closure(const Case* k, uint64_t offset) {
    for (size_t i = 0; i < k->closure_count; i++)
        if (k->closure[i] == offset) return 1;
    return 0;
}

static int u64_order(const void* a, const void* b) {
    const uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return x < y ? -1 : x > y;
}

/* The direct branch target of the word at pc (B, BL, B.cond, CBZ, CBNZ, TBZ, TBNZ), or 0 */
static uint64_t direct_target(uint32_t w, uint64_t pc) {
    if ((w & 0x7C000000) == 0x14000000) {
        int64_t imm = w & 0x3FFFFFF;
        if (imm & (1 << 25)) imm -= 1 << 26;
        return pc + (uint64_t)(imm * 4);
    }
    if ((w & 0xFF000010) == 0x54000000 || (w & 0x7E000000) == 0x34000000) {
        int64_t imm = (w >> 5) & 0x7FFFF;
        if (imm & (1 << 18)) imm -= 1 << 19;
        return pc + (uint64_t)(imm * 4);
    }
    if ((w & 0x7E000000) == 0x36000000) {
        int64_t imm = (w >> 5) & 0x3FFF;
        if (imm & (1 << 13)) imm -= 1 << 14;
        return pc + (uint64_t)(imm * 4);
    }
    return 0;
}

static void case_error(Case* k, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(k->error, sizeof(k->error), format, args);
    va_end(args);
}

/* Every main function the original can reach by direct branches, short of stubs and imports. Each is compiled. */
static int find_closure(Case* k) {
    uint64_t todo[MAX_CLOSURE];
    size_t pending = 0;
    k->closure[k->closure_count++] = k->recon->main_offset;
    todo[pending++] = k->recon->main_offset;
    while (pending) {
        const uint64_t at = todo[--pending];
        const uint32_t size = at == k->recon->main_offset ? k->recon->size : inventory_size(at);
        for (uint64_t pc = at; pc < at + size; pc += 4) {
            const uint64_t t = direct_target(rd32(g_buffer + pc), pc);
            if (!t || (t >= at && t < at + size)) continue;
            if (find_stub(k, t)) {
                if (t >= (at & ~0xFFFull) && t < at)
                    return case_error(k, "stub main+%llx shares a page with its caller", (unsigned long long)t), -1;
                continue;
            }
            char name[48];
            if (import_name_at(g_buffer, g_image_size, t, name, sizeof(name))) {
                if (!find_import(t) && g_import_count < MAX_IMPORTS) {
                    Import* im = &g_imports[g_import_count++];
                    im->offset = t;
                    snprintf(im->name, sizeof(im->name), "%s", name);
                    im->model = import_model(name);
                }
                continue;
            }
            if (in_closure(k, t)) continue;
            if (!g_inventory_count)
                return case_error(k, "it calls main+%llx: pass --functions", (unsigned long long)t), -1;
            const uint32_t callee_size = inventory_size(t);
            if (!callee_size)
                return case_error(k, "main+%llx branches to main+%llx, not a function of the inventory",
                                  (unsigned long long)pc, (unsigned long long)t),
                       -1;
            if (k->closure_count == MAX_CLOSURE) return case_error(k, "more than %d callees", MAX_CLOSURE), -1;
            k->closure[k->closure_count++] = t;
            todo[pending++] = t;
        }
    }
    qsort(k->closure, k->closure_count, sizeof(k->closure[0]), u64_order);
    k->closure_hash = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < k->closure_count; i++) {
        const uint64_t at = k->closure[i];
        const uint32_t size = at == k->recon->main_offset ? k->recon->size : inventory_size(at);
        if (!add_compiled(at, size)) return case_error(k, "more than %d functions to compile", MAX_COMPILED), -1;
        k->closure_hash = fnv1a_step(k->closure_hash, (const uint8_t*)&at, 8);
        k->closure_hash = fnv1a_step(k->closure_hash, g_buffer + at, size);
    }
    k->original = find_compiled(k->recon->main_offset);
    return 0;
}

/* ---- calls out of the code under test: RtAPI.miss, since CPU.disp is empty ---- */

typedef struct {
    uint64_t target, x[8], d[8];
    uint8_t int_args, fp_args;
} LoggedCall;

typedef struct {
    LoggedCall calls[MAX_LOGGED];
    unsigned count;
} CallLog;

static struct {
    const Case* k;
    CallLog* log;
    uint64_t seed; /* the stubs' results, the same for both sides of a run */
    ImportEnv env;
    unsigned calls, imports; /* this side's calls out of the function and to imports */
    char failure[192];
    void* escape[5]; /* __builtin_setjmp's: a failed call ends the side's run at once */
} g_run;

static void call_failed(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(g_run.failure, sizeof(g_run.failure), format, args);
    va_end(args);
    __builtin_longjmp(g_run.escape, 1);
}

static uint64_t mix(uint64_t x) { /* splitmix64's finalizer */
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static void stub_call(CPU* c, const Stub* s, uint64_t target) {
    CallLog* log = g_run.log;
    if (log->count == MAX_LOGGED) call_failed("more than %d stubbed calls", MAX_LOGGED);
    LoggedCall* e = &log->calls[log->count];
    memset(e, 0, sizeof(*e));
    e->target = target;
    e->int_args = s->int_args;
    e->fp_args = s->fp_args;
    for (int i = 0; i < s->int_args; i++) e->x[i] = c->x[i];
    for (int i = 0; i < s->fp_args; i++) e->d[i] = c->v[i].d[0];
    const uint64_t h = mix(g_run.seed + log->count++);
    if (s->effect) { /* before the result, while the arguments are still in the registers */
        const Guest g = {g_mem, MAIN_BASE};
        s->effect(c, &g, g_scratch, mix(h ^ 0xEFFEC7ull));
    }
    if (s->result == STUB_POINTER) {
        c->x[0] = (h & 3) ? g_scratch + ((h >> 8) % (SCRATCH_SIZE - 0x100) & ~0xFull) : 0;
    } else {
        const uint64_t choices[6] = {0, 1, ~0ull, h >> 32, (h >> 8) & 0xF, h};
        c->x[0] = choices[h % 6];
    }
    c->v[0].d[0] = mix(h);
    c->pc = c->x[30];
}

static void on_miss(CPU* c, uint64_t pc) {
    const Case* k = g_run.k;
    if (pc < MAIN_BASE || pc - MAIN_BASE >= g_image_size)
        call_failed("a call to %llx, outside main", (unsigned long long)pc);
    const uint64_t off = pc - MAIN_BASE;
    g_run.calls++;
    const Stub* s = find_stub(k, off);
    if (s) {
        stub_call(c, s, off);
        return;
    }
    const Import* im = find_import(off);
    if (im) {
        if (!im->model) call_failed("a call to %s, which has no model", im->name);
        g_run.imports++;
        const char* why = im->model(c, &g_run.env);
        if (why) call_failed("%s", why);
        return;
    }
    if (!in_closure(k, off)) call_failed("a call to main+%llx, outside the original's callees", (unsigned long long)off);
    find_compiled(off)->fn(c, pc);
}

/* ---- one run ---- */

typedef struct {
    uint64_t x0, d0, sp, pc, saved[11];
} Outcome;

static void prepare(CPU* c, RtAPI* rt, Rng* r) {
    memset(c, 0, sizeof(*c));
    c->mem = g_mem;
    c->rt = rt;
    for (int i = 0; i < 31; i++) c->x[i] = rng_next(r); /* garbage in every register an argument does not set */
    for (int i = 19; i <= 29; i++) c->x[i] = 0xC0DE000000000000ull | (uint64_t)i;
    c->x[30] = RETURN_ADDRESS;
    c->x[31] = g_scratch + STACK_TOP;
}

static Outcome outcome(const CPU* c) {
    Outcome o;
    o.x0 = c->x[0];
    o.d0 = c->v[0].d[0];
    o.sp = c->x[31];
    o.pc = c->pc;
    for (int i = 0; i < 11; i++) o.saved[i] = c->x[19 + i];
    return o;
}

/* Runs fn from c, NULL or why it failed a call */
static const char* run_side(GuestFn fn, CPU* c, uint64_t entry, CallLog* log) {
    log->count = 0;
    g_run.calls = g_run.imports = 0;
    g_run.log = log;
    if (__builtin_setjmp(g_run.escape)) return g_run.failure;
    fn(c, entry);
    return NULL;
}

static const char* compare_calls(const CallLog* a, const CallLog* b, char* why, size_t why_size) {
    for (unsigned i = 0; i < a->count && i < b->count; i++) {
        const LoggedCall *x = &a->calls[i], *y = &b->calls[i];
        if (x->target != y->target) {
            snprintf(why, why_size, "call %u: main+%llx, reconstruction main+%llx", i, (unsigned long long)x->target,
                     (unsigned long long)y->target);
            return why;
        }
        for (int r = 0; r < x->int_args; r++)
            if (x->x[r] != y->x[r]) {
                snprintf(why, why_size, "call %u to main+%llx: X%d %016llx, reconstruction %016llx", i,
                         (unsigned long long)x->target, r, (unsigned long long)x->x[r], (unsigned long long)y->x[r]);
                return why;
            }
        for (int r = 0; r < x->fp_args; r++)
            if (x->d[r] != y->d[r]) {
                snprintf(why, why_size, "call %u to main+%llx: D%d differs", i, (unsigned long long)x->target, r);
                return why;
            }
    }
    if (a->count != b->count) {
        snprintf(why, why_size, "%u stubbed calls, reconstruction %u", a->count, b->count);
        return why;
    }
    return NULL;
}

static const char* compare(const Case* k, const Outcome* a, const Outcome* b, const uint8_t* mem_a,
                           const uint8_t* mem_b, char* why, size_t why_size) {
    if (a->pc != RETURN_ADDRESS) return "the original did not return to X30";
    if (b->pc != RETURN_ADDRESS) return "the reconstruction did not return to X30";
    if (a->sp != b->sp) return "SP differs";
    for (int i = 0; i < 11; i++)
        if (a->saved[i] != b->saved[i]) {
            snprintf(why, why_size, "X%d differs", 19 + i);
            return why;
        }
    if (k->recon->ret == RECON_RET_W0 && (uint32_t)a->x0 != (uint32_t)b->x0) {
        snprintf(why, why_size, "W0 %08x, reconstruction %08x", (uint32_t)a->x0, (uint32_t)b->x0);
        return why;
    }
    if (k->recon->ret == RECON_RET_X0 && a->x0 != b->x0) {
        snprintf(why, why_size, "X0 %016llx, reconstruction %016llx", (unsigned long long)a->x0,
                 (unsigned long long)b->x0);
        return why;
    }
    if (k->recon->ret == RECON_RET_D0 && a->d0 != b->d0) {
        snprintf(why, why_size, "D0 %016llx, reconstruction %016llx", (unsigned long long)a->d0,
                 (unsigned long long)b->d0);
        return why;
    }
    for (uint64_t i = 0; i < SCRATCH_SIZE; i++)
        if (mem_a[i] != mem_b[i] && !(i >= STACK_TOP - DEAD_STACK && i < STACK_TOP)) {
            snprintf(why, why_size, "scratch byte +%llx: %02x, reconstruction %02x", (unsigned long long)i, mem_a[i],
                     mem_b[i]);
            return why;
        }
    return NULL;
}

int main(int argc, char** argv) {
    const char *main_path = NULL, *only = NULL, *results_path = NULL, *functions_path = NULL;
    unsigned runs = 20000;
    uint64_t seed = 0x5EED0A2CE05ull;
    int control = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--control")) control = 1;
        else if (!strcmp(argv[i], "--runs") && i + 1 < argc) runs = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--results") && i + 1 < argc) results_path = argv[++i];
        else if (!strcmp(argv[i], "--functions") && i + 1 < argc) functions_path = argv[++i];
        else main_path = argv[i];
    }
    if (!main_path) {
        fprintf(stderr, "usage: recon-verify MAIN.BIN [--functions TSV] [--runs N] [--seed S] [--only NAME] "
                        "[--results FILE] [--control]\n");
        return 2;
    }
    uint8_t* file = read_file(main_path, &g_image_size);
    if (!file) {
        fprintf(stderr, "cannot read %s\n", main_path);
        return 2;
    }
    const size_t scratch_off = (g_image_size + BSS_ROOM + 0xFFFF) & ~(size_t)0xFFFF;
    g_buffer = calloc(1, scratch_off + SCRATCH_SIZE + HOST_AREA);
    if (!g_buffer) return 2;
    memcpy(g_buffer, file, g_image_size);
    free(file);
    g_mem = (uint8_t*)((uintptr_t)g_buffer - (uintptr_t)MAIN_BASE); /* as integers: guest 0 is below the buffer */
    g_scratch = MAIN_BASE + scratch_off;
    const size_t relocations = relocate(g_buffer, g_image_size, MAIN_BASE);
    printf("main: %zu bytes at %llx, %zu relocations applied, scratch at %llx\n", g_image_size,
           (unsigned long long)MAIN_BASE, relocations, (unsigned long long)g_scratch);
    if (functions_path) {
        if (load_inventory(functions_path)) {
            fprintf(stderr, "cannot read %s\n", functions_path);
            return 2;
        }
        printf("inventory: %zu functions in main\n", g_inventory_count);
    }
    uint8_t* pristine = malloc(scratch_off);
    memcpy(pristine, g_buffer, scratch_off);

    static Case cases[MAX_CASES];
    size_t count = 0;
    for (size_t i = 0; i < recon_lua53_count && count < MAX_CASES; i++) {
        const ReconFunction* f = &recon_lua53_functions[i];
        if (only && strcmp(only, f->name)) continue;
        Case* k = &cases[count++];
        memset(k, 0, sizeof(*k));
        k->recon = f;
        for (size_t j = 0; j < recon_lua53_fixture_count; j++)
            if (!strcmp(recon_lua53_fixtures[j].name, f->name)) k->fixture = &recon_lua53_fixtures[j];
        for (size_t j = 0; j < recon_lua53_mutant_count; j++)
            if (!strcmp(recon_lua53_mutants[j].name, f->name)) k->mutant = recon_lua53_mutants[j].fn;
        for (size_t j = 0; j < recon_lua53_callee_count; j++)
            if (!strcmp(recon_lua53_callees[j].name, f->name)) k->plan = &recon_lua53_callees[j];
        find_closure(k);
    }

    char error[1024];
    LlvmSteps steps;
    if (llvm_steps_begin(&steps, 1, 0, error, sizeof(error))) {
        fprintf(stderr, "LLVM: %s\n", error);
        return 2;
    }
    for (size_t i = 0; i < g_compiled_count; i++) {
        Compiled* f = &g_compiled[i];
        if (analyze(f)) {
            fprintf(stderr, "%s: analysis failed\n", f->name);
            continue;
        }
        f->analyzed = 1;
        const SnxFunction* fn = snx_codegen_function(&f->codegen, MAIN_BASE + f->offset);
        if (!fn) {
            fprintf(stderr, "%s: no function at the entry\n", f->name);
            continue;
        }
        f->step = llvm_steps_add_function(&steps, &f->codegen, fn) + 1; /* 0: not compiled */
    }
    if (llvm_steps_load(&steps, error, sizeof(error))) {
        fprintf(stderr, "LLVM load: %s\n", error);
        return 2;
    }
    for (size_t i = 0; i < g_compiled_count; i++)
        if (g_compiled[i].step) g_compiled[i].fn = (GuestFn)(void (*)(void))steps.steps[g_compiled[i].step - 1];
    printf("%zu functions compiled, %zu imports\n", g_compiled_count, g_import_count);

    FILE* results = results_path ? fopen(results_path, "w") : NULL;
    if (results) fprintf(results, "name,main_offset,size,bytes_fnv1a,runs,result,detail,callees,callees_fnv1a\n");
    uint8_t* mem_before = malloc(SCRATCH_SIZE);
    uint8_t* mem_original = malloc(SCRATCH_SIZE);
    static CallLog log_original, log_recon;
    RtAPI rt;
    memset(&rt, 0, sizeof(rt));
    rt.undef = on_undef;
    rt.miss = on_miss;
    g_run.env.g.mem = g_mem;
    g_run.env.g.main_base = MAIN_BASE;
    g_run.env.lo = MAIN_BASE;
    g_run.env.hi = g_scratch + SCRATCH_SIZE + HOST_AREA;
    int failures = 0;
    for (size_t i = 0; i < count; i++) {
        Case* k = &cases[i];
        const uint64_t hash = fnv1a(g_buffer + k->recon->main_offset, k->recon->size);
        const char* failure = NULL;
        char why[256];
        unsigned done = 0, runs_calling = 0, runs_importing = 0;
        for (size_t j = 0; !k->error[0] && j < k->closure_count; j++)
            if (!find_compiled(k->closure[j])->fn)
                case_error(k, "main+%llx did not compile", (unsigned long long)k->closure[j]);
        if (k->error[0]) failure = k->error;
        else if (!k->fixture) failure = "no fixture";
        else if (control && !k->mutant) failure = "no mutant";
        const Guest guest = {g_mem, MAIN_BASE};
        Rng rng = {seed ^ fnv1a((const uint8_t*)k->recon->name, strlen(k->recon->name))};
        g_run.k = k;
        for (; !failure && done < runs; done++) {
            CPU c_original, c_recon;
            Rng regs = {rng_next(&rng) | 1};
            g_run.seed = mix(regs.state);
            prepare(&c_original, &rt, &regs);
            k->fixture->setup(&rng, &c_original, &guest, g_scratch, SCRATCH_SIZE);
            c_recon = c_original;
            uint8_t* scratch = g_mem + g_scratch;
            const uint64_t entry = MAIN_BASE + k->recon->main_offset;
            memcpy(mem_before, scratch, SCRATCH_SIZE);
            import_env_init(&g_run.env, g_scratch + SCRATCH_SIZE);
            const char* failed = run_side(k->original->fn, &c_original, entry, &log_original);
            if (failed) {
                snprintf(why, sizeof(why), "the original: %s", failed);
                failure = why;
                break;
            }
            runs_calling += g_run.calls != 0;
            runs_importing += g_run.imports != 0;
            memcpy(mem_original, scratch, SCRATCH_SIZE);
            memcpy(scratch, mem_before, SCRATCH_SIZE);
            import_env_init(&g_run.env, g_scratch + SCRATCH_SIZE);
            failed = run_side(control ? k->mutant : k->recon->fn, &c_recon, entry, &log_recon);
            if (failed) {
                snprintf(why, sizeof(why), "the reconstruction: %s", failed);
                failure = why;
                break;
            }
            const Outcome a = outcome(&c_original), b = outcome(&c_recon);
            failure = compare(k, &a, &b, mem_original, scratch, why, sizeof(why));
            if (!failure) failure = compare_calls(&log_original, &log_recon, why, sizeof(why));
        }
        if (!failure && memcmp(pristine, g_buffer, scratch_off)) failure = "the module image changed";
        const size_t callees = k->closure_count ? k->closure_count - 1 : 0;
        printf("%-24s main+%-8llx %4u bytes fnv1a %016llx  %s after %u runs", k->recon->name,
               (unsigned long long)k->recon->main_offset, k->recon->size, (unsigned long long)hash,
               failure ? "FAIL" : "PASS", done);
        if (callees || runs_calling)
            printf(" (%zu callees, %u runs calling out, %u reaching imports)", callees, runs_calling, runs_importing);
        printf("%s%s\n", failure ? ": " : "", failure ? failure : "");
        if (results) {
            char detail[256];
            snprintf(detail, sizeof(detail), "%s", failure ? failure : "");
            for (char* p = detail; *p; p++)
                if (*p == ',') *p = ';'; /* one CSV field */
            fprintf(results, "%s,%llx,%u,%016llx,%u,%s,%s,%zu,%016llx\n", k->recon->name,
                    (unsigned long long)k->recon->main_offset, k->recon->size, (unsigned long long)hash, done,
                    failure ? "fail" : "pass", detail, callees, (unsigned long long)(callees ? k->closure_hash : 0));
        }
        if (failure) failures++;
        memcpy(g_buffer, pristine, scratch_off);
    }
    if (results) fclose(results);
    llvm_steps_end(&steps);
    for (size_t i = 0; i < g_compiled_count; i++)
        if (g_compiled[i].analyzed) snx_codegen_free(&g_compiled[i].codegen);
    printf("%zu functions, %d failed%s\n", count, failures, control ? " (control: all must fail)" : "");
    if (control) return failures == (int)count ? 0 : 1;
    return failures ? 1 : 0;
}
