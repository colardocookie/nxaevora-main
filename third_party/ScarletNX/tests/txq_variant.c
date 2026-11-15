/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* D3's fixed size answer (ShaderInfo.txq_fixed). The comparison of a keyed module with an unkeyed one is held to
   controls built here, then run on two programs of the corpus SNX_SHADER_CORPUS names (game code, never in the
   repository), skipped (77) without it: keyed for another slot nothing changes, keyed for the slot only the
   scaling goes, the key's two constants come, and the whole module else reads the same. */
#include "nx/gpu/shader/shader.h"
#include <assert.h>
#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* what the scaling of a size query (spirv_translate.c scaled_coords) is made of in the function */
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

/* A module read for the comparison: each instruction's offset, the instruction that defines each id, and each id's
   decorations summed into one number */
#define MOST (1u << 20)
typedef struct {
    const u32* w;
    u32 n, body; /* instructions, and the index of the first OpFunction */
    u32 *at, *def, bound;
    u64 *canon, *deco;
    u32* pos;     /* a function instruction's place among those kept, plus 1 (0: not kept) */
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
/* whether word k of instruction i is a literal (else an id where the module defines one) */
static int literal(const Module* m, u32 i, u32 k) {
    const u32 at = m->at[i], op = m->w[at] & 0xFFFF;
    switch (op) {
        case 3: case 4: case 10: case 14: case 17: case 330: return 1;
        case 5: case 6: case 7: case 8: case 11: case 16: case 21: case 22: case 71: case 72: return k >= 2;
        case 15: { /* OpEntryPoint: the model, the function, a name of whole words, then the interface's ids */
            if (k == 1) return 1;
            if (k == 2) return 0;
            u32 end = 3;
            while (end < (m->w[at] >> 16)) {
                const u32 v = m->w[at + end++];
                if (!(v & 0xFF000000u) || !(v & 0xFF0000u) || !(v & 0xFF00u) || !(v & 0xFFu)) break;
            }
            return k < end;
        }
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
static int is_id(const Module* m, u32 i, u32 k) {
    const u32 v = m->w[m->at[i] + k];
    return !literal(m, i, k) && v < m->bound && m->def[v];
}
static void module_read(Module* m, const u32* w, size_t words) {
    memset(m, 0, sizeof *m);
    assert(words >= 5);
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
    if (m->body == ~0u) m->body = m->n;
}
static void module_free(Module* m) { free(m->at), free(m->def), free(m->canon), free(m->deco), free(m->pos); }

/* The two names the query's results have: the scaled width and height without the key, its two constants with it */
#define NAME_X 0x5858585858585858ull
#define NAME_Y 0x5959595959595959ull
typedef struct {
    const u32* key; /* the keyed module's two values, whose u32 constants are X and Y. NULL for the unkeyed one */
    const u32* fu;  /* the unkeyed module's removed scaling results, the width first */
    u32 nfu;
} Names;
/* An id's name: in the functions, the place of its instruction among those kept, or X and Y for the scaling's two
   results. Before them, what it is: opcode, literals, operands' names and decorations. */
static u64 name_of(Module* m, u32 id, const Names* nm) {
    assert(id < m->bound && m->def[id]);
    const u32 i = m->def[id] - 1, at = m->at[i], op = m->w[at] & 0xFFFF, count = m->w[at] >> 16;
    if (i >= m->body) {
        if (m->pos[i]) return mix(0xB0D1ull, m->pos[i]);
        for (u32 q = 0; q < nm->nfu; q++)
            if (nm->fu[q] == i) return q == 0 ? NAME_X : q == 1 ? NAME_Y : mix(0xDEADull, i);
        if (m->aligning && i + 1 >= m->aligning) return 0xF0F0F0F0F0F0F0F0ull; /* not reached yet: a branch, a phi */
        return mix(0xDEADull, i); /* a removed instruction's result that is not one of the two */
    }
    if (m->canon[id]) return m->canon[id];
    if (nm->key && op == 43 && count == 4 && m->w[at + 1] < m->bound && m->def[m->w[at + 1]]) {
        const u32 t = m->at[m->def[m->w[at + 1]] - 1];
        const int u32_type = (m->w[t] & 0xFFFF) == 21 && m->w[t + 2] == 32 && m->w[t + 3] == 0;
        if (u32_type && (m->w[at + 3] == nm->key[0] || m->w[at + 3] == nm->key[1]))
            return m->canon[id] = m->w[at + 3] == nm->key[0] ? NAME_X : NAME_Y;
    }
    u64 h = mix(0xDEC1ull, op);
    const int r = result_word(op);
    for (u32 k = 1; k < count; k++)
        if ((int)k != r) h = is_id(m, i, k) ? mix(h, name_of(m, m->w[at + k], nm)) : mix(h, mix(1, m->w[at + k]));
    h = mix(h, m->deco[id]);
    return m->canon[id] = h | 1;
}
/* one instruction by its opcode and operands' names. One declared before the functions with a result is its
   result's name. */
static u64 instruction_name(Module* m, u32 i, const Names* nm) {
    const u32 at = m->at[i], op = m->w[at] & 0xFFFF, count = m->w[at] >> 16;
    const int r = result_word(op);
    if (i < m->body && r && (u32)r < count) return name_of(m, m->w[at + r], nm);
    u64 h = mix(op, count);
    for (u32 k = 1; k < count; k++) {
        const u32 v = m->w[at + k];
        if ((int)k == r) h = mix(h, m->deco[v]);
        else h = is_id(m, i, k) ? mix(h, name_of(m, v, nm)) : mix(h, mix(1, v));
    }
    return h;
}
/* whether instruction i of m reads the id */
static int reads(const Module* m, u32 i, u32 id) {
    const u32 at = m->at[i], count = m->w[at] >> 16;
    const int r = result_word(m->w[at] & 0xFFFF);
    for (u32 k = 1; k < count; k++)
        if ((int)k != r && m->w[at + k] == id && is_id(m, i, k)) return 1;
    return 0;
}
static int u64_order(const void* x, const void* y) {
    const u64 a = *(const u64*)x, b = *(const u64*)y;
    return a < b ? -1 : a > b;
}

/* The keyed module k against the unkeyed a: 1 when only the scaling went and the key's constants came, 0 with a
   line saying what else differs. The functions in order (a's instructions that read unlike k's next are a's alone
   and may only be the scaling's), then every kept pair strictly, then all before the functions as named sets. */
static int compare(const u32* a, size_t aw, const u32* k, size_t kw, const u32 key[2], unsigned* removed,
                   unsigned* added) {
    Module ma, mk;
    static u32 fu[MOST];
    int ok = 1;
    *removed = *added = 0;
    if (a[0] != k[0] || a[1] != k[1] || a[2] != k[2] || a[4] != k[4]) {
        fprintf(stderr, "txq_variant: the headers differ\n");
        return 0;
    }
    module_read(&ma, a, aw);
    module_read(&mk, k, kw);
    Names na = {NULL, fu, 0}, nk = {key, NULL, 0};
#define OP(m, i) ((m).w[(m).at[i]] & 0xFFFF)
    u32 i = ma.body, j = mk.body, kept = 0, run = 0;
    while (ok && i < ma.n && j < mk.n) {
        ma.aligning = i + 1;
        mk.aligning = j + 1;
        if (OP(ma, i) == OP(mk, j) && instruction_name(&ma, i, &na) == instruction_name(&mk, j, &nk)) {
            ma.pos[i++] = ++kept;
            mk.pos[j++] = kept;
            run = 0;
            continue;
        }
        if (!scaling_op(OP(ma, i)) || ++run > 96) {
            fprintf(stderr, "txq_variant: only without the key: opcode %u at %u\n", OP(ma, i), i);
            ok = 0;
        }
        if (OP(ma, i) == 109) fu[na.nfu++] = i; /* OpConvertFToU: the scaling's results, the width first */
        i++, (*removed)++;
    }
    ma.aligning = mk.aligning = 0;
    for (; ok && i < ma.n; i++, (*removed)++)
        if (!scaling_op(OP(ma, i))) fprintf(stderr, "txq_variant: only without the key at the end\n"), ok = 0;
    if (ok && j < mk.n) fprintf(stderr, "txq_variant: an instruction of the keyed functions is new\n"), ok = 0;
    if (ok && na.nfu != 2) fprintf(stderr, "txq_variant: %u scaling results, not width and height\n", na.nfu), ok = 0;
    /* Every removed instruction is in the backward slice of the two scaled results, and no kept one reads any but
       those two: a removed instruction outside it, dead or not, is not the scaling's */
    unsigned char* slice = calloc(ma.n + 1, 1);
    u32* stack = calloc(ma.n + 1, sizeof *stack);
    assert(slice && stack);
    u32 depth = 0;
    for (u32 q = 0; ok && q < 2; q++) slice[fu[q]] = 1, stack[depth++] = fu[q];
    while (ok && depth) {
        const u32 e = stack[--depth], count = ma.w[ma.at[e]] >> 16;
        for (u32 w = 1; w < count; w++) {
            const u32 v = ma.w[ma.at[e] + w];
            if (!is_id(&ma, e, w) || (int)w == result_word(OP(ma, e)) || ma.def[v] - 1 < ma.body) continue;
            if (!slice[ma.def[v] - 1]) slice[ma.def[v] - 1] = 1, stack[depth++] = ma.def[v] - 1;
        }
    }
    for (u32 e = ma.body; ok && e < ma.n; e++) {
        if (ma.pos[e]) continue;
        if (!slice[e]) {
            fprintf(stderr, "txq_variant: a removed instruction (opcode %u at %u) is not in the scaled results' "
                    "slice\n", OP(ma, e), e);
            ok = 0;
            break;
        }
        const int r = result_word(OP(ma, e));
        for (u32 x = ma.body; ok && r && x < ma.n && e != fu[0] && e != fu[1]; x++)
            if (ma.pos[x] && reads(&ma, x, ma.w[ma.at[e] + r])) {
                fprintf(stderr, "txq_variant: a removed result (opcode %u at %u) is read by a kept instruction\n",
                        OP(ma, e), e);
                ok = 0;
            }
    }
    /* the declarations the removed instructions read, and what those read in turn: a's alone may only be these */
    unsigned char* owned = calloc(ma.bound + 1, 1);
    assert(owned);
    depth = 0;
    for (u32 e = ma.body; ok && e < ma.n; e++)
        if (!ma.pos[e]) stack[depth++] = e;
    while (ok && depth) {
        const u32 e = stack[--depth], count = ma.w[ma.at[e]] >> 16;
        for (u32 w = 1; w < count; w++) {
            const u32 v = ma.w[ma.at[e] + w];
            if (!is_id(&ma, e, w) || (int)w == result_word(OP(ma, e)) || ma.def[v] - 1 >= ma.body || owned[v]) continue;
            owned[v] = 1;
            stack[depth++] = ma.def[v] - 1;
        }
    }
    free(slice), free(stack);
    /* the data flow: every kept instruction reads the same, by its operands' names */
    for (u32 x = ma.body, y = mk.body; ok && x < ma.n; x++) {
        if (!ma.pos[x]) continue;
        while (y < mk.n && !mk.pos[y]) y++;
        if (y >= mk.n || instruction_name(&ma, x, &na) != instruction_name(&mk, y, &nk)) {
            fprintf(stderr, "txq_variant: the instructions at %u and %u differ (opcode %u), in what they read\n", x, y,
                    OP(ma, x));
            ok = 0;
        }
        y++;
    }
    /* Before the functions, as sets of names. a's alone may only be what nothing kept reads (the scaling's
       declarations) or a decoration of a removed result. k's alone only the key's two constants and a u32 type. */
    u64* sa = calloc(ma.body + 1, sizeof *sa);
    u64* sk = calloc(mk.body + 1, sizeof *sk);
    assert(sa && sk);
    for (u32 x = 0; ok && x < ma.body; x++) sa[x] = instruction_name(&ma, x, &na);
    for (u32 y = 0; ok && y < mk.body; y++) sk[y] = instruction_name(&mk, y, &nk);
    qsort(sa, ma.body, sizeof *sa, u64_order);
    qsort(sk, mk.body, sizeof *sk, u64_order);
    unsigned key_x = 0, key_y = 0;
    for (u32 x = 0, y = 0; ok && (x < ma.body || y < mk.body);) {
        if (x < ma.body && y < mk.body && sa[x] == sk[y]) {
            x++, y++;
            continue;
        }
        if (y >= mk.body || (x < ma.body && sa[x] < sk[y])) { /* a's alone: which, and does anything kept read it */
            u32 d = 0;
            while (d < ma.body && instruction_name(&ma, d, &na) != sa[x]) d++;
            assert(d < ma.body);
            const int r = result_word(OP(ma, d));
            const u32 target = ma.w[ma.at[d] + (r ? r : 1)];
            int still = 0;
            if (r && !owned[target]) {
                fprintf(stderr, "txq_variant: a declaration only without the key (opcode %u) is read by no removed "
                        "instruction\n", OP(ma, d));
                ok = 0;
            }
            if (r) { /* a declaration: read by a kept function instruction, or by a declaration that is not a's alone */
                for (u32 e = 0; e < ma.n && !still; e++) {
                    if (e == d || !reads(&ma, e, target) || (e >= ma.body && !ma.pos[e])) continue;
                    if (e < ma.body && (OP(ma, e) == 71 || OP(ma, e) == 72)) continue; /* its own decorations */
                    const u64 e_name = instruction_name(&ma, e, &na);
                    still = e >= ma.body || bsearch(&e_name, sk, mk.body, sizeof *sk, u64_order) != NULL;
                }
            } else { /* only a decoration or a name, and only of a function instruction that was removed */
                const u32 op = OP(ma, d);
                const int metadata = op == 5 || op == 6 || op == 71 || op == 72 || op == 332 || op == 5632;
                still = !metadata || !is_id(&ma, d, 1) || ma.def[target] - 1 < ma.body || ma.pos[ma.def[target] - 1];
            }
            if (still) {
                fprintf(stderr, "txq_variant: an instruction only without the key (opcode %u) %s\n", OP(ma, d),
                        r ? "is still read" : "is not a decoration or name of a removed instruction");
                ok = 0;
            }
            (*removed)++, x++;
            continue;
        }
        u32 d = 0; /* k's alone */
        while (d < mk.body && instruction_name(&mk, d, &nk) != sk[y]) d++;
        assert(d < mk.body);
        const u32 at = mk.at[d];
        const int u32_type = OP(mk, d) == 21 && mk.w[at + 2] == 32 && mk.w[at + 3] == 0;
        if (sk[y] == NAME_X) key_x++;
        else if (sk[y] == NAME_Y) key_y++;
        else if (!u32_type) {
            fprintf(stderr, "txq_variant: a declaration only with the key (opcode %u)\n", OP(mk, d));
            ok = 0;
        }
        (*added)++, y++;
    }
    if (ok && (key_x != 1 || key_y != 1))
        fprintf(stderr, "txq_variant: the key's constants are not both new\n"), ok = 0;
#undef OP
    free(sa), free(sk), free(owned);
    module_free(&ma);
    module_free(&mk);
    return ok;
}

/* Controls without a corpus: a module whose function scales a width and a height and subtracts them, the same
   keyed (the key's constants in the scaling's place), and keyed modules that must not pass */
static size_t emit(u32* m, size_t n, u32 op, int count, const u32* operands) {
    m[n++] = (u32)count << 16 | op;
    for (int q = 1; q < count; q++) m[n++] = operands[q - 1];
    return n;
}
#define E(op, ...)                                                                                                     \
    do {                                                                                                               \
        const u32 o_[] = {__VA_ARGS__};                                                                                \
        n = emit(m, n, op, (int)(sizeof o_ / sizeof o_[0]) + 1, o_);                                                   \
    } while (0)
#define E0(op) (m[n++] = 1u << 16 | (op))
enum { V_OK, V_SWAPPED, V_MODE, V_OPERAND, V_EXTRA, V_ONE_CONSTANT, V_DEAD, V_UNREAD, V_CAPABILITY };
/* ids: 1 void, 2 fn, 3 u32, 4 f32, 5 main, 6 width, 7 height, 8 half, 9 label, 10-15 the scaling, 16 the result,
   17 and 18 the key's constants, 19 an extra constant, 20 a dead OpFAdd and 21 an unread constant of the unkeyed
   module */
static size_t control(u32* m, int keyed, int variant) {
    size_t n = 5;
    const u32 header[5] = {0x07230203u, 0x00010300u, 0, 22, 0};
    memcpy(m, header, sizeof header);
    E(17, 1); /* OpCapability Shader */
    if (!keyed && variant == V_CAPABILITY) E(17, 11); /* OpCapability Int64: its literal is a removed result's id */
    E(14, 0, 1);                           /* OpMemoryModel Logical GLSL450 */
    E(15, 4, 5, 0x6E69616Du, 0);           /* OpEntryPoint Fragment %5 "main" */
    E(16, 5, variant == V_MODE ? 8u : 7u); /* OpExecutionMode %5 OriginUpperLeft (8: OriginLowerLeft) */
    if (!keyed) E(71, 11, 42);             /* OpDecorate %11 NoContraction */
    E(19, 1);
    E(33, 2, 1);
    E(21, 3, 32, 0);
    E(22, 4, 32);
    E(43, 3, 6, variant == V_OPERAND ? 3841u : 3840u);
    E(43, 3, 7, 2160);
    if (!keyed) E(43, 4, 8, 0x3F000000u);
    if (!keyed && variant == V_UNREAD) E(43, 3, 21, 77);
    if (keyed) {
        E(43, 3, 17, 1234);
        if (variant != V_ONE_CONSTANT) E(43, 3, 18, 567);
        if (variant == V_EXTRA) E(43, 3, 19, 99);
    }
    E(54, 1, 5, 0, 2);
    E(248, 9);
    if (!keyed) {
        E(112, 4, 10, 6); /* the width: OpConvertUToF, OpFMul by one half, OpConvertFToU */
        E(133, 4, 11, 10, 8);
        E(109, 3, 12, 11);
        E(112, 4, 13, 7); /* the height */
        E(133, 4, 14, 13, 8);
        E(109, 3, 15, 14);
        if (variant == V_DEAD) E(129, 4, 20, 10, 8); /* OpFAdd, read by nothing */
        E(130, 3, 16, 12, 15);                       /* OpISub width, height */
    } else if (variant == V_ONE_CONSTANT)
        E(130, 3, 16, 17, 17);
    else
        E(130, 3, 16, variant == V_SWAPPED ? 18u : 17u, variant == V_SWAPPED ? 17u : 18u);
    E0(253); /* OpReturn */
    E0(56);  /* OpFunctionEnd */
    return n;
}
static void controls(void) {
    static u32 a[256], k[256];
    static const struct {
        int unkeyed, keyed, variant, passes; /* the unkeyed module's variant, and the keyed one's */
        const char* what;
    } cases[] = {{V_OK, 1, V_OK, 1, "the key in the scaling's place"},
                 {V_OK, 1, V_SWAPPED, 0, "width and height swapped"},
                 {V_OK, 1, V_MODE, 0, "an execution mode's literal changed"},
                 {V_OK, 1, V_OPERAND, 0, "an unrelated constant changed"},
                 {V_OK, 1, V_EXTRA, 0, "an unrelated constant added"},
                 {V_OK, 0, V_OK, 0, "the key without effect (another slot keyed)"},
                 {V_OK, 1, V_ONE_CONSTANT, 0, "one of the key's constants missing"},
                 {V_DEAD, 1, V_OK, 0, "a dead instruction outside the scaling removed too"},
                 {V_UNREAD, 1, V_OK, 0, "an unread declaration outside the scaling removed too"},
                 {V_CAPABILITY, 1, V_OK, 0, "a capability whose literal equals a removed id removed too"}};
    const u32 key[2] = {1234, 567};
    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        const size_t aw = control(a, 0, cases[c].unkeyed);
        const size_t kw = control(k, cases[c].keyed, cases[c].variant);
        unsigned removed, added;
        fprintf(stderr, "txq_variant: control, %s, must %s:\n", cases[c].what, cases[c].passes ? "pass" : "fail");
        const int passed = compare(a, aw, k, kw, key, &removed, &added);
        if (passed != cases[c].passes) fprintf(stderr, "txq_variant: control '%s' went the other way\n", cases[c].what);
        assert(passed == cases[c].passes);
    }
    puts("txq_variant: controls ok (one passes, nine fail as they must)");
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
           fwrite(record, 1, old, f) == old && fwrite(code, 1, sizeof code, f) == sizeof code && !fclose(f));
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
    controls();
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
            unsigned removed, added;
            const int same = compare(a, aw, k, kw, keyed + 1, &removed, &added);
            assert(same && "only the scaling goes and the key's constants come");
            assert(removed >= 10 && "these two programs scale the query without the key");
            printf("txq_variant: %016llx handle 0x%X: %zu words, keyed %zu: %u instructions gone (the scaling and its "
                   "declarations), %u declarations new (the key's constants)\n", hash, programs[p].handle, aw, kw,
                   removed, added);
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
