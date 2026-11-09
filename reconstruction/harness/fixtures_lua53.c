/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Fixtures for the Lua 5.3 API: one or two lua_States in scratch memory, each with a CallInfo and a stack of typed
   values, a shared global_State and a pool of tables, userdata, closures and threads they point to. Fields nothing
   reads keep random bytes, so a stray access shows. Indices follow lapi.c's api_check rules for callers. */
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
#define OFF_OBJECTS 0x8000 /* the pool that collectable values point into */
#define OBJECT_SIZE 0x80
#define OBJECT_COUNT 64
#define MAX_VALUES 40 /* values above the function, well within the stack */
#define MAX_UPVALUES 8
#define NUMTAGS 9

typedef struct {
    GAddr L, func;
    int values;   /* stack values above func */
    int upvalues; /* the C closure's nupvalues, or -1 for a light C function */
} State;

static void fill_random(Rng* r, const Guest* g, GAddr scratch, uint64_t size) {
    for (uint64_t i = 0; i < size; i += 8) g_st64(g, scratch + i, rng_next(r));
}

static GAddr object(Rng* r, GAddr scratch) { return scratch + OFF_OBJECTS + OBJECT_SIZE * rng_below(r, OBJECT_COUNT); }

/* A pointer field of an object: NULL or another object of the pool */
static GAddr maybe_object(Rng* r, GAddr scratch) { return rng_below(r, 3) ? object(r, scratch) : 0; }

/* The objects' fields the API reads: metatables, a userdata's user value and tag, a closure's function */
static void make_objects(Rng* r, const Guest* g, GAddr scratch) {
    static const uint8_t user_tags[] = {LUA_TNIL, LUA_TBOOLEAN, LUA_TLIGHTUSERDATA, LUA_TNUMFLT, LUA_TNUMINT,
                                        LUA_TSHRSTR_TAGGED, LUA_TTABLE_TAGGED, LUA_TUSERDATA_TAGGED};
    for (unsigned i = 0; i < OBJECT_COUNT; i++) {
        const GAddr o = scratch + OFF_OBJECTS + OBJECT_SIZE * i;
        g_st64(g, o + TABLE_METATABLE, maybe_object(r, scratch));
        g_st64(g, o + UDATA_METATABLE, maybe_object(r, scratch));
        g->mem[o + UDATA_TTUV] = user_tags[rng_below(r, sizeof(user_tags))];
    }
    const GAddr G = scratch + OFF_GLOBAL;
    for (unsigned t = 0; t < 16; t++) g_st64(g, G + GS_MT + 8 * t, t < NUMTAGS ? maybe_object(r, scratch) : 0);
}

/* A value of any type, its value field valid for its tag */
static void make_value(Rng* r, const Guest* g, GAddr scratch, GAddr slot) {
    static const uint32_t tags[] = {LUA_TNIL,           LUA_TBOOLEAN,       LUA_TLIGHTUSERDATA,   LUA_TNUMFLT,
                                    LUA_TNUMINT,        LUA_TSHRSTR_TAGGED, LUA_TLNGSTR_TAGGED,   LUA_TTABLE_TAGGED,
                                    LUA_TLCL_TAGGED,    LUA_TLCF,           LUA_TCCL_TAGGED,      LUA_TUSERDATA_TAGGED,
                                    LUA_TTHREAD_TAGGED};
    const uint32_t tag = tags[rng_below(r, sizeof(tags) / sizeof(tags[0]))];
    g_st32(g, slot + TV_TT, tag);
    if (tag & BIT_ISCOLLECTABLE) g_st64(g, slot + TV_VALUE, object(r, scratch));
    else if (tag == LUA_TBOOLEAN) g_st32(g, slot + TV_VALUE, rng_below(r, 2) ? 0 : (uint32_t)rng_next(r));
}

static State make_state(Rng* r, const Guest* g, GAddr scratch, uint64_t off_state, uint64_t off_ci,
                        uint64_t off_stack, int min_values) {
    State s;
    s.L = scratch + off_state;
    s.func = scratch + off_stack + TVALUE_SIZE * rng_below(r, 4);
    s.values = min_values + (int)rng_below(r, MAX_VALUES + 1 - min_values);
    g_st64(g, s.L + LS_TOP, s.func + TVALUE_SIZE * (1 + (uint64_t)s.values));
    g_st64(g, s.L + LS_L_G, scratch + OFF_GLOBAL);
    g_st64(g, s.L + LS_CI, scratch + off_ci);
    g_st64(g, scratch + off_ci + CI_FUNC, s.func);
    for (int i = 1; i <= s.values; i++) make_value(r, g, scratch, s.func + TVALUE_SIZE * (uint64_t)i);
    /* The running function: a light C function or a C closure with a few upvalues */
    if (rng_below(r, 2)) {
        g_st32(g, s.func + TV_TT, LUA_TLCF);
        s.upvalues = -1;
    } else {
        const GAddr closure = scratch + OFF_CLOSURE;
        s.upvalues = (int)rng_below(r, MAX_UPVALUES + 1);
        g_st64(g, s.func + TV_VALUE, closure);
        g_st32(g, s.func + TV_TT, LUA_TCCL_TAGGED);
        g->mem[closure + CCL_NUPVALUES] = (uint8_t)s.upvalues;
        for (int i = 0; i < MAX_UPVALUES; i++) make_value(r, g, scratch, closure + CCL_UPVALUE + TVALUE_SIZE * i);
    }
    return s;
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

/* A stack index of an existing value (lua_rotate and the like need one) */
static int32_t pick_stack_index(Rng* r, const State* s) {
    const int32_t k = 1 + (int32_t)rng_below(r, (uint32_t)s->values);
    return rng_below(r, 2) ? k : k - s->values - 1;
}

static State world_at_least(Rng* r, CPU* c, const Guest* g, GAddr scratch, uint64_t size, int min_values) {
    fill_random(r, g, scratch, size);
    make_objects(r, g, scratch);
    make_value(r, g, scratch, scratch + OFF_GLOBAL + GS_L_REGISTRY);
    State a = make_state(r, g, scratch, OFF_STATE_A, OFF_CI_A, OFF_STACK_A, min_values);
    g_st64(g, scratch + OFF_GLOBAL + GS_MAINTHREAD, rng_below(r, 2) ? a.L : scratch + OFF_STATE_B + 0x200);
    c->x[0] = a.L;
    return a;
}
static State world(Rng* r, CPU* c, const Guest* g, GAddr scratch, uint64_t size) {
    return world_at_least(r, c, g, scratch, size, 0);
}

/* A 32-bit argument in a register whose upper half is not the argument's */
static uint64_t arg32(Rng* r, int32_t v) { return (rng_next(r) & 0xFFFFFFFF00000000ull) | (uint32_t)v; }

static void setup_state_only(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) { world(r, c, g, s, n); }

static void setup_index(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) {
    State a = world(r, c, g, s, n);
    c->x[1] = arg32(r, pick_index(r, &a));
}

static void setup_settop(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) {
    State a = world(r, c, g, s, n);
    c->x[1] = arg32(r, (int32_t)rng_below(r, (uint32_t)a.values + 32) - (a.values + 1)); /* -(values+1)..30 */
}

static void setup_rotate(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) {
    State a = world_at_least(r, c, g, s, n, 1);
    const int32_t idx = pick_stack_index(r, &a);
    const int32_t len = idx > 0 ? a.values - idx + 1 : -idx; /* the values from idx to the top */
    c->x[1] = arg32(r, idx);
    c->x[2] = arg32(r, (int32_t)rng_below(r, 2 * (uint32_t)len + 1) - len); /* |n| <= len */
}

static void setup_getuservalue(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) {
    State a = world_at_least(r, c, g, s, n, 1);
    const int32_t idx = rng_below(r, 5) ? pick_stack_index(r, &a) : LUA_REGISTRYINDEX;
    const GAddr o = idx == LUA_REGISTRYINDEX ? s + OFF_GLOBAL + GS_L_REGISTRY
                    : idx > 0                ? a.func + TVALUE_SIZE * (uint64_t)idx
                                             : g_ld64(g, a.L + LS_TOP) + TVALUE_SIZE * (uint64_t)(int64_t)idx;
    g_st64(g, o + TV_VALUE, object(r, s)); /* api_check(L, ttisfulluserdata(o)) */
    g_st32(g, o + TV_TT, LUA_TUSERDATA_TAGGED);
    c->x[1] = arg32(r, idx);
}

static void setup_xmove(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) {
    State from = world(r, c, g, s, n);
    State to = make_state(r, g, s, OFF_STATE_B, OFF_CI_B, OFF_STACK_B, 0);
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
    c->x[1] = arg32(r, (int32_t)rng_below(r, 10) - 1); /* -1..8 */
}

static void setup_number(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) {
    world(r, c, g, s, n);
    c->v[0].d[0] = rng_next(r); /* any bit pattern, NaNs included: it is stored unchanged */
    c->v[0].d[1] = rng_next(r);
}

static void setup_boolean(Rng* r, CPU* c, const Guest* g, GAddr s, uint64_t n) {
    world(r, c, g, s, n);
    const uint32_t choices[4] = {0, 1, (uint32_t)rng_next(r), 0x80000000u};
    c->x[1] = arg32(r, (int32_t)choices[rng_below(r, 4)]);
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
    {"lua_rotate", setup_rotate},
    {"lua_type", setup_index},
    {"lua_iscfunction", setup_index},
    {"lua_isinteger", setup_index},
    {"lua_isstring", setup_index},
    {"lua_toboolean", setup_index},
    {"lua_tocfunction", setup_index},
    {"lua_touserdata", setup_index},
    {"lua_tothread", setup_index},
    {"lua_getmetatable", setup_index},
    {"lua_getuservalue", setup_getuservalue},
};
const size_t recon_lua53_fixture_count = sizeof(recon_lua53_fixtures) / sizeof(recon_lua53_fixtures[0]);
