/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* D3's fixed size answer (ShaderInfo.txq_fixed) on two programs of a captured corpus, which is game code and never in
   the repository: SNX_SHADER_CORPUS names its folder, and without it the test is skipped (77). Keyed for another
   slot the SPIR-V is byte for byte the unkeyed one. Keyed for the slot, the query answers the key's two constants,
   and the only instructions that go are those of its scaling. */
#include "nx/gpu/shader/shader.h"
#include <assert.h>
#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What the scaling of a size query (spirv_translate.c scaled_coords) is made of: in the function, these, and before
   it a NoContraction decoration each, the pointer type to the frame's constants and its constants */
static int scaling_op(unsigned op) {
    static const unsigned ops[] = {61,  /* OpLoad */
                                   65,  /* OpAccessChain */
                                   81,  /* OpCompositeExtract */
                                   109, /* OpConvertFToU */
                                   112, /* OpConvertUToF */
                                   129, /* OpFAdd */
                                   133, /* OpFMul */
                                   169, /* OpSelect */
                                   171, /* OpINotEqual */
                                   194, /* OpShiftRightLogical */
                                   199 /* OpBitwiseAnd */};
    for (size_t k = 0; k < sizeof ops / sizeof ops[0]; k++)
        if (op == ops[k]) return 1;
    return 0;
}
static int scaling_declaration(unsigned op) { return op == 32 || op == 43 || op == 71; }
/* the 32 bit constants the scaling declares: one half and one as floats, and the components and bits it picks */
static int scaling_constant(u32 value) { return value == 0x3F000000u || value == 0x3F800000u || value <= 3; }

/* the 32 bit constants of one module that the other does not have as often: 1 when each of a's is the scaling's
   and each of b's is one of want's two values, counted into extra */
static int constants_differ(const u32* a, size_t aw, const u32* b, size_t bw, const u32 want[2], unsigned* extra) {
    static u32 va[1 << 16], vb[1 << 16];
    size_t na = 0, nb = 0;
    for (size_t at = 5; at < aw; at += a[at] >> 16)
        if ((a[at] & 0xFFFF) == 43 && a[at] >> 16 == 4) assert(na < 1 << 16), va[na++] = a[at + 3];
    for (size_t at = 5; at < bw; at += b[at] >> 16)
        if ((b[at] & 0xFFFF) == 43 && b[at] >> 16 == 4) assert(nb < 1 << 16), vb[nb++] = b[at + 3];
    int ok = 1;
    *extra = 0;
    for (size_t i = 0; i < na; i++) { /* a's left once b's matches are taken out */
        size_t j = 0;
        while (j < nb && vb[j] != va[i]) j++;
        if (j < nb) vb[j] = vb[--nb];
        else if (!scaling_constant(va[i])) {
            fprintf(stderr, "txq_variant: a constant only without the key: 0x%08x\n", va[i]);
            ok = 0;
        }
    }
    for (size_t j = 0; j < nb; j++) {
        if (vb[j] != want[0] && vb[j] != want[1]) {
            fprintf(stderr, "txq_variant: a constant only with the key: 0x%08x\n", vb[j]);
            ok = 0;
        }
        (*extra)++;
    }
    return ok && *extra <= 2;
}

/* the module's instructions as their first words (count and opcode), from word 5 on */
static size_t instructions(const u32* spv, size_t words, u32* out, size_t room) {
    size_t n = 0;
    for (size_t at = 5; at < words && n < room;) {
        const u32 count = spv[at] >> 16;
        assert(count && at + count <= words);
        out[n++] = at;
        at += count;
    }
    return n;
}

/* whether the module defines a 32 bit integer constant of this value */
static int has_constant(const u32* spv, size_t words, u32 value) {
    for (size_t at = 5; at < words; at += spv[at] >> 16)
        if ((spv[at] & 0xFFFF) == 43 && spv[at] >> 16 == 4 && spv[at + 3] == value) return 1;
    return 0;
}

static u32* translate(const u8* code, u32 max_bytes, int stage, const ShaderInfo* info, const u32 txq[3],
                      size_t* words) {
    ShaderInfo si = *info;
    memcpy(si.txq_fixed, txq, sizeof si.txq_fixed);
    u8* copy = malloc(max_bytes);
    assert(copy);
    memcpy(copy, code, max_bytes);
    u32* spv = shader_to_spirv(copy, max_bytes, stage, &si, words, 1, 1, 1, 1);
    free(copy);
    assert(spv && "the program translates");
    return spv;
}

/* The keyed module against the unkeyed one. Before the first function, where types and constants are declared when
   first used and so may move, by the count of each opcode: fewer only of the scaling's, more only constants. In
   the functions, in order: two cursors, a run of differences taken by the shortest skip that agrees again, and
   what is skipped may only be the scaling's, and only on the unkeyed side. */
#define OPS 8192
static void compare(const u32* a, size_t aw, const u32* k, size_t kw, unsigned* removed, unsigned* added) {
    static u32 ia[1 << 20], ik[1 << 20];
    static int count_a[OPS], count_k[OPS];
    const size_t na = instructions(a, aw, ia, 1 << 20), nk = instructions(k, kw, ik, 1 << 20);
#define OP(spv, at) ((spv)[at] & 0xFFFF)
    size_t fa = 0, fk = 0;
    while (fa < na && OP(a, ia[fa]) != 54) fa++; /* OpFunction */
    while (fk < nk && OP(k, ik[fk]) != 54) fk++;
    memset(count_a, 0, sizeof count_a);
    memset(count_k, 0, sizeof count_k);
    for (size_t i = 0; i < fa; i++) assert(OP(a, ia[i]) < OPS), count_a[OP(a, ia[i])]++;
    for (size_t j = 0; j < fk; j++) assert(OP(k, ik[j]) < OPS), count_k[OP(k, ik[j])]++;
    *removed = *added = 0;
    for (unsigned op = 0; op < OPS; op++) {
        if (count_a[op] > count_k[op]) {
            if (!scaling_declaration(op)) fprintf(stderr, "txq_variant: fewer declarations of opcode %u\n", op);
            assert(scaling_declaration(op));
            *removed += (unsigned)(count_a[op] - count_k[op]);
        } else if (count_k[op] > count_a[op]) {
            if (op != 43) fprintf(stderr, "txq_variant: more declarations of opcode %u\n", op);
            assert(op == 43);
            *added += (unsigned)(count_k[op] - count_a[op]);
        }
    }
    size_t i = fa, j = fk;
    while (i < na && j < nk) {
        if (OP(a, ia[i]) == OP(k, ik[j])) {
            i++, j++;
            continue;
        }
        size_t skip = 0;
        while (i + skip < na && OP(a, ia[i + skip]) != OP(k, ik[j])) {
            if (!scaling_op(OP(a, ia[i + skip])))
                fprintf(stderr, "txq_variant: only without the key: opcode %u\n", OP(a, ia[i + skip]));
            assert(scaling_op(OP(a, ia[i + skip])) && skip < 96);
            skip++;
        }
        *removed += (unsigned)skip;
        i += skip;
    }
    assert(j == nk && "nothing in the keyed functions is new");
    for (; i < na; i++, (*removed)++) assert(scaling_op(OP(a, ia[i])));
#undef OP
}

/* A record of the ShaderInfo before txq_fixed, any padding of it not 0: it loads, the new fields 0, the rest as
   written. Where the old size has padding on the new field, the loader's zeroing after the copy is what holds. */
static void old_record(void) {
    const size_t align = _Alignof(ShaderInfo);
    const size_t old = (offsetof(ShaderInfo, txq_fixed) + align - 1) / align * align;
    static const char path[] = "txq_variant_old_record.snxs";
    const u32 header[5] = {1, (u32)old, ST_FS, 4, 4}; /* version, record size, stage, length, window */
    unsigned char record[sizeof(ShaderInfo)];
    const unsigned char code[4] = {1, 2, 3, 4};
    assert(offsetof(ShaderInfo, txq_fixed) <= old && old < sizeof(ShaderInfo));
    memset(record, 0xA5, sizeof record);
    memset(record, 0, offsetof(ShaderInfo, txq_fixed));
    ((ShaderInfo*)record)->ntex = 3;
    FILE* f = fopen(path, "wb");
    assert(f && fwrite("SNXS", 1, 4, f) == 4 && fwrite(header, 1, sizeof header, f) == sizeof header &&
           fwrite(record, 1, old, f) == old &&
           fwrite(code, 1, sizeof code, f) == sizeof code && !fclose(f));
    u32 max_bytes;
    int stage;
    ShaderInfo info;
    memset(&info, 0x5A, sizeof info);
    u8* loaded = shader_corpus_load(path, &max_bytes, &stage, &info);
    remove(path);
    assert(loaded && stage == ST_FS && max_bytes == 4 && !memcmp(loaded, code, 4));
    assert(info.ntex == 3 && !info.txq_fixed[0] && !info.txq_fixed[1] && !info.txq_fixed[2]);
    free(loaded);
}

int main(void) {
    old_record();
    static const struct {
        unsigned long long hash;
        u32 handle; /* the scene colour's handle offset, in words */
    } programs[2] = {{0x21fbfbecbf90f4a3ull, 0x2A}, {0x8a160a27d90af72dull, 0x36}};
    const char* dir = getenv("SNX_SHADER_CORPUS");
    if (!dir || !dir[0]) {
        puts("txq_variant: skipped, SNX_SHADER_CORPUS names no corpus");
        return 77;
    }
    g_shader_texture_arrays = 1; /* as tools/snx_shaderc.c translates a corpus */
    g_shader_maximal_reconvergence = 1;
    DIR* folder = opendir(dir);
    assert(folder && "the corpus folder opens");
    int seen = 0;
    struct dirent* entry;
    while ((entry = readdir(folder))) {
        const size_t len = strlen(entry->d_name);
        if (len < 6 || strcmp(entry->d_name + len - 5, ".snxs")) continue;
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", dir, entry->d_name);
        u32 max_bytes, hlen;
        int stage;
        ShaderInfo info;
        u8* code = shader_corpus_load(path, &max_bytes, &stage, &info);
        if (!code) continue;
        const unsigned long long hash = shader_hash(code, max_bytes, &hlen);
        for (int p = 0; p < 2; p++) {
            if (hash != programs[p].hash || stage != ST_FS) continue;
            const u32 none[3] = {0, 0, 0}, other[3] = {programs[p].handle + 1 + 7, 1234, 567},
                      keyed[3] = {programs[p].handle + 1, 1234, 567};
            size_t aw, ow, kw;
            u32* a = translate(code, max_bytes, stage, &info, none, &aw);
            u32* o = translate(code, max_bytes, stage, &info, other, &ow);
            u32* k = translate(code, max_bytes, stage, &info, keyed, &kw);
            assert(ow == aw && !memcmp(o, a, aw * sizeof *a) && "a key for another slot changes nothing");
            assert(!(kw == aw && !memcmp(k, a, aw * sizeof *a)) && "the key for the slot changes the module");
            assert(has_constant(k, kw, 1234) && has_constant(k, kw, 567));
            assert(!has_constant(a, aw, 1234) && !has_constant(a, aw, 567));
            unsigned removed, added, extra;
            const u32 want[2] = {1234, 567};
            compare(a, aw, k, kw, &removed, &added);
            assert(constants_differ(a, aw, k, kw, want, &extra) && extra == 2 && "the key's two values, and no other");
            assert(removed >= 10 && "these two programs scale the query without the key");
            printf("txq_variant: %016llx handle 0x%X: %zu words, keyed %zu: %u instructions of the scaling gone, %u "
                   "constants added net, the key's two among them\n", hash, programs[p].handle, aw, kw, removed, added);
            free(a), free(o), free(k);
            seen |= 1 << p;
        }
        free(code);
    }
    closedir(folder);
    assert(seen == 3 && "both programs are in the corpus");
    puts("txq_variant: ok");
    return 0;
}
