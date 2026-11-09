/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Deliberately wrong variants of the reconstructed lapi.c functions, each with one small mistake of the kind a
   reconstruction could plausibly make. With --control the harness runs every original against its mutant, and each
   must fail: that shows the fixtures and the comparison would catch such a mistake. */
#include "mutants.h"
#include "../src/lua53/lua53_layout.h"
#include "recon/call.h"
#include <string.h>

#define ARG_I32(n) ((int32_t)(uint32_t)c->x[n])
#define G recon_guest(c)

static GAddr top(CPU* c, GAddr L) {
    Guest g = G;
    return g_ld64(&g, L + LS_TOP);
}
static GAddr func(CPU* c, GAddr L) {
    Guest g = G;
    return g_ld64(&g, g_ld64(&g, L + LS_CI) + CI_FUNC);
}

/* the stack size, not from func + 1: off by one */
static void m_gettop(CPU* c, u64 e) {
    (void)e;
    c->x[0] = (uint32_t)((top(c, c->x[0]) - func(c, c->x[0])) / TVALUE_SIZE);
    c->pc = c->x[30];
}
/* converts pseudo-indices too */
static void m_absindex(CPU* c, u64 e) {
    (void)e;
    const int32_t idx = ARG_I32(1);
    c->x[0] = (uint32_t)(idx > 0 ? idx : (int32_t)((top(c, c->x[0]) - func(c, c->x[0])) / TVALUE_SIZE) + idx);
    c->pc = c->x[30];
}
/* grows the stack without writing nils */
static void m_settop(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const GAddr L = c->x[0];
    const int32_t idx = ARG_I32(1);
    const GAddr t = idx >= 0 ? func(c, L) + TVALUE_SIZE * (1 + (GAddr)(int64_t)idx)
                             : top(c, L) + (GAddr)(int64_t)(idx + 1) * TVALUE_SIZE;
    g_st64(&g, L + LS_TOP, t);
    c->pc = c->x[30];
}
/* takes upvalue n from slot n, not n - 1 */
static void m_pushvalue(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const GAddr L = c->x[0];
    int32_t idx = ARG_I32(1);
    const GAddr fn = func(c, L), nil = recon_main_base + MAIN_LUAO_NILOBJECT;
    GAddr o;
    if (idx > 0) o = fn + (GAddr)(int64_t)idx * TVALUE_SIZE < top(c, L) ? fn + (GAddr)(int64_t)idx * TVALUE_SIZE : nil;
    else if (idx > LUA_REGISTRYINDEX) o = top(c, L) + (GAddr)(int64_t)idx * TVALUE_SIZE;
    else if (idx == LUA_REGISTRYINDEX) o = g_ld64(&g, L + LS_L_G) + GS_L_REGISTRY;
    else {
        idx = LUA_REGISTRYINDEX - idx;
        const GAddr cl = g_ld64(&g, fn + TV_VALUE);
        o = g_ld32(&g, fn + TV_TT) == LUA_TLCF || idx > g_ld8(&g, cl + CCL_NUPVALUES)
                ? nil
                : cl + CCL_UPVALUE + (GAddr)(int64_t)idx * TVALUE_SIZE;
    }
    g_copy16(&g, top(c, L), o);
    g_st64(&g, L + LS_TOP, top(c, L) + TVALUE_SIZE);
    c->pc = c->x[30];
}
/* leaves the source stack's top where it was */
static void m_xmove(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const GAddr from = c->x[0], to = c->x[1];
    const int32_t n = ARG_I32(2);
    if (from != to)
        for (int32_t i = 0; i < n; i++) {
            g_copy16(&g, top(c, to), top(c, from) - (GAddr)(int64_t)(n - i) * TVALUE_SIZE);
            g_st64(&g, to + LS_TOP, top(c, to) + TVALUE_SIZE);
        }
    c->pc = c->x[30];
}
/* returns the new handler, not the old one */
static void m_atpanic(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    g_st64(&g, g_ld64(&g, c->x[0] + LS_L_G) + GS_PANIC, c->x[1]);
    c->x[0] = c->x[1];
    c->pc = c->x[30];
}
/* always reads the global, even for a NULL state's static */
static void m_version(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    c->x[0] = c->x[0] ? g_ld64(&g, g_ld64(&g, c->x[0] + LS_L_G) + GS_VERSION) : 0;
    c->pc = c->x[30];
}
/* indexes the table by t, not t + 1 */
static void m_typename(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    c->x[0] = g_ld64(&g, recon_main_base + MAIN_LUAT_TYPENAMES + (GAddr)((int64_t)ARG_I32(1)) * 8);
    c->pc = c->x[30];
}
/* also clears the value field */
static void m_pushnil(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const GAddr L = c->x[0], s = top(c, L);
    g_st64(&g, s + TV_VALUE, 0);
    g_st32(&g, s + TV_TT, LUA_TNIL);
    g_st64(&g, L + LS_TOP, s + TVALUE_SIZE);
    c->pc = c->x[30];
}
/* tags the number as an integer */
static void m_pushnumber(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const GAddr L = c->x[0], s = top(c, L);
    g_st64(&g, s + TV_VALUE, c->v[0].d[0]);
    g_st32(&g, s + TV_TT, LUA_TNUMINT);
    g_st64(&g, L + LS_TOP, s + TVALUE_SIZE);
    c->pc = c->x[30];
}
/* stores only the low 32 bits of the integer */
static void m_pushinteger(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const GAddr L = c->x[0], s = top(c, L);
    g_st32(&g, s + TV_VALUE, (uint32_t)c->x[1]);
    g_st32(&g, s + TV_TT, LUA_TNUMINT);
    g_st64(&g, L + LS_TOP, s + TVALUE_SIZE);
    c->pc = c->x[30];
}
/* stores the boolean as 8 bytes */
static void m_pushboolean(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const GAddr L = c->x[0], s = top(c, L);
    g_st64(&g, s + TV_VALUE, ARG_I32(1) != 0);
    g_st32(&g, s + TV_TT, LUA_TBOOLEAN);
    g_st64(&g, L + LS_TOP, s + TVALUE_SIZE);
    c->pc = c->x[30];
}
/* writes the tag as 8 bytes */
static void m_pushlightuserdata(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const GAddr L = c->x[0], s = top(c, L);
    g_st64(&g, s + TV_VALUE, c->x[1]);
    g_st64(&g, s + TV_TT, LUA_TLIGHTUSERDATA);
    g_st64(&g, L + LS_TOP, s + TVALUE_SIZE);
    c->pc = c->x[30];
}
/* forgets the collectable bit in the tag */
static void m_pushthread(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const GAddr L = c->x[0], s = top(c, L);
    g_st64(&g, s + TV_VALUE, L);
    g_st32(&g, s + TV_TT, 8);
    g_st64(&g, L + LS_TOP, s + TVALUE_SIZE);
    c->x[0] = g_ld64(&g, g_ld64(&g, L + LS_L_G) + GS_MAINTHREAD) == L;
    c->pc = c->x[30];
}

/* The mutants' own index2addr, so they do not share code with what they test */
static GAddr m_index(CPU* c, GAddr L, int32_t idx) {
    Guest g = G;
    const GAddr fn = func(c, L), nil = recon_main_base + MAIN_LUAO_NILOBJECT;
    if (idx > 0) return fn + (GAddr)(int64_t)idx * TVALUE_SIZE < top(c, L) ? fn + (GAddr)(int64_t)idx * TVALUE_SIZE : nil;
    if (idx > LUA_REGISTRYINDEX) return top(c, L) + (GAddr)(int64_t)idx * TVALUE_SIZE;
    if (idx == LUA_REGISTRYINDEX) return g_ld64(&g, L + LS_L_G) + GS_L_REGISTRY;
    idx = LUA_REGISTRYINDEX - idx;
    if (g_ld32(&g, fn + TV_TT) == LUA_TLCF) return nil;
    const GAddr cl = g_ld64(&g, fn + TV_VALUE);
    return idx > g_ld8(&g, cl + CCL_NUPVALUES) ? nil : cl + CCL_UPVALUE + (GAddr)(int64_t)(idx - 1) * TVALUE_SIZE;
}
static uint32_t m_tag(CPU* c) {
    Guest g = G;
    return g_ld32(&g, m_index(c, c->x[0], ARG_I32(1)) + TV_TT);
}
#define RESULT(v) (c->x[0] = (v), c->pc = c->x[30])

/* the end of the prefix one value too far */
static void m_rotate(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const GAddr L = c->x[0], t = top(c, L) - TVALUE_SIZE, p = m_index(c, L, ARG_I32(1));
    const int32_t n = ARG_I32(2);
    const GAddr m = n >= 0 ? t - (GAddr)(int64_t)n * TVALUE_SIZE : p - (GAddr)(int64_t)n * TVALUE_SIZE;
    const GAddr ranges[3][2] = {{p, m}, {m + TVALUE_SIZE, t}, {p, t}};
    for (int k = 0; k < 3; k++)
        for (GAddr a = ranges[k][0], b = ranges[k][1]; a < b; a += TVALUE_SIZE, b -= TVALUE_SIZE) {
            uint8_t tmp[16];
            memcpy(tmp, g.mem + a, 16);
            g_copy16(&g, a, b);
            memcpy(g.mem + b, tmp, 16);
        }
    c->pc = c->x[30];
}
/* an invalid index reads as nil, not LUA_TNONE */
static void m_type(CPU* c, u64 e) { (void)e; RESULT((uint32_t)(m_tag(c) & 0xF)); }
/* any function, Lua closures too */
static void m_iscfunction(CPU* c, u64 e) { (void)e; RESULT((m_tag(c) & 0xF) == 6); }
/* any number, floats too */
static void m_isinteger(CPU* c, u64 e) { (void)e; RESULT((m_tag(c) & 0xF) == 3); }
/* strings only, without numbers */
static void m_isstring(CPU* c, u64 e) { (void)e; RESULT((m_tag(c) & 0xF) == 4); }
/* false only for nil, a false boolean counts as true */
static void m_toboolean(CPU* c, u64 e) { (void)e; RESULT(m_tag(c) != LUA_TNIL); }
/* a C closure gives the closure, not its function */
static void m_tocfunction(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const GAddr o = m_index(c, c->x[0], ARG_I32(1));
    const uint32_t tag = g_ld32(&g, o + TV_TT);
    RESULT(tag == LUA_TLCF || tag == LUA_TCCL_TAGGED ? g_ld64(&g, o + TV_VALUE) : 0);
}
/* a full userdata gives its header, not its memory block */
static void m_touserdata(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const GAddr o = m_index(c, c->x[0], ARG_I32(1));
    const uint32_t t = g_ld32(&g, o + TV_TT) & 0xF;
    RESULT(t == 7 || t == 2 ? g_ld64(&g, o + TV_VALUE) : 0);
}
/* compares with LUA_TTHREAD, forgetting the collectable bit */
static void m_tothread(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const GAddr o = m_index(c, c->x[0], ARG_I32(1));
    RESULT(g_ld32(&g, o + TV_TT) == 8 ? g_ld64(&g, o + TV_VALUE) : 0);
}
/* a userdata's metatable taken from the table's field */
static void m_getmetatable(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const GAddr L = c->x[0], o = m_index(c, L, ARG_I32(1));
    const uint32_t t = g_ld32(&g, o + TV_TT) & 0xF;
    const GAddr mt = t == 5 || t == 7 ? g_ld64(&g, g_ld64(&g, o + TV_VALUE) + TABLE_METATABLE)
                                      : g_ld64(&g, g_ld64(&g, L + LS_L_G) + GS_MT + t * 8);
    if (!mt) {
        RESULT(0);
        return;
    }
    const GAddr s = top(c, L);
    g_st64(&g, s + TV_VALUE, mt);
    g_st32(&g, s + TV_TT, LUA_TTABLE_TAGGED);
    g_st64(&g, L + LS_TOP, s + TVALUE_SIZE);
    RESULT(1);
}
/* the user value's tag as the full byte with no basic type taken */
static void m_getuservalue(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const GAddr L = c->x[0], u = g_ld64(&g, m_index(c, L, ARG_I32(1)) + TV_VALUE), s = top(c, L);
    g_st64(&g, s + TV_VALUE, g_ld64(&g, u + UDATA_USER));
    g_st32(&g, s + TV_TT, g_ld8(&g, u + UDATA_TTUV));
    g_st64(&g, L + LS_TOP, s + TVALUE_SIZE);
    RESULT(g_ld8(&g, u + UDATA_TTUV));
}

/* The ones that call the VM, through the same guest calls and frames as the reconstructions */
#define CALL(fn, ret_site) recon_call(c, recon_main_base + (fn), recon_main_base + (ret_site))

/* keeps the stack slot's old bits when the value does not convert, not 0 */
static void m_tonumberx(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const u64 lr = c->x[30];
    const GAddr o = m_index(c, c->x[0], ARG_I32(1)), pisnum = c->x[2], n = recon_stack_alloc(c, 0x20) + 8;
    int32_t isnum = 1;
    if (g_ld32(&g, o + TV_TT) == LUA_TNUMFLT) g_st64(&g, n, g_ld64(&g, o + TV_VALUE));
    else {
        c->x[0] = o;
        c->x[1] = n;
        isnum = (int32_t)CALL(MAIN_LUAV_TONUMBER, 0x50B80);
    }
    if (pisnum) g_st32(&g, pisnum, (uint32_t)isnum);
    c->v[0].d[0] = g_ld64(&g, n);
    recon_stack_free(c, 0x20);
    c->pc = lr;
}
/* takes every string as a number without converting it */
static void m_isnumber(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const u64 lr = c->x[30];
    const GAddr o = m_index(c, c->x[0], ARG_I32(1));
    const uint32_t tag = g_ld32(&g, o + TV_TT);
    if (tag == LUA_TNUMFLT || (tag & 0xF) == 4) {
        c->x[0] = 1;
    } else {
        c->x[0] = o;
        c->x[1] = recon_stack_alloc(c, 0x20) + 8;
        c->x[0] = (uint32_t)CALL(MAIN_LUAV_TONUMBER, 0x50690);
        recon_stack_free(c, 0x20);
    }
    c->pc = lr;
}
/* converts floats with mode 1 (floor), not 0 (exact only) */
static void m_tointegerx(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const u64 lr = c->x[30];
    const GAddr o = m_index(c, c->x[0], ARG_I32(1)), pisnum = c->x[2], n = recon_stack_alloc(c, 0x20) + 8;
    int32_t isnum = 1;
    if (g_ld32(&g, o + TV_TT) == LUA_TNUMINT) g_st64(&g, n, g_ld64(&g, o + TV_VALUE));
    else {
        c->x[0] = o;
        c->x[1] = n;
        c->x[2] = 1;
        isnum = (int32_t)CALL(MAIN_LUAV_TOINTEGER, 0x50C94);
        if (!isnum) g_st64(&g, n, 0);
    }
    if (pisnum) g_st32(&g, pisnum, (uint32_t)isnum);
    c->x[0] = g_ld64(&g, n);
    recon_stack_free(c, 0x20);
    c->pc = lr;
}
/* switches on the basic type (tag & 0xF), so a long string reads as a short one */
static void m_rawlen(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const u64 lr = c->x[30];
    const GAddr o = m_index(c, c->x[0], ARG_I32(1)), p = g_ld64(&g, o + TV_VALUE);
    switch (g_ld32(&g, o + TV_TT) & 0xF) {
    case 4: c->x[0] = g_ld8(&g, p + TSTRING_SHRLEN); break;
    case 7: c->x[0] = g_ld64(&g, p + UDATA_LEN); break;
    case 5:
        recon_stack_alloc(c, 0x10);
        c->x[0] = p;
        c->x[0] = (uint64_t)(int64_t)(int32_t)CALL(MAIN_LUAH_GETN, 0x5106C);
        recon_stack_free(c, 0x10);
        break;
    default: c->x[0] = 0;
    }
    c->pc = lr;
}

/* converts a number but never steps the collector (no luaC_checkGC) */
static void m_tolstring(CPU* c, u64 e) {
    (void)e;
    Guest g = G;
    const u64 lr = c->x[30];
    const GAddr L = c->x[0], len = c->x[2];
    const int32_t idx = ARG_I32(1);
    GAddr o = m_index(c, L, idx);
    const uint32_t t = g_ld32(&g, o + TV_TT) & 0xF;
    if (t != 4 && t != 3) {
        if (len) g_st64(&g, len, 0);
        c->x[0] = 0;
        c->pc = lr;
        return;
    }
    if (t == 3) {
        recon_stack_alloc(c, 0x30);
        c->x[0] = L;
        c->x[1] = o;
        CALL(MAIN_LUAO_TOSTRING, 0x50EBC);
        recon_stack_free(c, 0x30);
        o = m_index(c, L, idx);
    }
    const GAddr ts = g_ld64(&g, o + TV_VALUE);
    if (len) g_st64(&g, len, g_ld8(&g, ts + 8) == 4 ? g_ld8(&g, ts + TSTRING_SHRLEN) : g_ld64(&g, ts + TSTRING_LNGLEN));
    c->x[0] = ts + TSTRING_CONTENTS;
    c->pc = lr;
}

const Mutant recon_lua53_mutants[] = {
    {"lua_tolstring", m_tolstring},
    {"lua_isnumber", m_isnumber},     {"lua_tonumberx", m_tonumberx},
    {"lua_tointegerx", m_tointegerx}, {"lua_rawlen", m_rawlen},
    {"lua_absindex", m_absindex},     {"lua_gettop", m_gettop},
    {"lua_settop", m_settop},         {"lua_pushvalue", m_pushvalue},
    {"lua_xmove", m_xmove},           {"lua_atpanic", m_atpanic},
    {"lua_version", m_version},       {"lua_typename", m_typename},
    {"lua_pushnil", m_pushnil},       {"lua_pushnumber", m_pushnumber},
    {"lua_pushinteger", m_pushinteger}, {"lua_pushboolean", m_pushboolean},
    {"lua_pushlightuserdata", m_pushlightuserdata}, {"lua_pushthread", m_pushthread},
    {"lua_rotate", m_rotate},         {"lua_type", m_type},
    {"lua_iscfunction", m_iscfunction}, {"lua_isinteger", m_isinteger},
    {"lua_isstring", m_isstring},     {"lua_toboolean", m_toboolean},
    {"lua_tocfunction", m_tocfunction}, {"lua_touserdata", m_touserdata},
    {"lua_tothread", m_tothread},     {"lua_getmetatable", m_getmetatable},
    {"lua_getuservalue", m_getuservalue},
};
const size_t recon_lua53_mutant_count = sizeof(recon_lua53_mutants) / sizeof(recon_lua53_mutants[0]);
