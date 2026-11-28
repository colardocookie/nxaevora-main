/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Stats (settings.txt's stats and stats_corner): the performance overlay's level and the corner it draws in. The
   values that the overlay shows come from the Debug page's measurements (debug_stats.h), not from here. */
#ifndef SNX_STATS_H
#define SNX_STATS_H
#include "nx/runtime/ini.h"

/* The performance overlay's levels (settings.txt's stats). */
enum {
    STATS_LEVEL_OFF,
    STATS_LEVEL_MINIMAL,
    STATS_LEVEL_STANDARD,
    STATS_LEVEL_DETAILED,
    STATS_LEVEL_COUNT
};

/* stats_corner's: tl, tr, bl or br; another value keeps the corner it had, said in the log */
void stats_corner_set(const IniOpt* o, const char* v);
/* The corner stats draws in, 0 top left, 1 top right, 2 bottom left, 3 bottom right. */
int stats_corner(void);
/* The level, Off for an out-of-range value. */
int stats_level(void);
/* The next level, 0>1>2>3>0; an out-of-range value gives Off. */
int stats_level_next(int level);
#endif
