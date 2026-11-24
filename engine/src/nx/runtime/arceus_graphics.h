/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_ARCEUS_GRAPHICS_H
#define SNX_ARCEUS_GRAPHICS_H
/* The Arceus graphics page's three rows and the seven SNX_ARCEUS_* names they stand for. arceus_graphics_map turns
   the rows into the seven values, each a text or NULL for not set, and hands out only the sets the user has played.
   arceus_graphics is the one reader of the seven: the environment's value when the environment sets any of them,
   else the rows' value. */

enum { ARCEUS_AA_OFF, ARCEUS_AA_TAA, ARCEUS_AA_FSR, ARCEUS_AA_DLSS, ARCEUS_AA_XESS, ARCEUS_AA_COUNT };
enum { ARCEUS_SCALE_NATIVE, ARCEUS_SCALE_3_4, ARCEUS_SCALE_2_3, ARCEUS_SCALE_1_2, ARCEUS_SCALE_COUNT };
enum {
    ARCEUS_GFX_TEMPORAL,
    ARCEUS_GFX_VECTORS,
    ARCEUS_GFX_LINKS,
    ARCEUS_GFX_RECT,
    ARCEUS_GFX_UPSCALER,
    ARCEUS_GFX_SCENE_SCALE,
    ARCEUS_GFX_FIXED_RESOLUTION,
    ARCEUS_GFX_NAMES
};

/* aa an ARCEUS_AA_*, scale an ARCEUS_SCALE_*, fixed 0 for Off and 1 for On. A value out of its range reads as that
   row's default (Off, Native, On) */
typedef struct {
    int aa, scale, fixed;
} ArceusGraphicsRows;
#define ARCEUS_GRAPHICS_ROWS_DEFAULT {ARCEUS_AA_OFF, ARCEUS_SCALE_NATIVE, 1}

/* the seven names, in the order of ARCEUS_GFX_* */
extern const char* const g_arceus_graphics_names[ARCEUS_GFX_NAMES];

void arceus_graphics_map(const ArceusGraphicsRows* rows, const char* out[ARCEUS_GFX_NAMES]);
/* the name that makes every one of the seven come from the environment for this run: the first of the seven that is
   set (an empty value included), else SNX_ARCEUS_MENU when it is 0, else NULL */
const char* arceus_graphics_overridden(void);
/* the rows the reader maps, set once at start before any reader runs. Until then they are the defaults */
void arceus_graphics_set_rows(const ArceusGraphicsRows* rows);
/* in place of getenv for the seven names. Any other name is getenv's */
const char* arceus_graphics(const char* name);

#endif
