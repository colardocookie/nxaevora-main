/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Fixtures for the Lua 5.3 API: one or two lua_States in scratch memory, each with a CallInfo, a stack and a shared
   global_State. Fields the functions do not read keep random bytes, so a stray read or write shows up as a
   difference. Indices stay within what the Lua API allows its callers (lapi.c's api_check rules). */
#include "fixtures.h"
#include "../src/lua53/lua53_layout.h"

/* Scratch layout, as offsets from the scratch base */
#define OFF_STATE_A 0x0000
#define OFF_STATE_B 0x0400
#define OFF_GLOBAL 0x0800
#define OFF_CI_A 0x0A00
#define OFF_CI_B 0x0A80
#define OFF_CLOSURE 0x0B00
#define OFF_STACK_A 0x1000
#define OFF_STACK_B 0x4000
#define STACK_SLOTS 512
#define MAX_VALUES 40 /* values above the function, well within the stack */
#define MAX_UPVALUES 8

typedef struct {
    GAddr L, func;
    int values;   /* stack values above func */
    int upvalues; /* the C closure's nupvalues, or -1 for a light C function */
} State;

static void fill_random(Rng* r, const Guest* g, GAddr scratch, uint64_t size) {
    for (uint64_t i = 0; i < size; i += 8) g_st64(g, scratch + i, rng_next(r));
}

static State make_state(Rng* r, const Guest* g, GAddr scratch, uint64_t off_state, uint64_t off_ci,
                        uint64_t off_stack) {
    State s;
    s.L = scratch + off_state;
    s.func = scratch + off_stack + TVALUE_SIZE * rng_below(r, 4);
    s.values = (int)rng_below(r, MAX_VALUES + 1);
    g_st64(g, s.L + LS_TOP, s.func + TVALUE_SIZE * (1 + (uint64_t)s.values));
    g_st64(g, s.L + LS_L_G, scratch + OFF_GLOBAL);
    g_st64(g, s.L + LS_CI, scratch + off_ci);
    g_st64(g, scratch + off_ci + CI_FUNC, s.func);
    /* The running function: a light C function or a C closure with a few upvalues */
    if (rng_below(r, 2)) {
        g_st32(g, s.func + TV_TT, LUA_TLCF);
        s.upvalues = -1;
    } else {
        const GAddr closure = scratch + OFF_CLOSURE;
        s.upvalues = (int)rng_below(r, MAX_UPVALUES + 1);
        g_st64(g, s.func + TV_VALUE, closure);
        g_st32(g, s.func + TV_TT, LUA_TCCL);
        g->mem[closure + CCL_NUPVALUES] = (uint8_t)s.upvalues;
    }
    return s;
}

static void make_global(Rng* r, const Guest* g, GAddr scratch, GAddr mainthread) {
    const GAddr G = scratch + OFF_GLOBAL;
    g_st64(g, G + GS_MAINTHREAD, rng_below(r, 2) ? mainthread : scratch + OFF_STATE_B + 0x200);
}

/* An index a caller may pass to index2addr: in or beyond the stack, from the top, the registry or an upvalue */
static int32_t pick_index(Rng* r, const State* s) {
    switch (rng_below(r, 4)) {
    case 0: return 1 + (int32_t)rng_below(r, (uint32_t)s->values + 5);
    case 1: return s->values ? -(1 + (int32_t)rng_below(r, (uint32_t)s->values)) : 1;
    case 2: return LUA_REGISTRYINDEX;
    default: return LUA_REGISTRYINDEX - (1 + (int32_t)rng_below(r, MAX_UPVALUES + 3));
    }
}

static State world(Rng* r, CPU* c, const Guest* g, GAddr scratch, uint64_t size) {
    fill_random(r, g, scratch, size);
    State a = make_state(r, g, scratch, OFF_STATE_A, OFF_CI_A, OFF_STACK_A);
    make_global(r, g, scratch, a.L);
    c->x[0] = a.L;
    return a;
}

static void setup_state_only(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) { world(r, c, g, s, n); }

static void setup_index(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) {
    State a = world(r, c, g, s, n);
    c->x[1] = (rng_next(r) & 0xFFFFFFFF00000000ull) | (uint32_t)pick_index(r, &a); /* the upper half is not the
                                                                                      argument's */
}

static void setup_settop(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) {
    State a = world(r, c, g, s, n);
    const int32_t idx = (int32_t)rng_below(r, (uint32_t)a.values + 32) - (a.values + 1); /* -(values+1)..30 */
    c->x[1] = (rng_next(r) & 0xFFFFFFFF00000000ull) | (uint32_t)idx;
}

static void setup_xmove(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) {
    State from = world(r, c, g, s, n);
    State to = make_state(r, g, s, OFF_STATE_B, OFF_CI_B, OFF_STACK_B);
    c->x[0] = from.L;
    c->x[1] = rng_below(r, 10) ? to.L : from.L; /* the same state now and then: nothing moves */
    c->x[2] = rng_below(r, (uint32_t)from.values + 1);
}

static void setup_pointer_arg(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) {
    world(r, c, g, s, n);
    c->x[1] = rng_next(r);
}

static void setup_version(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) {
    world(r, c, g, s, n);
    if (rng_below(r, 2)) c->x[0] = 0;
}

static void setup_typename(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) {
    world(r, c, g, s, n);
    c->x[1] = (rng_next(r) & 0xFFFFFFFF00000000ull) | (uint32_t)((int32_t)rng_below(r, 10) - 1); /* -1..8 */
}

static void setup_number(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) {
    world(r, c, g, s, n);
    c->v[0].d[0] = rng_next(r); /* any bit pattern, NaNs included: it is stored unchanged */
    c->v[0].d[1] = rng_next(r);
}

static void setup_boolean(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) {
    world(r, c, g, s, n);
    const uint32_t choices[4] = {0, 1, (uint32_t)rng_next(r), 0x80000000u};
    c->x[1] = (rng_next(r) & 0xFFFFFFFF00000000ull) | choices[rng_below(r, 4)];
}

const Fixture recon_lua53_fixtures[] = {
    {"lua_absindex", setup_index},
    {"lua_gettop", setup_state_only},
    {"lua_settop", setup_settop},
    {"lua_pushvalue", setup_index},
    {"lua_xmove", setup_xmove},
    {"lua_atpanic", setup_pointer_arg},
    {"lua_version", setup_version},
    {"lua_typename", setup_typename},
    {"lua_pushnil", setup_state_only},
    {"lua_pushnumber", setup_number},
    {"lua_pushinteger", setup_pointer_arg},
    {"lua_pushboolean", setup_boolean},
    {"lua_pushlightuserdata", setup_pointer_arg},
    {"lua_pushthread", setup_state_only},
};
const size_t recon_lua53_fixture_count = sizeof(recon_lua53_fixtures) / sizeof(recon_lua53_fixtures[0]);
