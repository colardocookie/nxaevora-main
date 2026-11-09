/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* GuestFn adapters for the reconstructed lapi.c functions: AAPCS64 arguments from X0-X7 and D0, the result in W0 or
   X0 (a W write clears the upper half, as the hardware does), then a return through X30. */
#include "lapi.h"
#include "recon/registry.h"

#define ARG_I32(n) ((int32_t)(uint32_t)c->x[n])
#define RETURN_TO_CALLER() (c->pc = c->x[30])

static void fn_absindex(CPU* c, u64 e) {
    (void)e;
    Guest g = recon_guest(c);
    c->x[0] = (uint32_t)lua_absindex(&g, c->x[0], ARG_I32(1));
    RETURN_TO_CALLER();
}
static void fn_gettop(CPU* c, u64 e) {
    (void)e;
    Guest g = recon_guest(c);
    c->x[0] = (uint32_t)lua_gettop(&g, c->x[0]);
    RETURN_TO_CALLER();
}
static void fn_settop(CPU* c, u64 e) {
    (void)e;
    Guest g = recon_guest(c);
    lua_settop(&g, c->x[0], ARG_I32(1));
    RETURN_TO_CALLER();
}
static void fn_pushvalue(CPU* c, u64 e) {
    (void)e;
    Guest g = recon_guest(c);
    lua_pushvalue(&g, c->x[0], ARG_I32(1));
    RETURN_TO_CALLER();
}
static void fn_xmove(CPU* c, u64 e) {
    (void)e;
    Guest g = recon_guest(c);
    lua_xmove(&g, c->x[0], c->x[1], ARG_I32(2));
    RETURN_TO_CALLER();
}
static void fn_atpanic(CPU* c, u64 e) {
    (void)e;
    Guest g = recon_guest(c);
    c->x[0] = lua_atpanic(&g, c->x[0], c->x[1]);
    RETURN_TO_CALLER();
}
static void fn_version(CPU* c, u64 e) {
    (void)e;
    Guest g = recon_guest(c);
    c->x[0] = lua_version(&g, c->x[0]);
    RETURN_TO_CALLER();
}
static void fn_typename(CPU* c, u64 e) {
    (void)e;
    Guest g = recon_guest(c);
    c->x[0] = lua_typename(&g, c->x[0], ARG_I32(1));
    RETURN_TO_CALLER();
}
static void fn_pushnil(CPU* c, u64 e) {
    (void)e;
    Guest g = recon_guest(c);
    lua_pushnil(&g, c->x[0]);
    RETURN_TO_CALLER();
}
static void fn_pushnumber(CPU* c, u64 e) {
    (void)e;
    Guest g = recon_guest(c);
    lua_pushnumber(&g, c->x[0], c->v[0].d[0]);
    RETURN_TO_CALLER();
}
static void fn_pushinteger(CPU* c, u64 e) {
    (void)e;
    Guest g = recon_guest(c);
    lua_pushinteger(&g, c->x[0], (int64_t)c->x[1]);
    RETURN_TO_CALLER();
}
static void fn_pushboolean(CPU* c, u64 e) {
    (void)e;
    Guest g = recon_guest(c);
    lua_pushboolean(&g, c->x[0], ARG_I32(1));
    RETURN_TO_CALLER();
}
static void fn_pushlightuserdata(CPU* c, u64 e) {
    (void)e;
    Guest g = recon_guest(c);
    lua_pushlightuserdata(&g, c->x[0], c->x[1]);
    RETURN_TO_CALLER();
}
static void fn_pushthread(CPU* c, u64 e) {
    (void)e;
    Guest g = recon_guest(c);
    c->x[0] = (uint32_t)lua_pushthread(&g, c->x[0]);
    RETURN_TO_CALLER();
}

/* (lua_State *L, int idx) functions with an int or a pointer result */
#define IDX_TO_W0(fn)                                                                                                      static void fn_##fn(CPU* c, u64 e) {                                                                                       (void)e;                                                                                                               Guest g = recon_guest(c);                                                                                              c->x[0] = (uint32_t)fn(&g, c->x[0], ARG_I32(1));                                                                       RETURN_TO_CALLER();                                                                                                }
#define IDX_TO_X0(fn)                                                                                                      static void fn_##fn(CPU* c, u64 e) {                                                                                       (void)e;                                                                                                               Guest g = recon_guest(c);                                                                                              c->x[0] = fn(&g, c->x[0], ARG_I32(1));                                                                                 RETURN_TO_CALLER();                                                                                                }
IDX_TO_W0(lua_type)
IDX_TO_W0(lua_iscfunction)
IDX_TO_W0(lua_isinteger)
IDX_TO_W0(lua_isstring)
IDX_TO_W0(lua_toboolean)
IDX_TO_W0(lua_getmetatable)
IDX_TO_W0(lua_getuservalue)
IDX_TO_X0(lua_tocfunction)
IDX_TO_X0(lua_touserdata)
IDX_TO_X0(lua_tothread)

static void fn_rotate(CPU* c, u64 e) {
    (void)e;
    Guest g = recon_guest(c);
    lua_rotate(&g, c->x[0], ARG_I32(1), ARG_I32(2));
    RETURN_TO_CALLER();
}

/* The ones that make calls: those set X30, so the caller's return address is kept from entry */
static void fn_tonumberx(CPU* c, u64 e) {
    (void)e;
    const u64 lr = c->x[30];
    const uint64_t bits = lua_tonumberx(c, c->x[0], ARG_I32(1), c->x[2]);
    c->v[0].d[0] = bits; /* an LDR of D0 clears the rest of V0 */
    c->v[0].d[1] = 0;
    c->x[30] = lr;
    c->pc = lr;
}
static void fn_isnumber(CPU* c, u64 e) {
    (void)e;
    const u64 lr = c->x[30];
    c->x[0] = (uint32_t)lua_isnumber(c, c->x[0], ARG_I32(1));
    c->x[30] = lr;
    c->pc = lr;
}
static void fn_tointegerx(CPU* c, u64 e) {
    (void)e;
    const u64 lr = c->x[30];
    c->x[0] = (uint64_t)lua_tointegerx(c, c->x[0], ARG_I32(1), c->x[2]);
    c->x[30] = lr;
    c->pc = lr;
}
static void fn_rawlen(CPU* c, u64 e) {
    (void)e;
    const u64 lr = c->x[30];
    c->x[0] = lua_rawlen(c, c->x[0], ARG_I32(1));
    c->x[30] = lr;
    c->pc = lr;
}

static void fn_tolstring(CPU* c, u64 e) {
    (void)e;
    const u64 lr = c->x[30];
    c->x[0] = lua_tolstring(c, c->x[0], ARG_I32(1), c->x[2]);
    c->x[30] = lr;
    c->pc = lr;
}

#define SRC "src/lua53/lapi.c"
const ReconFunction recon_lua53_functions[] = {
    {"lua_absindex", SRC, 0x4FEB0, 52, RECON_RET_W0, fn_absindex},
    {"lua_gettop", SRC, 0x4FEF0, 28, RECON_RET_W0, fn_gettop},
    {"lua_settop", SRC, 0x4FF10, 84, RECON_RET_NONE, fn_settop},
    {"lua_pushvalue", SRC, 0x50260, 172, RECON_RET_NONE, fn_pushvalue},
    {"lua_xmove", SRC, 0x4FE00, 116, RECON_RET_NONE, fn_xmove},
    {"lua_atpanic", SRC, 0x4FE80, 16, RECON_RET_X0, fn_atpanic},
    {"lua_version", SRC, 0x4FE90, 28, RECON_RET_X0, fn_version},
    {"lua_typename", SRC, 0x503F0, 20, RECON_RET_X0, fn_typename},
    {"lua_pushnil", SRC, 0x51490, 24, RECON_RET_NONE, fn_pushnil},
    {"lua_pushnumber", SRC, 0x514B0, 32, RECON_RET_NONE, fn_pushnumber},
    {"lua_pushinteger", SRC, 0x514D0, 32, RECON_RET_NONE, fn_pushinteger},
    {"lua_pushboolean", SRC, 0x517E0, 40, RECON_RET_NONE, fn_pushboolean},
    {"lua_pushlightuserdata", SRC, 0x51810, 32, RECON_RET_NONE, fn_pushlightuserdata},
    {"lua_pushthread", SRC, 0x51830, 44, RECON_RET_W0, fn_pushthread},
    {"lua_rotate", SRC, 0x4FF70, 340, RECON_RET_NONE, fn_rotate},
    {"lua_type", SRC, 0x50310, 220, RECON_RET_W0, fn_lua_type},
    {"lua_iscfunction", SRC, 0x50410, 180, RECON_RET_W0, fn_lua_iscfunction},
    {"lua_isinteger", SRC, 0x504D0, 204, RECON_RET_W0, fn_lua_isinteger},
    {"lua_isstring", SRC, 0x506A0, 176, RECON_RET_W0, fn_lua_isstring},
    {"lua_toboolean", SRC, 0x50CC0, 216, RECON_RET_W0, fn_lua_toboolean},
    {"lua_tocfunction", SRC, 0x510E0, 228, RECON_RET_X0, fn_lua_tocfunction},
    {"lua_touserdata", SRC, 0x511D0, 248, RECON_RET_X0, fn_lua_touserdata},
    {"lua_tothread", SRC, 0x512D0, 212, RECON_RET_X0, fn_lua_tothread},
    {"lua_getmetatable", SRC, 0x52090, 320, RECON_RET_W0, fn_lua_getmetatable},
    {"lua_getuservalue", SRC, 0x521D0, 200, RECON_RET_W0, fn_lua_getuservalue},
    {"lua_isnumber", SRC, 0x505A0, 252, RECON_RET_W0, fn_isnumber},
    {"lua_tonumberx", SRC, 0x50A90, 272, RECON_RET_D0, fn_tonumberx},
    {"lua_tointegerx", SRC, 0x50BA0, 276, RECON_RET_X0, fn_tointegerx},
    {"lua_rawlen", SRC, 0x50FC0, 276, RECON_RET_X0, fn_rawlen},
    {"lua_tolstring", SRC, 0x50DA0, 532, RECON_RET_X0, fn_tolstring},
};
const size_t recon_lua53_count = sizeof(recon_lua53_functions) / sizeof(recon_lua53_functions[0]);
