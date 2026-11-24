/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/runtime/arceus_graphics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* written once by arceus_graphics_decide, before any thread that reads them starts */
static ArceusGraphicsRows g_rows = ARCEUS_GRAPHICS_ROWS_DEFAULT;
static int g_decided;
static unsigned g_early;      /* reads before the decision */
static char g_missing[512]; /* the upscaler the rows named, not there: its line */

static const char* const aa_words[ARCEUS_AA_COUNT] = {"off", "TAA", "FSR", "DLSS", "XeSS"};

void arceus_graphics_decide(uint64_t title_id, const ArceusGraphicsRows* rows, ArceusGraphicsPresent present) {
    static const ArceusGraphicsRows defaults = ARCEUS_GRAPHICS_ROWS_DEFAULT;
    const char* mapped[ARCEUS_GFX_NAMES];
    char why[384];
    g_rows = arceus_graphics_title(title_id) ? *rows : defaults;
    g_missing[0] = 0;
    arceus_graphics_map(&g_rows, mapped);
    /* only for the rows' own upscaler: with the environment's, the run asks for it as it always did */
    if (present && mapped[ARCEUS_GFX_UPSCALER] && !arceus_graphics_overridden() &&
        !present(mapped[ARCEUS_GFX_UPSCALER], why, sizeof why)) {
        snprintf(g_missing, sizeof g_missing, "arceus-graphics: %s is not available (%s): TAA at native size is used",
                 aa_words[g_rows.aa], why);
        g_rows.aa = ARCEUS_AA_TAA; /* the map sets no scale under TAA: native size */
    }
    g_decided = 1;
}

int arceus_graphics_line(char* out, size_t size) {
    const char* mapped[ARCEUS_GFX_NAMES];
    int n = 0, any = 0;
    if (!size) return 0;
    out[0] = 0;
    if (g_early)
        n = snprintf(out, size, "arceus-graphics: %u reads of the graphics page's switches came before the settings "
                     "were decided and took the environment's values", g_early);
    if (g_missing[0]) {
        const size_t at = n <= 0 ? 0 : (size_t)n < size ? (size_t)n : size - 1;
        const int more = snprintf(out + at, size - at, "%s%s", n > 0 ? "\n" : "", g_missing);
        n = more < 0 ? n : (int)at + more;
    }
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
