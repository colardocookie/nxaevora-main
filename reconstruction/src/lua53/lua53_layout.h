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
#define GS_MT 0x1A0        /* struct Table *mt[LUA_NUMTAGS], the basic types' metatables */

/* CClosure: ClosureHeader, then lua_CFunction f and TValue upvalue[] */
#define CCL_NUPVALUES 0xA /* lu_byte nupvalues */
#define CCL_F 0x18        /* lua_CFunction f */
#define CCL_UPVALUE 0x20  /* TValue upvalue[1] */

/* Table and Udata */
#define TABLE_METATABLE 0x28 /* struct Table *metatable */
#define UDATA_TTUV 0xA       /* lu_byte ttuv_, the user value's tag */
#define UDATA_METATABLE 0x10 /* struct Table *metatable */
#define UDATA_USER 0x20      /* union Value user_ */
#define UDATA_SIZE 0x28      /* sizeof(UUdata), where getudatamem's block starts */

/* Basic types (lua.h). ttnov(o) is the tag's low 4 bits. */
#define LUA_TNONE (-1)
#define LUA_TTABLE 5
#define LUA_TUSERDATA 7

/* Type tags (tt_) as Lua 5.3 stores them: the basic type, its variant in bits 4-5 and BIT_ISCOLLECTABLE (bit 6) */
#define BIT_ISCOLLECTABLE 0x40
#define LUA_TNIL 0
#define LUA_TBOOLEAN 1
#define LUA_TLIGHTUSERDATA 2
#define LUA_TNUMFLT 3                                /* LUA_TNUMBER | (0 << 4) */
#define LUA_TNUMINT 19                               /* LUA_TNUMBER | (1 << 4) */
#define LUA_TSHRSTR_TAGGED (4 | BIT_ISCOLLECTABLE)   /* a short string */
#define LUA_TLNGSTR_TAGGED (20 | BIT_ISCOLLECTABLE)  /* a long string */
#define LUA_TTABLE_TAGGED (5 | BIT_ISCOLLECTABLE)
#define LUA_TLCL_TAGGED (6 | BIT_ISCOLLECTABLE)      /* a Lua closure */
#define LUA_TLCF 22                                  /* LUA_TFUNCTION | (1 << 4), a light C function */
#define LUA_TCCL_TAGGED (38 | BIT_ISCOLLECTABLE)     /* LUA_TFUNCTION | (2 << 4), a C closure */
#define LUA_TUSERDATA_TAGGED (7 | BIT_ISCOLLECTABLE) /* a full userdata */
#define LUA_TTHREAD_TAGGED (8 | BIT_ISCOLLECTABLE)

/* lua.h: LUAI_MAXSTACK is 1000000, so the registry pseudo-index is -1001000 */
#define LUA_REGISTRYINDEX (-1000000 - 1000)

/* Statics in the main module's read-only data, as module offsets */
#define MAIN_LUAO_NILOBJECT 0x38C5C28 /* const TValue luaO_nilobject_ */
#define MAIN_LUA_VERSION 0x38C5A20    /* static const lua_Number version (lapi.c) */
#define MAIN_LUAT_TYPENAMES 0x401AD80 /* const char *const luaT_typenames_[LUA_TOTALTAGS] */

#endif
