/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "nx/runtime/platform/stats.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/vpad.h"
#include "snx_settings.h"
#include <stdio.h>

static struct {
    int corner; /* stats_corner's, as vpad_corner reads it */
} g_stats;

void stats_corner_set(const IniOpt* o, const char* v) {
    int i = vpad_corner(v);
    if (i >= 0) {
        __atomic_store_n(&g_stats.corner, i, __ATOMIC_RELAXED); /* the renderer's thread reads it */
        return;
    }
    i = __atomic_load_n(&g_stats.corner, __ATOMIC_RELAXED);
    LOG("settings: stats_corner=%s is not tl, tr, bl or br: it keeps %s\n", v, vpad_corner_word(i));
    snprintf((char*)o->var, o->size, "%s", vpad_corner_word(i));
}

int stats_corner(void) { return __atomic_load_n(&g_stats.corner, __ATOMIC_RELAXED); }

int stats_level(void) {
    int level = __atomic_load_n(&g_settings.stats, __ATOMIC_RELAXED);
    return level >= STATS_LEVEL_OFF && level < STATS_LEVEL_COUNT ? level : STATS_LEVEL_OFF;
}

int stats_level_next(int level) {
    if (level < STATS_LEVEL_OFF || level >= STATS_LEVEL_COUNT) return STATS_LEVEL_OFF;
    return (level + 1) % STATS_LEVEL_COUNT;
}
