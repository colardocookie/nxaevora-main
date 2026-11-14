/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Loading an upscaler's adapter that is a module of its own (upscaler.h, upscaler_module.h), by its full path, with
   what it depends on looked for in its own folder and the system's only. Only the function that returns its table
   is called, and the table's number is read first. Nothing is logged here: the caller says what the answer means. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "nx/gpu/vk/upscaler.h"
#include "nx/gpu/vk/upscaler_module.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

/* the pure header reads the table as bytes: its places are the real structure's */
_Static_assert(SNX_MODULE_TABLE_ABI == offsetof(SnxUpscalerApi, abi) && sizeof(((SnxUpscalerApi*)0)->abi) == 4,
               "the table's version number");
_Static_assert(SNX_MODULE_TABLE_CALLS == offsetof(SnxUpscalerApi, instance_extensions) &&
                   offsetof(SnxUpscalerApi, device_extensions) == SNX_MODULE_TABLE_CALLS + sizeof(void*) &&
                   offsetof(SnxUpscalerApi, create) == SNX_MODULE_TABLE_CALLS + 2 * sizeof(void*) &&
                   offsetof(SnxUpscalerApi, prepare) == SNX_MODULE_TABLE_CALLS + 3 * sizeof(void*) &&
                   offsetof(SnxUpscalerApi, evaluate) == SNX_MODULE_TABLE_CALLS + 4 * sizeof(void*) &&
                   offsetof(SnxUpscalerApi, destroy) == SNX_MODULE_TABLE_CALLS + 5 * sizeof(void*) &&
                   offsetof(SnxUpscalerApi, shutdown) == SNX_MODULE_TABLE_CALLS + 6 * sizeof(void*) &&
                   sizeof(SnxUpscalerApi) ==
                       SNX_MODULE_TABLE_CALLS + (SNX_MODULE_TABLE_CALL_COUNT + 1) * sizeof(void*) &&
                   sizeof(((SnxUpscalerApi*)0)->create) == sizeof(void*),
               "the table's six calls, and shutdown after them");

/* The module at this full path, UTF-8, or NULL with why (SNX_MODULE_*). Its one function is handed the log. A file
   that is refused is unloaded again: nothing of it was called but that function. One that is taken stays loaded
   for the session. */
const SnxUpscalerApi* vk_upscaler_module_load(const char* path, SnxUpscalerLog log, int* why) {
    SnxUpscalerEntry entry = NULL;
    const SnxUpscalerApi* table = NULL;
#ifdef _WIN32
    if (!snx_module_full_path(path, 1)) {
        *why = SNX_MODULE_NOT_FULL_PATH;
        return NULL;
    }
    wchar_t* wide = calloc(32768, sizeof *wide);
    HMODULE module = NULL;
    /* its own folder for what it depends on, then the system's: not the current folder, not the search path */
    if (wide && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 32768))
        module = LoadLibraryExW(wide, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    free(wide);
    if (!module) {
        *why = SNX_MODULE_NOT_LOADED;
        return NULL;
    }
    const FARPROC found = GetProcAddress(module, SNX_UPSCALER_ENTRY);
    if (found) memcpy(&entry, &found, sizeof entry);
#else
    if (!snx_module_full_path(path, 0)) {
        *why = SNX_MODULE_NOT_FULL_PATH;
        return NULL;
    }
    /* a name with a slash is one file to the loader: no folder is searched for it */
    void* module = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        *why = SNX_MODULE_NOT_LOADED;
        return NULL;
    }
    void* found = dlsym(module, SNX_UPSCALER_ENTRY);
    if (found) memcpy(&entry, &found, sizeof entry);
#endif
    if (!entry) *why = SNX_MODULE_NO_EXPORT;
    else {
        table = entry(SNX_UPSCALER_ABI, log);
        *why = snx_module_table(table, SNX_UPSCALER_ABI); /* the number first, the calls only if it is ours */
    }
    if (*why == SNX_MODULE_OK) return table;
#ifdef _WIN32
    FreeLibrary(module);
#else
    dlclose(module);
#endif
    return NULL;
}
