/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The platform layer's parts SDL already does the same way on every host: the clocks, sleeping, shared libraries and
   the path queries. plat_posix.c and plat_win32.c keep what SDL has no call for (condition variables with nanosecond
   deadlines, threads with their alternate stacks, the guest's address space, file maps, the crash handler) */
#include "nx/runtime/platform/plat.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

/* ================================================================ time */
/* SDL's high-resolution clock, counted from its first call */
u64 plat_ns(void) { return SDL_GetTicksNS(); }
i64 plat_unix_time(void) {
    SDL_Time t;
    return SDL_GetCurrentTime(&t) ? (i64)(t / (SDL_Time)SDL_NS_PER_SECOND) : 0;
}
i64 plat_tz_offset(void) {
    SDL_Time t;
    SDL_DateTime dt;
    if (!SDL_GetCurrentTime(&t) || !SDL_TimeToDateTime(t, &dt, true)) return 0;
    return (i64)dt.utc_offset;
}
/* a thread's own high-resolution waitable timer on Windows, nanosleep elsewhere */
void plat_sleep_ns(i64 ns) {
    if (ns <= 0)
        plat_yield();
    else
        SDL_DelayNS((Uint64)ns);
}

/* ================================================================ libraries */
/* dlopen with RTLD_NOW | RTLD_LOCAL, or LoadLibraryW of the UTF-8 path */
void* plat_dl_open(const char* path) {
    return SDL_LoadObject(path);
}
void* plat_dl_sym(void* h, const char* name) {
    SDL_FunctionPointer f = SDL_LoadFunction((SDL_SharedObject*)h, name);
    void* p;
    memcpy(&p, &f, sizeof p);
    return p;
}
const char* plat_dl_error(void) {
    const char* e = SDL_GetError();
    return e && *e ? e : "no error reported";
}

/* ================================================================ files */
int plat_is_dir(const char* path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}
/* rename, or MoveFileExW replacing dst: tmp is beside dst, so it is never a copy across volumes */
int plat_replace_file(const char* tmp, const char* dst) {
    if (SDL_RenamePath(tmp, dst)) return 0;
    SDL_RemovePath(tmp);
    return -1;
}
