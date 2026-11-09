/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The layout of the Lua 5.3 VM embedded in the game (main has the string "Lua 5.3" at +38366CF and lua_version's
   static at +38C5A20 holds 503.0). Offsets were read from the game's own lapi.c functions, listed in
   reconstruction/manifest/functions.csv. Field names follow Lua 5.3's lstate.h, lobject.h and lua.h. */
#ifndef RECON_LUA53_LAYOUT_H
#define RECON_LUA53_LAYOUT_H

/* TValue: Value value_ at +0, int tt_ at +8, 16 bytes */
#define TVALUE_SIZE 16
#define TV_VALUE 0x0
#define TV_TT 0x8

/* lua_State */
#define LS_TOP 0x10 /* StkId top */
#define LS_L_G 0x18 /* global_State *l_G */
#define LS_CI 0x20  /* CallInfo *ci */

/* CallInfo */
#define CI_FUNC 0x0 /* StkId func */

/* global_State */
#define GS_L_REGISTRY 0x40 /* TValue l_registry */
#define GS_PANIC 0xC0      /* lua_CFunction panic */
#define GS_MAINTHREAD 0xC8 /* lua_State *mainthread */
#define GS_VERSION 0xD0    /* const lua_Number *version */

/* CClosure: ClosureHeader then TValue upvalue[] */
#define CCL_NUPVALUES 0xA /* lu_byte nupvalues */
#define CCL_UPVALUE 0x20  /* TValue upvalue[1] */

/* Type tags (tt_) as Lua 5.3 stores them: the basic type, its variant in bits 4-5 and BIT_ISCOLLECTABLE (bit 6) */
#define LUA_TNIL 0
#define LUA_TBOOLEAN 1
#define LUA_TLIGHTUSERDATA 2
#define LUA_TNUMFLT 3          /* LUA_TNUMBER | (0 << 4) */
#define LUA_TNUMINT 19         /* LUA_TNUMBER | (1 << 4) */
#define LUA_TLCF 22            /* LUA_TFUNCTION | (1 << 4), a light C function */
#define LUA_TCCL 38            /* LUA_TFUNCTION | (2 << 4), a C closure */
#define LUA_TTHREAD_TAGGED 72  /* LUA_TTHREAD | BIT_ISCOLLECTABLE */

/* lua.h: LUAI_MAXSTACK is 1000000, so the registry pseudo-index is -1001000 */
#define LUA_REGISTRYINDEX (-1000000 - 1000)

/* Statics in the main module's read-only data, as module offsets */
#define MAIN_LUAO_NILOBJECT 0x38C5C28 /* const TValue luaO_nilobject_ */
#define MAIN_LUA_VERSION 0x38C5A20    /* static const lua_Number version (lapi.c) */
#define MAIN_LUAT_TYPENAMES 0x401AD80 /* const char *const luaT_typenames_[LUA_TOTALTAGS] */

#endif
