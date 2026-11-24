/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/runtime/arceus_graphics.h"
#include <stdlib.h>
#include <string.h>

const char* const g_arceus_graphics_names[ARCEUS_GFX_NAMES] = {
    "SNX_ARCEUS_TEMPORAL",
    "SNX_ARCEUS_TEMPORAL_VECTORS",
    "SNX_ARCEUS_TEMPORAL_LINKS",
    "SNX_ARCEUS_TEMPORAL_RECT",
    "SNX_ARCEUS_UPSCALER",
    "SNX_ARCEUS_SCENE_SCALE",
    "SNX_ARCEUS_FIXED_RESOLUTION",
};

static ArceusGraphicsRows g_rows = ARCEUS_GRAPHICS_ROWS_DEFAULT;

void arceus_graphics_map(const ArceusGraphicsRows* rows, const char* out[ARCEUS_GFX_NAMES]) {
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

const char* arceus_graphics_overridden(void) {
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++)
        if (getenv(g_arceus_graphics_names[i])) return g_arceus_graphics_names[i];
    const char* menu = getenv("SNX_ARCEUS_MENU");
    return menu && !strcmp(menu, "0") ? "SNX_ARCEUS_MENU" : NULL;
}

void arceus_graphics_set_rows(const ArceusGraphicsRows* rows) { g_rows = *rows; }

const char* arceus_graphics(const char* name) {
    int at = 0;
    while (at < ARCEUS_GFX_NAMES && strcmp(name, g_arceus_graphics_names[at])) at++;
    if (at == ARCEUS_GFX_NAMES || arceus_graphics_overridden()) return getenv(name);
    const char* mapped[ARCEUS_GFX_NAMES];
    arceus_graphics_map(&g_rows, mapped);
    return mapped[at];
}
