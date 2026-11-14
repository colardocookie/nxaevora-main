/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_VK_UPSCALER_MODULE_H
#define SNX_VK_UPSCALER_MODULE_H
/* An upscaler's adapter that is a module of its own (upscaler.h): which names mean one and where, what a path
   to it must be, and what is read of the table it hands back before anything of it is called. Pure: no Vulkan
   header, so that it is tested on any host. The table is read here as bytes at the offsets it has on a 64 bit
   system. upscaler_module.c asserts them against the real structure. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    SNX_MODULE_OK,
    SNX_MODULE_NOT_FULL_PATH, /* the file is not named by a full path */
    SNX_MODULE_NOT_LOADED,    /* the system did not load the file */
    SNX_MODULE_NO_EXPORT,     /* the file does not export the one function */
    SNX_MODULE_NO_TABLE,      /* the function returned nothing for our version */
    SNX_MODULE_OTHER_VERSION, /* the table's version number is not ours */
    SNX_MODULE_MISSING_CALL   /* the table lacks one of the six calls it must have */
};
static inline const char* snx_module_text(int why) {
    switch (why) {
        case SNX_MODULE_OK: return "loaded";
        case SNX_MODULE_NOT_FULL_PATH: return "its file is not named by a full path";
        case SNX_MODULE_NOT_LOADED: return "the system did not load its file";
        case SNX_MODULE_NO_EXPORT: return "its file does not export the function that returns the table";
        case SNX_MODULE_NO_TABLE: return "it returned no table for this version of the contract";
        case SNX_MODULE_OTHER_VERSION: return "its table is of another version of the contract";
        default: return "its table lacks one of the calls";
    }
}

/* Where a name is known. fsr is part of the program. dlss is a module, and in this build it is known to the
   self-test's lookup only: in the game the name never loads a module, asks one anything, or creates or
   evaluates a context, whatever a module would answer. */
enum { SNX_MODULE_NAME_UNKNOWN, SNX_MODULE_NAME_BUILT_IN, SNX_MODULE_NAME_MODULE, SNX_MODULE_NAME_NOT_IN_GAME };
static inline int snx_module_name(const char* name, int selftest) {
    if (!name) return SNX_MODULE_NAME_UNKNOWN;
    if (!strcmp(name, "fsr")) return SNX_MODULE_NAME_BUILT_IN;
    if (!strcmp(name, "dlss")) return selftest ? SNX_MODULE_NAME_MODULE : SNX_MODULE_NAME_NOT_IN_GAME;
    return SNX_MODULE_NAME_UNKNOWN;
}

/* 1 when the path names one file fully, so that loading it searches no folder. On Windows: a drive, a colon and
   a separator, or two separators and a server's name, and no component that is . or .. after it. Elsewhere:
   it starts with a slash. A relative path, one relative to a drive, and one rooted on the current drive are not
   full. */
static inline int snx_module_full_path(const char* path, int windows) {
    size_t at;
    if (!path || !path[0]) return 0;
    if (!windows) at = path[0] == '/' ? 1 : 0;
    else if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' &&
             (path[2] == '\\' || path[2] == '/'))
        at = 3;
    else if ((path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/') && path[2] &&
             path[2] != '\\' && path[2] != '/' && path[2] != '.' && path[2] != '?')
        at = 2;
    else
        at = 0;
    if (!at || !path[at]) return 0;
    /* no component that is . or .. and no empty one at the end: the name of a file is the last */
    for (size_t from = at;;) {
        size_t to = from;
        while (path[to] && path[to] != '/' && !(windows && path[to] == '\\')) to++;
        const size_t n = to - from;
        if ((n == 1 && path[from] == '.') || (n == 2 && path[from] == '.' && path[from + 1] == '.')) return 0;
        if (!path[to]) return n > 0;
        from = to + 1;
    }
}

/* The table, as bytes: its version number in the first four, then at 16 the first of six calls it must have, a
   pointer each, and a seventh, shutdown, that may be NULL. The number is read first and alone. A table of another
   number is read no further: what is behind its number has a meaning this build does not know. Only then are the
   six calls looked at, and none is called here. */
#define SNX_MODULE_TABLE_ABI 0
#define SNX_MODULE_TABLE_CALLS 16
#define SNX_MODULE_TABLE_CALL_COUNT 6
static inline int snx_module_table(const void* table, uint32_t abi) {
    uint32_t number;
    if (!table) return SNX_MODULE_NO_TABLE;
    memcpy(&number, (const unsigned char*)table + SNX_MODULE_TABLE_ABI, sizeof number);
    if (number != abi) return SNX_MODULE_OTHER_VERSION;
    for (int k = 0; k < SNX_MODULE_TABLE_CALL_COUNT; k++) {
        void* call;
        memcpy(&call, (const unsigned char*)table + SNX_MODULE_TABLE_CALLS + (size_t)k * sizeof call, sizeof call);
        if (!call) return SNX_MODULE_MISSING_CALL;
    }
    return SNX_MODULE_OK;
}
#endif
