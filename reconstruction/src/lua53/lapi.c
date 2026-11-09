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

/* reverse, lua_rotate's helper: swaps the values of from..to end to end */
static void reverse(const Guest* g, GAddr from, GAddr to) {
    for (; from < to; from += TVALUE_SIZE, to -= TVALUE_SIZE) {
        uint8_t temp[TVALUE_SIZE];
        memcpy(temp, g->mem + from, TVALUE_SIZE);
        g_copy16(g, from, to);
        memcpy(g->mem + to, temp, TVALUE_SIZE);
    }
}

void lua_rotate(const Guest* g, GAddr L, int32_t idx, int32_t n) {
    const GAddr t = top_of(g, L) - TVALUE_SIZE; /* the end of the segment being rotated */
    const GAddr p = index2addr(g, L, idx);      /* its start */
    const GAddr m = n >= 0 ? t - (GAddr)(int64_t)n * TVALUE_SIZE
                           : p - (GAddr)(int64_t)n * TVALUE_SIZE - TVALUE_SIZE; /* the end of the prefix */
    reverse(g, p, m);
    reverse(g, m + TVALUE_SIZE, t);
    reverse(g, p, t);
}

static uint32_t tag_of(const Guest* g, GAddr o) { return g_ld32(g, o + TV_TT); }
static int32_t novariant(uint32_t tag) { return (int32_t)(tag & 0xF); } /* ttnov */

int32_t lua_type(const Guest* g, GAddr L, int32_t idx) {
    const GAddr o = index2addr(g, L, idx);
    return o == g->main_base + MAIN_LUAO_NILOBJECT ? LUA_TNONE : novariant(tag_of(g, o));
}

int32_t lua_iscfunction(const Guest* g, GAddr L, int32_t idx) {
    const uint32_t tag = tag_of(g, index2addr(g, L, idx));
    return tag == LUA_TLCF || tag == LUA_TCCL_TAGGED;
}

int32_t lua_isinteger(const Guest* g, GAddr L, int32_t idx) {
    return tag_of(g, index2addr(g, L, idx)) == LUA_TNUMINT;
}

int32_t lua_isstring(const Guest* g, GAddr L, int32_t idx) {
    /* ttisstring(o) || cvt2str(o): a string, or a number that converts to one */
    const int32_t t = novariant(tag_of(g, index2addr(g, L, idx)));
    return t == 3 || t == 4;
}

int32_t lua_toboolean(const Guest* g, GAddr L, int32_t idx) {
    const GAddr o = index2addr(g, L, idx);
    const uint32_t tag = tag_of(g, o);
    if (tag == LUA_TNIL) return 0; /* !l_isfalse(o) */
    if (tag == LUA_TBOOLEAN) return g_ld32(g, o + TV_VALUE) != 0;
    return 1;
}

GAddr lua_tocfunction(const Guest* g, GAddr L, int32_t idx) {
    const GAddr o = index2addr(g, L, idx);
    const uint32_t tag = tag_of(g, o);
    if (tag == LUA_TLCF) return g_ld64(g, o + TV_VALUE);
    if (tag == LUA_TCCL_TAGGED) return g_ld64(g, g_ld64(g, o + TV_VALUE) + CCL_F);
    return 0;
}

GAddr lua_touserdata(const Guest* g, GAddr L, int32_t idx) {
    const GAddr o = index2addr(g, L, idx);
    switch (novariant(tag_of(g, o))) {
    case LUA_TUSERDATA: return g_ld64(g, o + TV_VALUE) + UDATA_SIZE; /* getudatamem */
    case LUA_TLIGHTUSERDATA: return g_ld64(g, o + TV_VALUE);
    default: return 0;
    }
}

GAddr lua_tothread(const Guest* g, GAddr L, int32_t idx) {
    const GAddr o = index2addr(g, L, idx);
    return tag_of(g, o) == LUA_TTHREAD_TAGGED ? g_ld64(g, o + TV_VALUE) : 0;
}

int32_t lua_getmetatable(const Guest* g, GAddr L, int32_t objindex) {
    const GAddr obj = index2addr(g, L, objindex);
    const int32_t t = novariant(tag_of(g, obj));
    GAddr mt;
    if (t == LUA_TTABLE) mt = g_ld64(g, g_ld64(g, obj + TV_VALUE) + TABLE_METATABLE);
    else if (t == LUA_TUSERDATA) mt = g_ld64(g, g_ld64(g, obj + TV_VALUE) + UDATA_METATABLE);
    else mt = g_ld64(g, global_of(g, L) + GS_MT + (GAddr)t * 8);
    if (!mt) return 0;
    const GAddr slot = top_of(g, L); /* sethvalue(L, L->top, mt) */
    g_st64(g, slot + TV_VALUE, mt);
    g_st32(g, slot + TV_TT, LUA_TTABLE_TAGGED);
    incr_top(g, L);
    return 1;
}

int32_t lua_getuservalue(const Guest* g, GAddr L, int32_t idx) {
    const GAddr u = g_ld64(g, index2addr(g, L, idx) + TV_VALUE); /* a full userdata (api_check) */
    const GAddr slot = top_of(g, L);                              /* getuservalue(L, u, L->top) */
    g_st64(g, slot + TV_VALUE, g_ld64(g, u + UDATA_USER));
    g_st32(g, slot + TV_TT, g_ld8(g, u + UDATA_TTUV));
    incr_top(g, L);
    return novariant(tag_of(g, top_of(g, L) - TVALUE_SIZE));
}
