/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* D3's fixed size answer (ShaderInfo.txq_fixed) on two programs of a captured corpus, which is game code and never in
   the repository: SNX_SHADER_CORPUS names its folder, and without it the test is skipped (77). Keyed for another
   slot the SPIR-V is byte for byte the unkeyed one. Keyed for the slot, only the scaling goes and the key's two
   constants come, and every other instruction reads the same, the width where the scaled width was read. */
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

/* A module read for the comparison: each instruction's offset, the instruction that defines each id, and each id's
   decorations summed into one number */
#define MOST (1u << 20)
typedef struct {
    const u32* w;
    u32 n, body; /* instructions, and the index of the first OpFunction */
    u32 *at, *def, bound;
    u64 *canon, *deco;
    u32* pos; /* a body instruction's place among those kept, plus 1 (0: removed) */
    u32 aligning; /* while the functions are aligned: the instruction looked at, plus 1 */
} Module;
static u64 mix(u64 h, u64 v) { return (h ^ v) * 0x100000001B3ull + 0x9E3779B97F4A7C15ull; }
/* the word of an instruction that holds its result id: 0 none, 1 or 2 */
static int result_word(u32 op) {
    switch (op) {
        case 0: case 2: case 3: case 4: case 5: case 6: case 8: case 10: case 14: case 15: case 16: case 17:
        case 39: case 56: case 62: case 63: case 71: case 72: case 74: case 75: case 99: case 218: case 219:
        case 224: case 225: case 246: case 247: case 249: case 250: case 251: case 252: case 253: case 254:
        case 255: case 317: case 330: case 331: case 332: case 4416: case 5380: case 5632: return 0;
        case 7: case 11: case 73: case 248: return 1;
        default: return op >= 19 && op <= 38 ? 1 : 2;
    }
}
/* whether word k of an instruction of op is a literal (else an id where it names one) */
static int literal(u32 op, u32 k) {
    switch (op) {
        case 7: case 11: case 21: case 22: case 71: case 72: return k >= 2;
        case 23: case 24: case 54: case 59: return k == 3;
        case 25: case 43: case 50: case 62: case 63: case 246: return k >= 3;
        case 32: return k == 2;
        case 61: case 81: return k >= 4;
        case 79: case 82: return k >= 5;
        case 12: return k == 4;
        case 247: return k >= 2;
        case 250: return k >= 4;
        case 251: return k >= 3 && (k - 3) % 2 == 0;
        case 87: case 88: case 91: case 92: case 95: case 98: return k == 5;
        case 89: case 90: case 93: case 94: case 96: case 97: return k == 6;
        case 99: return k == 4;
        default: return 0;
    }
}
static void module_read(Module* m, const u32* w, size_t words) {
    memset(m, 0, sizeof *m);
    m->w = w;
    m->bound = w[3];
    m->at = calloc(MOST, sizeof *m->at);
    m->def = calloc(m->bound, sizeof *m->def);
    m->canon = calloc(m->bound, sizeof *m->canon);
    m->deco = calloc(m->bound, sizeof *m->deco);
    m->pos = calloc(MOST, sizeof *m->pos);
    assert(m->at && m->def && m->canon && m->deco && m->pos);
    m->body = ~0u;
    for (size_t at = 5; at < words;) {
        const u32 count = w[at] >> 16, op = w[at] & 0xFFFF;
        assert(count && at + count <= words && m->n < MOST);
        if (op == 54 && m->body == ~0u) m->body = m->n;
        const int r = result_word(op);
        if (r && (u32)r < count) assert(w[at + r] < m->bound), m->def[w[at + r]] = m->n + 1;
        if ((op == 71 || op == 72) && count > 1 && w[at + 1] < m->bound) { /* decorations, in any order */
            u64 h = op;
            for (u32 k = 2; k < count; k++) h = mix(h, w[at + k]);
            m->deco[w[at + 1]] += h;
        }
        m->at[m->n++] = (u32)at;
        at += count;
    }
    assert(m->body != ~0u);
}
static void module_free(Module* m) { free(m->at), free(m->def), free(m->canon), free(m->deco), free(m->pos); }

/* The two names the query's results have: the scaled width and height without the key, its two constants with it */
#define NAME_X 0x5858585858585858ull
#define NAME_Y 0x5959595959595959ull
/* An id's name: in the functions, the place of the instruction that defines it among those kept, or for one of the
   scaling's two results X and Y. Before them, what it is, by its instruction's opcode, literals and operands' names,
   and its decorations. key: the keyed module's two values, whose constants are X and Y. */
static u64 name_of(Module* m, u32 id, const u32* key, const u32* fu, u32 nfu) {
    assert(id < m->bound && m->def[id]);
    const u32 i = m->def[id] - 1, at = m->at[i], op = m->w[at] & 0xFFFF, count = m->w[at] >> 16;
    if (i >= m->body) {
        if (m->pos[i]) return mix(0xB0D1ull, m->pos[i]);
        for (u32 q = 0; q < nfu; q++)
            if (fu[q] == i) return q == 0 ? NAME_X : q == 1 ? NAME_Y : mix(0xDEADull, i);
        if (m->aligning && i + 1 >= m->aligning) return 0xF0F0F0F0F0F0F0F0ull; /* not reached yet: a branch, a phi */
        return mix(0xDEADull, i); /* a removed instruction's result that is not one of the two */
    }
    if (m->canon[id]) return m->canon[id];
    if (key && op == 43 && count == 4 && (m->w[at + 3] == key[0] || m->w[at + 3] == key[1]))
        return m->canon[id] = m->w[at + 3] == key[0] ? NAME_X : NAME_Y;
    u64 h = mix(0xDEC1ull, op);
    const int r = result_word(op);
    for (u32 k = 1; k < count; k++) {
        if ((int)k == r) continue;
        const u32 v = m->w[at + k];
        h = !literal(op, k) && v < m->bound && m->def[v] ? mix(h, name_of(m, v, key, fu, nfu)) : mix(h, mix(1, v));
    }
    h = mix(h, m->deco[id]);
    return m->canon[id] = h | 1;
}
/* one function instruction, by its opcode and its operands' names */
static u64 instruction_name(Module* m, u32 i, const u32* key, const u32* fu, u32 nfu) {
    const u32 at = m->at[i], op = m->w[at] & 0xFFFF, count = m->w[at] >> 16;
    const int r = result_word(op);
    u64 h = mix(op, count);
    for (u32 k = 1; k < count; k++) {
        const u32 v = m->w[at + k];
        if ((int)k == r) h = mix(h, m->deco[v]);
        else h = !literal(op, k) && v < m->bound && m->def[v] ? mix(h, name_of(m, v, key, fu, nfu)) : mix(h, mix(1, v));
    }
    return h;
}

/* k, keyed, against a, unkeyed. Before the functions (types and constants are declared at first use, so they move),
   by opcode counts: fewer only of the scaling's, more only constants. In the functions, a's instructions that read
   unlike k's next are a's alone and may only be the scaling's. Then every kept one must read as k's does, operands
   named by what they are: the scaled width where a reads it is the key's width in k, the height likewise. */
#define OPS 8192
static void compare(const u32* a, size_t aw, const u32* k, size_t kw, const u32 key[2], unsigned* removed,
                    unsigned* added) {
    static int count_a[OPS], count_k[OPS];
    Module ma, mk;
    module_read(&ma, a, aw);
    module_read(&mk, k, kw);
#define OP(m, i) ((m).w[(m).at[i]] & 0xFFFF)
    memset(count_a, 0, sizeof count_a);
    memset(count_k, 0, sizeof count_k);
    for (u32 i = 0; i < ma.body; i++) assert(OP(ma, i) < OPS), count_a[OP(ma, i)]++;
    for (u32 j = 0; j < mk.body; j++) assert(OP(mk, j) < OPS), count_k[OP(mk, j)]++;
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
    /* Which of a's function instructions are not in k: in order, one that does not read as k's next does (its
       operands named as far as known, those not reached yet all alike) is a's alone, and may only be the scaling's */
    static u32 fu[MOST];
    u32 nfu = 0, i = ma.body, j = mk.body, kept = 0, run = 0;
    while (i < ma.n && j < mk.n) {
        ma.aligning = i + 1;
        mk.aligning = j + 1;
        if (OP(ma, i) == OP(mk, j) &&
            instruction_name(&ma, i, NULL, fu, nfu) == instruction_name(&mk, j, key, NULL, 0)) {
            ma.pos[i++] = ++kept;
            mk.pos[j++] = kept;
            run = 0;
            continue;
        }
        if (!scaling_op(OP(ma, i)))
            fprintf(stderr, "txq_variant: only without the key: opcode %u at %u\n", OP(ma, i), i);
        assert(scaling_op(OP(ma, i)) && ++run < 96);
        if (OP(ma, i) == 109) fu[nfu++] = i; /* OpConvertFToU: the scaling's results, the width first */
        i++, (*removed)++;
    }
    ma.aligning = mk.aligning = 0;
    assert(j == mk.n && "nothing in the keyed functions is new");
    for (; i < ma.n; i++, (*removed)++) assert(scaling_op(OP(ma, i)));
    assert(nfu == 2 && "the scaling's two results, width and height");
    /* the data flow: every kept instruction the same, by its operands' names */
    for (u32 x = ma.body, y = mk.body; x < ma.n; x++) {
        if (!ma.pos[x]) continue;
        while (y < mk.n && !mk.pos[y]) y++;
        assert(y < mk.n);
        const u64 na = instruction_name(&ma, x, NULL, fu, nfu), nk = instruction_name(&mk, y, key, NULL, 0);
        if (na != nk)
            fprintf(stderr, "txq_variant: the instructions at %u and %u differ (opcode %u), in what they read\n", x, y,
                    OP(ma, x));
        assert(na == nk);
        y++;
    }
#undef OP
    module_free(&ma);
    module_free(&mk);
}

/* A record of the ShaderInfo before txq_fixed, any padding of it not 0: it loads, the new fields 0, the rest as
   written. Where the old size has padding on the new field, the loader's zeroing after the copy is what holds. */
static void old_record(void) {
    const size_t align = _Alignof(ShaderInfo);
    const size_t old = (offsetof(ShaderInfo, txq_fixed) + align - 1) / align * align;
    static const char path[] = "txq_variant_old_record.snxs";
    const u32 header[5] = {1, (u32)old, ST_FS, 4, 4}; /* version, record size, stage, length, window */
    ShaderInfo written;
    unsigned char record[sizeof(ShaderInfo)];
    const unsigned char code[4] = {1, 2, 3, 4};
    assert(offsetof(ShaderInfo, txq_fixed) <= old && old < sizeof(ShaderInfo));
    memset(&written, 0, sizeof written);
    written.ntex = 3;
    memcpy(record, &written, sizeof record);
    memset(record + offsetof(ShaderInfo, txq_fixed), 0xA5, sizeof record - offsetof(ShaderInfo, txq_fixed));
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
            compare(a, aw, k, kw, want, &removed, &added);
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
