/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_ARCEUS_SCENE_SCALE_H
#define SNX_ARCEUS_SCENE_SCALE_H
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
/* SNX_ARCEUS_SCENE_SCALE, the pure parts. Arceus 1.1.1 draws its 3D passes into the top left part of full-size
   targets, by a pair of scales its mode setter (main+CC3888) takes from a table of five pairs at main+3978740. The
   fourth pair, at main+3978758, is the default mode's and is 1.0 and 1.0. The switch puts one number there. */
#define ARCEUS_SCENE_SCALE_TABLE 0x3978740u /* from the module's base: ten floats */
#define ARCEUS_SCENE_SCALE_WORDS 10
#define ARCEUS_SCENE_SCALE_PAIR 6          /* the default pair's first word in the table */
#define ARCEUS_SCENE_SCALE_LEAST 0.5f
enum {
    ARCEUS_SCENE_SCALE_OFF,  /* unset, or 1.0 and above: nothing is touched */
    ARCEUS_SCENE_SCALE_USED, /* from 0.5 to below 1.0 */
    ARCEUS_SCENE_SCALE_BAD   /* below 0.5, not a number, not finite, or text after the number: refused */
};
/* The switch's text. A value is never moved into the range, so a typing error gives no scale nobody asked for. */
static inline int arceus_scene_scale_parse(const char* text, float* scale) {
    *scale = 1.0f;
    if (!text) return ARCEUS_SCENE_SCALE_OFF;
    char* end = NULL;
    const float value = strtof(text, &end);
    if (end == text || *end || !isfinite(value) || value < ARCEUS_SCENE_SCALE_LEAST) return ARCEUS_SCENE_SCALE_BAD;
    if (value >= 1.0f) return ARCEUS_SCENE_SCALE_OFF;
    *scale = value;
    return ARCEUS_SCENE_SCALE_USED;
}
enum {
    ARCEUS_SCENE_GUARD_OK,
    ARCEUS_SCENE_GUARD_GOVERNOR, /* the automatic governor is not off (SNX_ARCEUS_FIXED_RESOLUTION) */
    ARCEUS_SCENE_GUARD_PAIR,     /* the default pair is not exactly 1.0 and 1.0 */
    ARCEUS_SCENE_GUARD_TABLE     /* the pair after it is not exactly 0.5 and 0.5: this is not the table */
};
/* Whether the pair may be written: the table's ten words as they are now. Both words of the pair are looked at
   before either is changed. The three pairs before it are not exact in binary and are not tested. */
static inline int arceus_scene_scale_guard(int governor_off, const uint32_t words[ARCEUS_SCENE_SCALE_WORDS]) {
    if (!governor_off) return ARCEUS_SCENE_GUARD_GOVERNOR;
    if (words[ARCEUS_SCENE_SCALE_PAIR] != 0x3f800000u || words[ARCEUS_SCENE_SCALE_PAIR + 1] != 0x3f800000u)
        return ARCEUS_SCENE_GUARD_PAIR;
    if (words[ARCEUS_SCENE_SCALE_PAIR + 2] != 0x3f000000u || words[ARCEUS_SCENE_SCALE_PAIR + 3] != 0x3f000000u)
        return ARCEUS_SCENE_GUARD_TABLE;
    return ARCEUS_SCENE_GUARD_OK;
}
/* After the write: 1 when both words read back as the scale's bits. Else the write did not take. */
static inline int arceus_scene_scale_took(uint32_t bits, uint32_t first, uint32_t second) {
    return first == bits && second == bits;
}
#endif
