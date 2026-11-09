/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Reconstructed functions of the game's Lua 5.3 C API (lapi.c), written against the game's memory layout
   (lua53_layout.h). Each one was compared with the game's own code by reconstruction/harness. The logic follows
   Lua 5.3's lapi.c, written here from the game's code. Stores keep the game's widths: a tag is a 4-byte int, a
   boolean a 4-byte int. Main-module offsets are in reconstruction/manifest/functions.csv. */
#include "lapi.h"
#include "lua53_layout.h"

static GAddr top_of(const Guest* g, GAddr L) { return g_ld64(g, L + LS_TOP); }
static void set_top(const Guest* g, GAddr L, GAddr top) { g_st64(g, L + LS_TOP, top); }
static GAddr ci_func(const Guest* g, GAddr L) { return g_ld64(g, g_ld64(g, L + LS_CI) + CI_FUNC); }
static GAddr global_of(const Guest* g, GAddr L) { return g_ld64(g, L + LS_L_G); }

/* api_incr_top */
static void incr_top(const Guest* g, GAddr L) { set_top(g, L, top_of(g, L) + TVALUE_SIZE); }

/* index2addr, which the game inlines into its callers. Invalid indices give luaO_nilobject. */
static GAddr index2addr(const Guest* g, GAddr L, int32_t idx) {
    const GAddr func = ci_func(g, L);
    const GAddr nilobject = g->main_base + MAIN_LUAO_NILOBJECT;
    if (idx > 0) {
        const GAddr o = func + (GAddr)(int64_t)idx * TVALUE_SIZE;
        return o < top_of(g, L) ? o : nilobject;
    }
    if (idx > LUA_REGISTRYINDEX) /* !ispseudo(idx): a negative index from the top */
        return top_of(g, L) + (GAddr)(int64_t)idx * TVALUE_SIZE;
    if (idx == LUA_REGISTRYINDEX) return global_of(g, L) + GS_L_REGISTRY;
    /* An upvalue of the running C closure. A light C function has none. */
    idx = LUA_REGISTRYINDEX - idx;
    if (g_ld32(g, func + TV_TT) == LUA_TLCF) return nilobject;
    const GAddr closure = g_ld64(g, func + TV_VALUE);
    if (idx > g_ld8(g, closure + CCL_NUPVALUES)) return nilobject;
    return closure + CCL_UPVALUE + (GAddr)(int64_t)(idx - 1) * TVALUE_SIZE;
}

int32_t lua_absindex(const Guest* g, GAddr L, int32_t idx) {
    if (idx > 0 || idx <= LUA_REGISTRYINDEX) return idx;
    return (int32_t)((top_of(g, L) - ci_func(g, L)) / TVALUE_SIZE) + idx;
}

int32_t lua_gettop(const Guest* g, GAddr L) {
    return (int32_t)((top_of(g, L) - (ci_func(g, L) + TVALUE_SIZE)) / TVALUE_SIZE);
}

void lua_settop(const Guest* g, GAddr L, int32_t idx) {
    if (idx >= 0) {
        const GAddr func = ci_func(g, L);
        const GAddr target = func + TVALUE_SIZE + (GAddr)(int64_t)idx * TVALUE_SIZE;
        while (top_of(g, L) < target) { /* setnilvalue(L->top++) */
            const GAddr slot = top_of(g, L);
            set_top(g, L, slot + TVALUE_SIZE);
            g_st32(g, slot + TV_TT, LUA_TNIL);
        }
        set_top(g, L, target);
    } else {
        set_top(g, L, top_of(g, L) + (GAddr)(int64_t)(idx + 1) * TVALUE_SIZE);
    }
}

void lua_pushvalue(const Guest* g, GAddr L, int32_t idx) {
    g_copy16(g, top_of(g, L), index2addr(g, L, idx));
    incr_top(g, L);
}

void lua_xmove(const Guest* g, GAddr from, GAddr to, int32_t n) {
    if (from == to) return;
    set_top(g, from, top_of(g, from) - (GAddr)(int64_t)n * TVALUE_SIZE);
    for (int32_t i = 0; i < n; i++) {
        g_copy16(g, top_of(g, to), top_of(g, from) + (GAddr)(int64_t)i * TVALUE_SIZE);
        incr_top(g, to);
    }
}

GAddr lua_atpanic(const Guest* g, GAddr L, GAddr panicf) {
    const GAddr G = global_of(g, L);
    const GAddr old = g_ld64(g, G + GS_PANIC);
    g_st64(g, G + GS_PANIC, panicf);
    return old;
}

GAddr lua_version(const Guest* g, GAddr L) {
    if (!L) return g->main_base + MAIN_LUA_VERSION;
    return g_ld64(g, global_of(g, L) + GS_VERSION);
}

GAddr lua_typename(const Guest* g, GAddr L, int32_t t) {
    (void)L;
    /* ttypename(t): luaT_typenames_[t + 1], so LUA_TNONE (-1) is the first entry */
    return g_ld64(g, g->main_base + MAIN_LUAT_TYPENAMES + (GAddr)((int64_t)t + 1) * 8);
}

void lua_pushnil(const Guest* g, GAddr L) {
    g_st32(g, top_of(g, L) + TV_TT, LUA_TNIL);
    incr_top(g, L);
}

void lua_pushnumber(const Guest* g, GAddr L, uint64_t number_bits) {
    const GAddr slot = top_of(g, L);
    g_st64(g, slot + TV_VALUE, number_bits);
    g_st32(g, slot + TV_TT, LUA_TNUMFLT);
    incr_top(g, L);
}

void lua_pushinteger(const Guest* g, GAddr L, int64_t n) {
    const GAddr slot = top_of(g, L);
    g_st64(g, slot + TV_VALUE, (uint64_t)n);
    g_st32(g, slot + TV_TT, LUA_TNUMINT);
    incr_top(g, L);
}

void lua_pushboolean(const Guest* g, GAddr L, int32_t b) {
    const GAddr slot = top_of(g, L);
    g_st32(g, slot + TV_VALUE, b != 0); /* setbvalue: the int b field only */
    g_st32(g, slot + TV_TT, LUA_TBOOLEAN);
    incr_top(g, L);
}

void lua_pushlightuserdata(const Guest* g, GAddr L, GAddr p) {
    const GAddr slot = top_of(g, L);
    g_st64(g, slot + TV_VALUE, p);
    g_st32(g, slot + TV_TT, LUA_TLIGHTUSERDATA);
    incr_top(g, L);
}

int32_t lua_pushthread(const Guest* g, GAddr L) {
    const GAddr slot = top_of(g, L);
    g_st64(g, slot + TV_VALUE, L);
    g_st32(g, slot + TV_TT, LUA_TTHREAD_TAGGED);
    incr_top(g, L);
    return g_ld64(g, global_of(g, L) + GS_MAINTHREAD) == L;
}
