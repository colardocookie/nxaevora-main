/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/runtime/arceus_graphics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* written once by arceus_graphics_decide, before any thread that reads them starts */
static ArceusGraphicsRows g_rows = ARCEUS_GRAPHICS_ROWS_DEFAULT;
static int g_decided;
static unsigned g_early; /* reads before the decision */

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

void arceus_graphics_decide(uint64_t title_id, const ArceusGraphicsRows* rows) {
    static const ArceusGraphicsRows defaults = ARCEUS_GRAPHICS_ROWS_DEFAULT;
    g_rows = arceus_graphics_title(title_id) ? *rows : defaults;
    g_decided = 1;
}

int arceus_graphics_line(char* out, size_t size) {
    static const char* const aa_words[ARCEUS_AA_COUNT] = {"off", "TAA", "FSR", "DLSS", "XeSS"};
    const char* mapped[ARCEUS_GFX_NAMES];
    int n = 0, any = 0;
    if (!size) return 0;
    out[0] = 0;
    if (g_early)
        n = snprintf(out, size, "arceus-graphics: %u reads of the graphics page's switches came before the settings "
                     "were decided and took the environment's values", g_early);
    arceus_graphics_map(&g_rows, mapped);
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++) any |= mapped[i] != NULL;
    if (!g_decided || !any) return n > 0; /* another game's rows are the defaults: nothing */
    const char* by = arceus_graphics_overridden();
    const char* sep = n > 0 ? "\n" : "";
    const size_t at = n <= 0 ? 0 : (size_t)n < size ? (size_t)n : size - 1; /* after the first line, cut or not */
    const int aa = g_rows.aa >= 0 && g_rows.aa < ARCEUS_AA_COUNT ? g_rows.aa : ARCEUS_AA_OFF;
    if (by && !strcmp(by, "SNX_ARCEUS_MENU"))
        snprintf(out + at, size - at, "%sarceus-graphics: SNX_ARCEUS_MENU=0: the settings' rows are not used", sep);
    else if (by)
        snprintf(out + at, size - at, "%sarceus-graphics: the environment sets %s: the settings' rows are not used",
                 sep, by);
    else
        snprintf(out + at, size - at, "%sarceus-graphics: anti-aliasing %s, scene scale %s, fixed resolution %s from "
                 "the settings", sep, aa_words[aa], mapped[ARCEUS_GFX_SCENE_SCALE] ? mapped[ARCEUS_GFX_SCENE_SCALE]
                 : "native", mapped[ARCEUS_GFX_FIXED_RESOLUTION] ? "off" : "on");
    return 1;
}

const char* arceus_graphics(const char* name) {
    int at = 0;
    while (at < ARCEUS_GFX_NAMES && strcmp(name, arceus_graphics_name(at))) at++;
    if (at == ARCEUS_GFX_NAMES) return getenv(name);
    if (!g_decided) {
        g_early++;
        return getenv(name);
    }
    if (arceus_graphics_overridden()) return getenv(name);
    const char* mapped[ARCEUS_GFX_NAMES];
    arceus_graphics_map(&g_rows, mapped);
    return mapped[at];
}
