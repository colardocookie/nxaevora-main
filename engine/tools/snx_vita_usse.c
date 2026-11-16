/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* snx-vita-usse: a Vita shader corpus (the vita_shader_corpus setting's folder) translated again, as the runtime
   translates each program: how many translate, the first reason each of the others doesn't (grouped), and with
   --survey every instruction the programs hold that the translator lacks, by how many programs need it. --validate
   runs spirv-val (on PATH) on each translation, --out DIR keeps them as <hash>.spv (two builds' outputs, diffed, show a
   translator change kept the output) */
#include "vita/gpu/usse.h"
#include "common/utilities/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KINDS 256u
typedef struct {
    char name[96];
    unsigned programs, instructions;
} Count;
typedef struct {
    Count counts[KINDS];
    unsigned count;
} Counts;
static Count* count_of(Counts* c, const char* name) {
    for (unsigned i = 0; i < c->count; ++i)
        if (!strcmp(c->counts[i].name, name)) return &c->counts[i];
    if (c->count == KINDS) return NULL;
    Count* n = &c->counts[c->count++];
    snprintf(n->name, sizeof n->name, "%s", name);
    return n;
}
static int by_programs(const void* a, const void* b) {
    const Count *x = a, *y = b;
    return x->programs != y->programs ? (x->programs < y->programs) - (x->programs > y->programs)
                                      : strcmp(x->name, y->name);
}
static void print(const char* title, Counts* c) {
    qsort(c->counts, c->count, sizeof *c->counts, by_programs);
    if (c->count) printf("%s\n", title);
    for (unsigned i = 0; i < c->count; ++i)
        printf("  %5u programs %7u instructions  %s\n", c->counts[i].programs, c->counts[i].instructions,
               c->counts[i].name);
}
/* the survey of one program: each instruction it lacks, counted once for the program */
typedef struct {
    Counts* missing;
    Counts seen;
} Survey;
static void each(void* context, const char* name, bool known) {
    Survey* s = context;
    if (known) return;
    Count* c = count_of(&s->seen, name ? name : "(no pattern)");
    if (c) ++c->instructions;
}
/* the reason's text without the instruction's own bits: "VMAD2 (0480...) is not translated yet" to its name */
static void reason_kind(const char* error, char* out, size_t size) {
    const char* paren = strstr(error, " (");
    const char* yet = strstr(error, "is not translated yet");
    if (paren && yet)
        snprintf(out, size, "%.*s is not translated yet", (int)(paren - error), error);
    else
        snprintf(out, size, "%s", error);
}
int main(int argc, char** argv) {
    const char *folder = NULL, *out = NULL;
    bool survey = false, validate = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--survey")) survey = true;
        else if (!strcmp(argv[i], "--validate")) validate = true;
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (argv[i][0] != '-' && !folder) folder = argv[i];
        else {
            fprintf(stderr, "Usage: snx-vita-usse CORPUS [--survey] [--validate] [--out DIR]\n");
            return 2;
        }
    }
    if (!folder) {
        fprintf(stderr, "Usage: snx-vita-usse CORPUS [--survey] [--validate] [--out DIR]\n");
        return 2;
    }
    char** names = NULL;
    size_t name_count = 0;
    if (snx_list_dir(folder, &names, &name_count)) {
        fprintf(stderr, "snx-vita-usse: cannot list %s\n", folder);
        return 1;
    }
    if (out && snx_mkdirs(out)) {
        fprintf(stderr, "snx-vita-usse: cannot create %s\n", out);
        return 1;
    }
    static Counts reasons, missing;
    unsigned programs = 0, translated = 0, invalid = 0;
    for (size_t i = 0; i < name_count; ++i) {
        size_t length = strlen(names[i]);
        if (length < 5 || strcmp(names[i] + length - 4, ".gxp")) continue;
        char* path = snx_path_join(folder, names[i]);
        unsigned char* program = NULL;
        size_t size = 0;
        UsseHints hints;
        if (!path || !usse_corpus_read(path, &program, &size, &hints)) {
            fprintf(stderr, "snx-vita-usse: cannot read %s\n", path ? path : names[i]);
            free(path);
            continue;
        }
        ++programs;
        if (survey) {
            Survey s = {&missing, {.count = 0}};
            usse_survey(program, size, each, &s);
            for (unsigned k = 0; k < s.seen.count; ++k) {
                Count* c = count_of(&missing, s.seen.counts[k].name);
                if (c) ++c->programs, c->instructions += s.seen.counts[k].instructions;
            }
        }
        UsseShader shader;
        char error[256];
        if (usse_translate(program, size, &hints, true, &shader, error, sizeof error)) {
            ++translated;
            char *spv = NULL, *base = snx_format("%.*s", (int)(length - 4), names[i]);
            if (out || validate) spv = out ? snx_format("%s/%s.spv", out, base) : snx_format("%s.spv", path);
            if (spv && !snx_write_file(spv, shader.words, shader.count * 4) && validate) {
                const char* command[] = {"spirv-val", "--target-env", "vulkan1.3", spv, NULL};
                char* output = NULL;
                if (snx_run(command, NULL, NULL, &output)) {
                    ++invalid;
                    printf("%s: spirv-val refuses it: %s", names[i], output ? output : "\n");
                }
                free(output);
                if (!out) remove(spv);
            }
            free(spv);
            free(base);
            usse_shader_free(&shader);
        } else {
            char kind[256];
            reason_kind(error, kind, sizeof kind);
            Count* c = count_of(&reasons, kind);
            if (c) ++c->programs;
        }
        free(program);
        free(path);
    }
    snx_free_strings(names, name_count);
    printf("%u programs, %u translated", programs, translated);
    if (validate) printf(", %u refused by spirv-val", invalid);
    printf("\n");
    print("Why the others aren't:", &reasons);
    if (survey) print("Instructions the translator lacks:", &missing);
    return translated == programs && !invalid ? 0 : 1;
}
