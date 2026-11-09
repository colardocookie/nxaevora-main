/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Differential verifier: each original, compiled from the relocated main module by ScarletNX's LLVM backend, and its
   reconstruction run from the same inputs and must leave the same results, registers and memory (README.md).
     recon-verify MAIN.BIN [--runs N] [--seed S] [--only NAME] [--results FILE] [--control]
   --control pairs each original with a wrong variant of its reconstruction (mutants_lua53.c): all must fail. */
#include "llvm_steps.h"
#include "nx/loader/codegen_view.h"
#include "recon/registry.h"
#include "fixtures.h"
#include "mutants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAIN_BASE 0x8005000ull     /* where the runtime maps main for this game (functions.tsv uses it too) */
#define BSS_ROOM (64ull << 20)     /* zeroed room after the image for its BSS */
#define SCRATCH_SIZE (1ull << 20)  /* fixture memory, compared byte for byte */
#define RETURN_ADDRESS 0x1000ull   /* X30 on entry: a RET must land here */
#define R_AARCH64_RELATIVE 1027

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

static uint64_t fnv1a(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

typedef struct {
    const ReconFunction* recon;
    const Fixture* fixture;
    GuestFn mutant;
    SnxCodegen codegen;
    SnxProgram program;
    SnxModuleImage module;
    SnxFde fde;
    size_t step;
    int compiled;
} Case;

/* The original's bytes as a one-function module on their own page, so the code generator sees the real pcs */
static int analyze(Case* k) {
    const uint64_t off = k->recon->main_offset, page = off & ~0xFFFull;
    memset(&k->program, 0, sizeof(k->program));
    memset(&k->module, 0, sizeof(k->module));
    k->module.name = (char*)k->recon->name;
    k->module.image = g_buffer + page;
    k->module.image_size = (size_t)(off + k->recon->size - page);
    k->module.base = MAIN_BASE + page;
    k->module.size = (k->module.image_size + 0xFFF) & ~0xFFFull;
    k->module.text_size = (SnxU32)k->module.image_size;
    k->fde.start = off - page;
    k->fde.size = k->recon->size;
    k->module.fde = &k->fde;
    k->module.fde_count = 1;
    k->program.modules = &k->module;
    k->program.module_count = 1;
    k->program.npdm.title_id = 0x01001F5010DFA000ull;
    k->program.npdm.stack = 0x100000;
    k->program.npdm.prio = 44;
    k->program.npdm.core = 0;
    return snx_nx_codegen_analyze(&k->codegen, &k->program, NULL, 0, 1);
}

typedef struct {
    uint64_t x0, sp, pc, saved[11];
} Outcome;

static void prepare(CPU* c, RtAPI* rt, Rng* r) {
    memset(c, 0, sizeof(*c));
    c->mem = g_mem;
    c->rt = rt;
    for (int i = 0; i < 31; i++) c->x[i] = rng_next(r); /* garbage in every register an argument does not set */
    for (int i = 19; i <= 29; i++) c->x[i] = 0xC0DE000000000000ull | (uint64_t)i;
    c->x[30] = RETURN_ADDRESS;
    c->x[31] = g_scratch + SCRATCH_SIZE - 0x1000;
}

static Outcome outcome(const CPU* c) {
    Outcome o;
    o.x0 = c->x[0];
    o.sp = c->x[31];
    o.pc = c->pc;
    for (int i = 0; i < 11; i++) o.saved[i] = c->x[19 + i];
    return o;
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
    for (uint64_t i = 0; i < SCRATCH_SIZE; i++)
        if (mem_a[i] != mem_b[i]) {
            snprintf(why, why_size, "scratch byte +%llx: %02x, reconstruction %02x", (unsigned long long)i, mem_a[i],
                     mem_b[i]);
            return why;
        }
    return NULL;
}

int main(int argc, char** argv) {
    const char *main_path = NULL, *only = NULL, *results_path = NULL;
    unsigned runs = 20000;
    uint64_t seed = 0x5EED0A2CE05ull;
    int control = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--control")) control = 1;
        else if (!strcmp(argv[i], "--runs") && i + 1 < argc) runs = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--results") && i + 1 < argc) results_path = argv[++i];
        else main_path = argv[i];
    }
    if (!main_path) {
        fprintf(stderr, "usage: recon-verify MAIN.BIN [--runs N] [--seed S] [--only NAME] [--results FILE]\n");
        return 2;
    }
    uint8_t* file = read_file(main_path, &g_image_size);
    if (!file) {
        fprintf(stderr, "cannot read %s\n", main_path);
        return 2;
    }
    const size_t scratch_off = (g_image_size + BSS_ROOM + 0xFFFF) & ~(size_t)0xFFFF;
    g_buffer = calloc(1, scratch_off + SCRATCH_SIZE);
    if (!g_buffer) return 2;
    memcpy(g_buffer, file, g_image_size);
    free(file);
    g_mem = (uint8_t*)((uintptr_t)g_buffer - (uintptr_t)MAIN_BASE); /* as integers: guest 0 is below the buffer */
    g_scratch = MAIN_BASE + scratch_off;
    const size_t relocations = relocate(g_buffer, g_image_size, MAIN_BASE);
    printf("main: %zu bytes at %llx, %zu relocations applied, scratch at %llx\n", g_image_size,
           (unsigned long long)MAIN_BASE, relocations, (unsigned long long)g_scratch);
    uint8_t* pristine = malloc(scratch_off);
    memcpy(pristine, g_buffer, scratch_off);

    Case cases[64];
    size_t count = 0;
    for (size_t i = 0; i < recon_lua53_count && count < 64; i++) {
        const ReconFunction* f = &recon_lua53_functions[i];
        if (only && strcmp(only, f->name)) continue;
        cases[count].recon = f;
        cases[count].fixture = NULL;
        for (size_t j = 0; j < recon_lua53_fixture_count; j++)
            if (!strcmp(recon_lua53_fixtures[j].name, f->name)) cases[count].fixture = &recon_lua53_fixtures[j];
        cases[count].mutant = NULL;
        for (size_t j = 0; j < recon_lua53_mutant_count; j++)
            if (!strcmp(recon_lua53_mutants[j].name, f->name)) cases[count].mutant = recon_lua53_mutants[j].fn;
        cases[count].compiled = 0;
        count++;
    }

    char error[1024];
    LlvmSteps steps;
    if (llvm_steps_begin(&steps, 1, 0, error, sizeof(error))) {
        fprintf(stderr, "LLVM: %s\n", error);
        return 2;
    }
    for (size_t i = 0; i < count; i++) {
        Case* k = &cases[i];
        if (analyze(k)) {
            fprintf(stderr, "%s: analysis failed\n", k->recon->name);
            continue;
        }
        const SnxFunction* fn = snx_codegen_function(&k->codegen, MAIN_BASE + k->recon->main_offset);
        if (!fn) {
            fprintf(stderr, "%s: no function at the entry\n", k->recon->name);
            continue;
        }
        k->step = llvm_steps_add_function(&steps, &k->codegen, fn);
        k->compiled = 1;
    }
    if (llvm_steps_load(&steps, error, sizeof(error))) {
        fprintf(stderr, "LLVM load: %s\n", error);
        return 2;
    }

    FILE* results = results_path ? fopen(results_path, "w") : NULL;
    if (results) fprintf(results, "name,main_offset,size,bytes_fnv1a,runs,result,detail\n");
    uint8_t* mem_before = malloc(SCRATCH_SIZE);
    uint8_t* mem_original = malloc(SCRATCH_SIZE);
    RtAPI rt;
    memset(&rt, 0, sizeof(rt));
    rt.undef = on_undef;
    int failures = 0;
    for (size_t i = 0; i < count; i++) {
        Case* k = &cases[i];
        const uint64_t hash = fnv1a(g_buffer + k->recon->main_offset, k->recon->size);
        const char* failure = NULL;
        char why[256];
        unsigned done = 0;
        if (!k->compiled) failure = "the original did not compile";
        else if (!k->fixture) failure = "no fixture";
        else if (control && !k->mutant) failure = "no mutant";
        const GuestFn original = k->compiled ? (GuestFn)(void (*)(void))steps.steps[k->step] : NULL;
        const Guest guest = {g_mem, MAIN_BASE};
        Rng rng = {seed ^ fnv1a((const uint8_t*)k->recon->name, strlen(k->recon->name))};
        for (; !failure && done < runs; done++) {
            CPU c_original, c_recon;
            Rng regs = {rng_next(&rng) | 1};
            prepare(&c_original, &rt, &regs);
            k->fixture->setup(&rng, &c_original, &guest, g_scratch, SCRATCH_SIZE);
            c_recon = c_original;
            uint8_t* scratch = g_mem + g_scratch;
            memcpy(mem_before, scratch, SCRATCH_SIZE);
            original(&c_original, MAIN_BASE + k->recon->main_offset);
            memcpy(mem_original, scratch, SCRATCH_SIZE);
            memcpy(scratch, mem_before, SCRATCH_SIZE);
            const GuestFn recon = control ? k->mutant : k->recon->fn;
            recon(&c_recon, MAIN_BASE + k->recon->main_offset);
            const Outcome a = outcome(&c_original), b = outcome(&c_recon);
            failure = compare(k, &a, &b, mem_original, scratch, why, sizeof(why));
        }
        if (!failure && memcmp(pristine, g_buffer, scratch_off)) failure = "the module image changed";
        printf("%-24s main+%-8llx %4u bytes fnv1a %016llx  %s after %u runs%s%s\n", k->recon->name,
               (unsigned long long)k->recon->main_offset, k->recon->size, (unsigned long long)hash,
               failure ? "FAIL" : "PASS", done, failure ? ": " : "", failure ? failure : "");
        if (results)
            fprintf(results, "%s,%llx,%u,%016llx,%u,%s,%s\n", k->recon->name, (unsigned long long)k->recon->main_offset,
                    k->recon->size, (unsigned long long)hash, done, failure ? "fail" : "pass", failure ? failure : "");
        if (failure) failures++;
        memcpy(g_buffer, pristine, scratch_off);
    }
    if (results) fclose(results);
    llvm_steps_end(&steps);
    for (size_t i = 0; i < count; i++)
        if (cases[i].compiled) snx_codegen_free(&cases[i].codegen);
    printf("%zu functions, %d failed%s\n", count, failures, control ? " (control: all must fail)" : "");
    if (control) return failures == (int)count ? 0 : 1;
    return failures ? 1 : 0;
}
