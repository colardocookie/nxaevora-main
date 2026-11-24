/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_ARCEUS_GRAPHICS_H
#define SNX_ARCEUS_GRAPHICS_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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
/* Pokémon Legends: Arceus, the base game's title ID. Its update's program has its own, 0x800 above */
#define ARCEUS_GRAPHICS_TITLE UINT64_C(0x01001F5010DFA000)

/* the seven names, in the order of ARCEUS_GFX_*. Inline, with the next, for the launcher, which shares the run's
   environment and links none of the runtime */
static inline const char* arceus_graphics_name(int i) {
    static const char* const names[ARCEUS_GFX_NAMES] = {
        "SNX_ARCEUS_TEMPORAL",
        "SNX_ARCEUS_TEMPORAL_VECTORS",
        "SNX_ARCEUS_TEMPORAL_LINKS",
        "SNX_ARCEUS_TEMPORAL_RECT",
        "SNX_ARCEUS_UPSCALER",
        "SNX_ARCEUS_SCENE_SCALE",
        "SNX_ARCEUS_FIXED_RESOLUTION",
    };
    return i >= 0 && i < ARCEUS_GFX_NAMES ? names[i] : NULL;
}
/* the name that makes every one of the seven come from the environment for this run: the first of the seven that is
   set (an empty value included), else SNX_ARCEUS_MENU when it is 0, else NULL */
static inline const char* arceus_graphics_overridden(void) {
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++)
        if (getenv(arceus_graphics_name(i))) return arceus_graphics_name(i);
    const char* menu = getenv("SNX_ARCEUS_MENU");
    return menu && !strcmp(menu, "0") ? "SNX_ARCEUS_MENU" : NULL;
}

/* the rows to the seven values, each a text or NULL for not set. Inline for the launcher's import as well */
static inline void arceus_graphics_map(const ArceusGraphicsRows* rows, const char* out[ARCEUS_GFX_NAMES]) {
    static const char* const upscalers[ARCEUS_AA_COUNT] = {NULL, NULL, "fsr", "dlss", "xess"};
    static const char* const scales[ARCEUS_SCALE_COUNT] = {NULL, "3/4", "2/3", "1/2"};
    const int aa = rows->aa >= 0 && rows->aa < ARCEUS_AA_COUNT ? rows->aa : ARCEUS_AA_OFF;
    const int scale = rows->scale >= 0 && rows->scale < ARCEUS_SCALE_COUNT ? rows->scale : ARCEUS_SCALE_NATIVE;
    const int temporal = aa != ARCEUS_AA_OFF;
    out[ARCEUS_GFX_TEMPORAL] = temporal ? "taa" : NULL;
    out[ARCEUS_GFX_VECTORS] = temporal ? "object" : NULL;
    out[ARCEUS_GFX_LINKS] = temporal ? "1" : NULL;
    out[ARCEUS_GFX_RECT] = temporal ? "1" : NULL;
    out[ARCEUS_GFX_UPSCALER] = upscalers[aa];
    /* the scale needs an upscaler. TAA and the upscalers keep the fixed resolution on, the code's default */
    out[ARCEUS_GFX_SCENE_SCALE] = upscalers[aa] ? scales[scale] : NULL;
    out[ARCEUS_GFX_FIXED_RESOLUTION] = !temporal && rows->fixed == 0 ? "0" : NULL;
}
/* 1 for the game's program or its update's (the title ID with its low 12 bits left out) */
static inline int arceus_graphics_title(uint64_t title_id) {
    return (title_id & ~UINT64_C(0xfff)) == ARCEUS_GRAPHICS_TITLE;
}
/* whether an upscaler's adapter and library are there (gpu/vk/upscaler.h vk_upscaler_present): 1, else 0 with why */
typedef int (*ArceusGraphicsPresent)(const char* upscaler, char* why, size_t size);
/* The one decision, at start before any of the seven is read and before any thread starts: the rows for this game's
   title, or the defaults for any other title. Until it is made, a read gives the environment's value and is counted.
   When the rows are used and name an upscaler that present says is not there, they become TAA at Native. present is
   asked only then, never when the environment sets the switches, and may be NULL */
void arceus_graphics_decide(uint64_t title_id, const ArceusGraphicsRows* rows, ArceusGraphicsPresent present);
/* the start line of the decision, without its newline: 0 when there is none to say (the rows at their defaults, no read
   before the decision, or another game). Three lines at most, a newline between */
int arceus_graphics_line(char* out, size_t size);
/* in place of getenv for the seven names. Any other name is getenv's */
const char* arceus_graphics(const char* name);

#endif
