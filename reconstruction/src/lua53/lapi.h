/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The reconstructed Lua 5.3 C API functions (lapi.c). Pointers are guest addresses. A lua_Number travels as its
   IEEE bits, because the game passes it in D0 and the reconstruction must store those bits unchanged. */
#ifndef RECON_LUA53_LAPI_H
#define RECON_LUA53_LAPI_H

#include "recon/guest.h"

int32_t lua_absindex(const Guest* g, GAddr L, int32_t idx);
int32_t lua_gettop(const Guest* g, GAddr L);
void lua_settop(const Guest* g, GAddr L, int32_t idx);
void lua_pushvalue(const Guest* g, GAddr L, int32_t idx);
void lua_xmove(const Guest* g, GAddr from, GAddr to, int32_t n);
GAddr lua_atpanic(const Guest* g, GAddr L, GAddr panicf);
GAddr lua_version(const Guest* g, GAddr L);
GAddr lua_typename(const Guest* g, GAddr L, int32_t t);
void lua_pushnil(const Guest* g, GAddr L);
void lua_pushnumber(const Guest* g, GAddr L, uint64_t number_bits);
void lua_pushinteger(const Guest* g, GAddr L, int64_t n);
void lua_pushboolean(const Guest* g, GAddr L, int32_t b);
void lua_pushlightuserdata(const Guest* g, GAddr L, GAddr p);
int32_t lua_pushthread(const Guest* g, GAddr L);

#endif
