/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* What runtime/options.c calls outside its own file, for the store's dirty flag test: sections of text rows, the
   log kept in g_test_log (warnings only), and the runtime's own tables and calls as no-ops */
#include "common/utilities/common.h"
#include "nx/hle/services/services.h"
#include "nx/memory/mem.h"
#include "nx/runtime/ini.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/main.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/backend.h"
#include "nx/runtime/platform/hud.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include "nx/runtime/platform/unattended.h"
#include "nx/runtime/startup.h"
#include "snx_settings.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the test's sections: the runtime's own text rows, and one that apply hands to backend_ini, which reads the
   store's value of back_key when it runs */
char g_test_mem[64];
char g_test_new[64];
char g_test_def[64];
char g_test_backend[64];
char g_test_backend_store[64];
size_t g_test_backend_len;
char g_test_backend_last;
const IniOpt g_test_table[] = {
    {"mem_key", NULL, NULL, INI_TEXT, 0, g_test_mem, 0, 0, NULL, NULL, NULL, 0, 0, sizeof g_test_mem},
    {"new_key", NULL, NULL, INI_TEXT, 0, g_test_new, 0, 0, NULL, NULL, NULL, 0, 0, sizeof g_test_new},
    {"def_key", "dflt", NULL, INI_TEXT, 0, g_test_def, 0, 0, NULL, NULL, NULL, 0, 0, sizeof g_test_def},
    {NULL, NULL, NULL, 0, 0, NULL, 0, 0, NULL, NULL, NULL, 0, 0, 0},
};
const IniOpt g_test_back_table[] = {
    {"back_key", NULL, NULL, INI_TEXT, 0, g_test_backend, 0, 0, NULL, NULL, NULL, 0, 0, sizeof g_test_backend},
    {NULL, NULL, NULL, 0, 0, NULL, 0, 0, NULL, NULL, NULL, 0, 0, 0},
};
const IniSection g_ini_sections[] = {
    {"Test", 1, {g_test_table, NULL, NULL}, {NULL, NULL, NULL}},
    {"Backend", 0, {g_test_back_table, NULL, NULL}, {NULL, NULL, NULL}},
};
const int g_ini_sections_count = 2;
const IniOpt g_debug_ini[] = {{NULL, NULL, NULL, 0, 0, NULL, 0, 0, NULL, NULL, NULL, 0, 0, 0}};

/* the warnings of a phase, which the test clears between them */
char g_test_log[8192];
size_t g_test_log_len;
int g_test_warn_count;

u64 g_heap_max;
MemLayout g_layout;
const SnxModule* g_mod;
char g_exename[256] = "settings-test";
int g_opt_gdb, g_opt_gdb_stop;

int set_language_count(void) { return 1; }
int unattended_known(const char* k) {
    (void)k;
    return 0;
}
int backend_ini(const char* k, const char* v) {
    size_t n = strlen(v);
    g_test_backend_len = n;
    g_test_backend_last = n ? v[n - 1] : 0;
    snprintf(g_test_backend, sizeof g_test_backend, "%s", v);
    if (!strcmp(k, "back_key")) {
        const char* s = snx_settings_value(&g_test_back_table[0]);
        snprintf(g_test_backend_store, sizeof g_test_backend_store, "%s", s ? s : "(none)");
    }
    return 0;
}
int hud_debug_set(const char* text) {
    (void)text;
    return 0;
}
int plat_replace_file(const char* tmp, const char* dst) { return snx_replace(tmp, dst); }
void pm_lock(PMutex* m) { (void)m; }
void pm_unlock(PMutex* m) { (void)m; }

void logf_(int lvl, const char* fmt, ...) {
    va_list ap;
    int n;
    if (lvl != 0) return;
    va_start(ap, fmt);
    n = vsnprintf(g_test_log + g_test_log_len, sizeof g_test_log - g_test_log_len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        g_test_log_len += (size_t)n;
        if (g_test_log_len >= sizeof g_test_log) g_test_log_len = sizeof g_test_log - 1;
    }
    g_test_warn_count++;
}

void fatal(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}
