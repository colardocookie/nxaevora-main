/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The callees the Lua 5.3 cases stub (callees.h). A function not listed here has all its callees compiled. */
#include "callees.h"
#include "fixtures_lua53.h"
#include "../src/lua53/lua53_layout.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* luaH_getn walks a table's array and hash parts, which the fixtures' tables do not have */
static const Stub rawlen_stubs[] = {
    {MAIN_LUAH_GETN, "luaH_getn", 1, 0, STUB_INT, NULL},
};

/* luaO_tostring(L, obj) makes obj a string in place, short or long, from the fixtures' pool */
static void tostring_effect(CPU* c, const Guest* g, GAddr scratch, uint64_t h) {
    const GAddr obj = c->x[1];
    g_st64(g, obj + TV_VALUE, scratch + OFF_OBJECTS + OBJECT_SIZE * (h % OBJECT_COUNT));
    g_st32(g, obj + TV_TT, (h >> 32) & 1 ? LUA_TLNGSTR_TAGGED : LUA_TSHRSTR_TAGGED);
}

/* luaO_tostring formats with snprintf and interns the result (luaS_newlstr), so it allocates. luaC_step runs the
   collector. Neither has inputs the fixtures build. */
static const Stub tolstring_stubs[] = {
    {MAIN_LUAO_TOSTRING, "luaO_tostring", 2, 0, STUB_INT, tostring_effect},
    {MAIN_LUAC_STEP, "luaC_step", 1, 0, STUB_INT, NULL},
};

const CalleePlan recon_lua53_callees[] = {
    {"lua_rawlen", rawlen_stubs, COUNT(rawlen_stubs)},
    {"lua_tolstring", tolstring_stubs, COUNT(tolstring_stubs)},
};
const size_t recon_lua53_callee_count = COUNT(recon_lua53_callees);
