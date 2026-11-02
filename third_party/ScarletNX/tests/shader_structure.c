/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The structured control flow of gpu/shader/structure.c, held to the program's own: random programs of branches,
   conditional branches, EXITs and blocks that run into the next, each walked many times with random branch outcomes,
   once through its graph and once through the structure's tokens read the way spirv_translate.c writes them (scopes
   as loop constructs, the pending exit tested at each merge it may reach). Both walks must visit the same blocks in the
   same order, and every reducible program must be structured. */
#include "nx/gpu/shader/translator.h"

#define SCHED 0x001F8000FC0007E0ull
#define EXIT  0xE30000000007000Full
#define NOP   0x50B0000000070F00ull
#define MAXW  400
#define STEPS 2000

enum { T_FALL, T_GOTO, T_COND, T_EXIT };

static struct {
    int failures;
    u32 rng; /* the random programs' and walks' generator */
} g_test = {0, 12345};
static void check(int ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        g_test.failures++;
    }
}

static u32 rnd(u32 n) {
    g_test.rng = g_test.rng * 1103515245u + 12345u;
    return (g_test.rng >> 8) % n;
}

static u64 flow(u64 top12, u32 at, u32 to) {
    return top12 << 52 | (u64)(((to - (at + 1)) * 8) & 0xFFFFFF) << 20 | 0x7000Full;
}

typedef struct {
    u64 insn[MAXW];
    u8 leader[MAXW];
    u32 n, nb, start[64], term[64], target[64];
} Program;

static u32 next_slot(u32 i) {
    i++;
    while (!IDX_OK(i)) i++;
    return i;
}

/* nb blocks of one to three instructions, the last a flow instruction or a NOP that runs into the next block */
static void make_program(Program* p, u32 nb) {
    u32 i = 11, b;
    memset(p, 0, sizeof *p);
    p->insn[10] = SCHED;
    p->nb = nb;
    for (b = 0; b < nb; b++) {
        u32 extra = rnd(3);
        p->start[b] = i;
        while (extra--) {
            p->insn[i] = NOP;
            i = next_slot(i);
        }
        p->term[b] = rnd(10) < 3 ? T_COND : rnd(10) < 4 ? T_GOTO : rnd(10) < 2 ? T_EXIT : T_FALL;
        if (b == nb - 1 && p->term[b] != T_GOTO) p->term[b] = T_EXIT;
        p->target[b] = rnd(nb);
        p->insn[i] = NOP; /* the flow word, written once the blocks' words are known */
        i = next_slot(i);
    }
    p->n = i;
    for (u32 k = 10; k < p->n; k += 4) p->insn[k] = SCHED;
    for (b = 0; b < nb; b++) {
        u32 last = p->start[b];
        while (next_slot(last) < (b + 1 < nb ? p->start[b + 1] : p->n)) last = next_slot(last);
        if (p->term[b] == T_GOTO) p->insn[last] = flow(0xE24, last, p->start[p->target[b]]);
        if (p->term[b] == T_COND) p->insn[last] = flow(0xE24, last, p->start[p->target[b]]) & ~0xF0000ull;
        if (p->term[b] == T_EXIT) p->insn[last] = EXIT;
    }
    /* the translator's leaders: the entry, branch targets, and what follows a branch or an EXIT */
    p->leader[11] = 1;
    for (b = 0; b < nb; b++) {
        if (p->term[b] == T_GOTO || p->term[b] == T_COND) p->leader[p->start[p->target[b]]] = 1;
        if (p->term[b] != T_FALL && b + 1 < nb) p->leader[p->start[b + 1]] = 1;
    }
}

/* the leaders split the blocks the generator made: walk by leader-delimited blocks, numbered by their first word */
static u32 walk_graph(const Program* p, u32 seed, u32* out) {
    u32 count = 0, at = 11;
    g_test.rng = seed;
    while (count < STEPS) {
        out[count++] = at;
        /* the leader-delimited block from at: its last instruction */
        u32 i, last = at;
        for (i = at; i < p->n; i = next_slot(i)) {
            if (i != at && p->leader[i]) break;
            last = i;
        }
        u64 w = p->insn[last];
        const Op* op = decode(w);
        if (op && op->id == O_EXIT) return count;
        if (op && op->id == O_BRA) {
            /* a conditional branch to the next block is no choice (the structure takes it as one successor) */
            if (flow_always(w) || (bra_target(w, last) != next_slot(last) && rnd(2))) {
                at = bra_target(w, last);
                continue;
            }
        }
        at = next_slot(last);
        if (at >= p->n) return count | 0x80000000u; /* ran off the end */
    }
    return count;
}

/* the tokens read as the translator writes them */
static u32 walk_tokens(const ShStructure* st, u32 seed, u32* out) {
    u32 count = 0, k = 0, flag = 0;
    u32 *close_of = calloc(st->nscopes, sizeof *close_of), *open_of = calloc(st->nscopes, sizeof *open_of);
    u32 *else_of = calloc(st->ntokens, sizeof *else_of), *endif_of = calloc(st->ntokens, sizeof *endif_of);
    u32 stack[512], sp = 0;
    g_test.rng = seed;
    for (u32 t = 0; t < st->ntokens; t++) {
        const ShsToken* tk = &st->token[t];
        if (tk->kind == SHS_OPEN_BLOCK || tk->kind == SHS_OPEN_LOOP) open_of[tk->a] = t;
        if (tk->kind == SHS_CLOSE) close_of[tk->a] = t;
        if (tk->kind == SHS_IF) stack[sp++] = t;
        if (tk->kind == SHS_ELSE) else_of[stack[sp - 1]] = t;
        if (tk->kind == SHS_ENDIF) {
            endif_of[stack[sp - 1]] = t;
            else_of[t] = stack[sp - 1];
            sp--;
        }
    }
    for (u32 t = 0; t < st->ntokens; t++)
        if (st->token[t].kind == SHS_ELSE) /* the ELSE's ENDIF: the IF's */
            for (u32 f = 0; f < st->ntokens; f++)
                if (st->token[f].kind == SHS_IF && else_of[f] == t) endif_of[t] = endif_of[f];
    while (count < STEPS && k < st->ntokens) {
        const ShsToken* t = &st->token[k];
        switch (t->kind) {
            case SHS_BODY: out[count++] = t->a; k++; break;
            case SHS_RETURNED: goto done;
            case SHS_END: count |= 0x80000000u; goto done;
            case SHS_OPEN_BLOCK:
            case SHS_OPEN_LOOP: k++; break;
            case SHS_IF: k = rnd(2) ? k + 1 : else_of[k] + 1; break;
            case SHS_ELSE: k = endif_of[k] + 1; break;
            case SHS_ENDIF: k++; break;
            case SHS_CONTINUE: k = open_of[t->a] + 1; break;
            case SHS_BREAK:
                if (t->b) flag = t->b;
                k = close_of[t->a];
                /* fall into the merge */
                /* fallthrough */
            case SHS_CLOSE: {
                u32 s = st->token[k].a;
                const ShsScope* sc = &st->scope[s];
                k++;
                /* se_pending: a break to the parent's merge is taken on the next turn, as running off its end */
                if (sc->check && flag) {
                    u32 parent = sc->parent - 1;
                    if (!st->scope[parent].loop) {
                        if (sc->arrive && sc->beyond)
                            flag = flag == parent + 1 ? 0 : flag;
                        else if (sc->arrive)
                            flag = 0;
                        k = close_of[parent];
                    } else if (sc->arrive && (!sc->beyond || flag == parent + 1)) {
                        flag = 0;
                        k = open_of[parent] + 1;
                    } else
                        k = close_of[parent];
                }
                break;
            }
            default: k++; break;
        }
    }
done:
    free(close_of);
    free(open_of);
    free(else_of);
    free(endif_of);
    return count;
}

int main(void) {
    u32 structured = 0, tried = 0, a[STEPS], b[STEPS];
    for (u32 round = 0; round < 3000; round++) {
        Program p;
        u32 nb = 2 + rnd(round < 1500 ? 8 : 24);
        make_program(&p, nb);
        u32 saved = g_test.rng;
        ShStructure* st = shader_structure(p.insn, p.n, p.leader, NULL);
        g_test.rng = saved;
        tried++;
        if (!st) continue;
        structured++;
        for (u32 walk = 0; walk < 40; walk++) {
            u32 seed = round * 977u + walk * 31u + 7u;
            u32 na = walk_graph(&p, seed, a), nt = walk_tokens(st, seed, b);
            int same = na == nt && !memcmp(a, b, (na & 0x7fffffffu) * sizeof *a);
            if (!same) {
                fprintf(stderr, "round %u walk %u: graph %u steps, tokens %u steps\n", round, walk, na, nt);
                check(0, "the structure visits the blocks the program does");
                break;
            }
        }
        free(st);
        g_test.rng = saved;
    }
    printf("%u of %u random programs structured\n", structured, tried);
    check(structured > tried / 4, "most random programs are structured");
    return g_test.failures ? 1 : 0;
}
