/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* What runtime/options.c calls outside its own file, for the store's dirty flag test: a section of two text rows, the
   log kept in g_test_log (warnings only), and the runtime's own tables and calls as no-ops */
#include "common/utilities/common.h"
#include "nx/hle/services/services.h"
#include "nx/memory/mem.h"
#include "nx/runtime/ini.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/main.h"
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

/* the test's section: a value read into one of these is the memory value of the change */
char g_test_mem[64];
char g_test_new[64];
const IniOpt g_test_table[] = {
    {"mem_key", NULL, NULL, INI_TEXT, 0, g_test_mem, 0, 0, NULL, NULL, NULL, 0, 0, sizeof g_test_mem},
    {"new_key", NULL, NULL, INI_TEXT, 0, g_test_new, 0, 0, NULL, NULL, NULL, 0, 0, sizeof g_test_new},
    {NULL, NULL, NULL, 0, 0, NULL, 0, 0, NULL, NULL, NULL, 0, 0, 0},
};
const IniSection g_ini_sections[] = {{"Test", 1, {g_test_table, NULL, NULL}, {NULL, NULL, NULL}}};
const int g_ini_sections_count = 1;
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
    (void)k;
    (void)v;
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
