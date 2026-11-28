/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The stats state (runtime/platform/stats.c): the level steps 0>1>2>3>0 and
   an out-of-range value gives Off, and the corner set and get. */
#include "nx/runtime/platform/stats.h"
#include "nx/runtime/platform/plat.h"
#include "nx/runtime/platform/vpad.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ui.c's lock and log, and what vpad.c names, without the platform layer */
void pm_lock(PMutex* m) { (void)m; }
void pm_unlock(PMutex* m) { (void)m; }
void fatal(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    abort();
}
void options_language_set(const IniOpt* o, const char* v) {
    (void)o;
    (void)v;
}
static char logged[256];
void logf_(int lvl, const char* fmt, ...) {
    va_list ap;
    (void)lvl;
    va_start(ap, fmt);
    vsnprintf(logged, sizeof logged, fmt, ap);
    va_end(ap);
}

int main(void) {
    const IniOpt* corner = ini_find(g_display_ini, "stats_corner");
    assert(corner);
    /* the level steps 0>1>2>3>0; an out-of-range value gives Off */
    assert(stats_level_next(STATS_LEVEL_OFF) == STATS_LEVEL_MINIMAL);
    assert(stats_level_next(STATS_LEVEL_MINIMAL) == STATS_LEVEL_STANDARD);
    assert(stats_level_next(STATS_LEVEL_STANDARD) == STATS_LEVEL_DETAILED);
    assert(stats_level_next(STATS_LEVEL_DETAILED) == STATS_LEVEL_OFF);
    assert(stats_level_next(STATS_LEVEL_COUNT) == STATS_LEVEL_OFF);
    assert(stats_level_next(-1) == STATS_LEVEL_OFF);
    /* the setting's value, clamped: an out-of-range value is Off */
    g_settings.stats = STATS_LEVEL_DETAILED;
    assert(stats_level() == STATS_LEVEL_DETAILED);
    g_settings.stats = 7;
    assert(stats_level() == STATS_LEVEL_OFF);
    /* the default corner, a word for each corner, and a word that is no corner
       keeps the corner and says which */
    assert(stats_corner() == 0);
    /* ini_read writes the text into the setting before it calls set: a valid word is already there */
    snprintf((char*)corner->var, corner->size, "%s", "br");
    stats_corner_set(corner, "br");
    assert(stats_corner() == 3 && !strcmp(g_settings.stats_corner, "br"));
    stats_corner_set(corner, "middle");
    assert(stats_corner() == 3 && !strcmp(g_settings.stats_corner, "br"));
    assert(!strcmp(logged, "settings: stats_corner=middle is not tl, tr, bl or br: it keeps br\n"));
    snprintf((char*)corner->var, corner->size, "%s", "tl");
    stats_corner_set(corner, "tl");
    assert(stats_corner() == 0 && !strcmp(g_settings.stats_corner, "tl"));
    puts("stats passed");
    return 0;
}
