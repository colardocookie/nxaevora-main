/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The Arceus graphics page's map and its reader: every combination of the three rows against the sets the design
   names (coord research 2026-11-24 19:36 and 19:38), and the reader's precedence of the environment over the rows. */
#include "nx/runtime/arceus_graphics.c"
#include "nx/runtime/arceus_scene_scale.h"
#include <assert.h>
#include <stdio.h>

static void put(const char* name, const char* value) {
#ifdef _WIN32
    assert(!_putenv_s(name, value));
#else
    assert(!setenv(name, value, 1));
#endif
}
static void clear(const char* name) {
#ifdef _WIN32
    assert(!_putenv_s(name, "")); /* an empty value removes the variable on Windows */
#else
    assert(!unsetenv(name));
#endif
}
static void clear_all(void) {
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++) clear(g_arceus_graphics_names[i]);
    clear("SNX_ARCEUS_MENU");
    clear("SNX_ARCEUS_TEMPORAL_SHARPEN");
}
static int same(const char* a, const char* b) { return a == b || (a && b && !strcmp(a, b)); }

/* the design's table, written out per row 1 choice: TEMPORAL, VECTORS, LINKS, RECT, UPSCALER */
static const char* const want_aa[ARCEUS_AA_COUNT][5] = {
    {NULL, NULL, NULL, NULL, NULL},
    {"taa", "object", "1", "1", NULL},
    {"taa", "object", "1", "1", "fsr"},
    {"taa", "object", "1", "1", "dlss"},
    {"taa", "object", "1", "1", "xess"},
};
static const char* const want_scale[ARCEUS_SCALE_COUNT] = {NULL, "3/4", "2/3", "1/2"};
static const float want_value[ARCEUS_SCALE_COUNT] = {1.0f, 0.75f, (float)(2.0 / 3.0), 0.5f};

static void check_map(int aa, int scale, int fixed) {
    const ArceusGraphicsRows rows = {aa, scale, fixed};
    const char* out[ARCEUS_GFX_NAMES];
    arceus_graphics_map(&rows, out);
    for (int i = 0; i < 5; i++) assert(same(out[i], want_aa[aa][i]));
    const int upscaled = aa >= ARCEUS_AA_FSR;
    assert(same(out[ARCEUS_GFX_SCENE_SCALE], upscaled ? want_scale[scale] : NULL));
    /* free only with Off: TAA and the upscalers leave it to the code's default, on */
    assert(same(out[ARCEUS_GFX_FIXED_RESOLUTION], aa == ARCEUS_AA_OFF && !fixed ? "0" : NULL));
    /* both readers of the scale take the text (arceus_scene_scale.h), and the fixed resolution's readers take
       "0" as off and no value as on (temporal.c fixed_resolution_on, arceus_clock.c arceus_switch_enabled) */
    float value;
    const int asked = arceus_scene_scale_parse(out[ARCEUS_GFX_SCENE_SCALE], &value);
    if (out[ARCEUS_GFX_SCENE_SCALE]) {
        assert(asked == ARCEUS_SCENE_SCALE_USED && value == want_value[scale]);
    } else
        assert(asked == ARCEUS_SCENE_SCALE_OFF && value == 1.0f);
    const char* f = out[ARCEUS_GFX_FIXED_RESOLUTION];
    assert((!f || !strcmp(f, "1")) == (aa != ARCEUS_AA_OFF || fixed));
}

static void check_rows_used(const ArceusGraphicsRows* rows) {
    const char* out[ARCEUS_GFX_NAMES];
    arceus_graphics_map(rows, out);
    assert(!arceus_graphics_overridden());
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++) assert(arceus_graphics(g_arceus_graphics_names[i]) == out[i]);
}
/* the environment's value for every one of the seven, the very pointer getenv gives, and none of the rows' */
static void check_env_used(const char* why) {
    const char* by = arceus_graphics_overridden();
    assert(by && !strcmp(by, why));
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++)
        assert(arceus_graphics(g_arceus_graphics_names[i]) == getenv(g_arceus_graphics_names[i]));
}

int main(void) {
    int combinations = 0;
    for (int aa = 0; aa < ARCEUS_AA_COUNT; aa++)
        for (int scale = 0; scale < ARCEUS_SCALE_COUNT; scale++)
            for (int fixed = 0; fixed <= 1; fixed++, combinations++) check_map(aa, scale, fixed);
    assert(combinations == 40);
    /* the defaults hand the code nothing, and a value out of its range reads as its row's default */
    const char* out[ARCEUS_GFX_NAMES];
    const ArceusGraphicsRows defaults = ARCEUS_GRAPHICS_ROWS_DEFAULT;
    arceus_graphics_map(&defaults, out);
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++) assert(!out[i]);
    const ArceusGraphicsRows odd[] = {{-1, 0, 1}, {ARCEUS_AA_COUNT, 2, 1}, {0, -1, 1}, {0, 0, -1}, {0, 0, 2}};
    for (unsigned k = 0; k < sizeof odd / sizeof *odd; k++) {
        arceus_graphics_map(&odd[k], out);
        for (int i = 0; i < ARCEUS_GFX_NAMES; i++) assert(!out[i]);
    }
    const ArceusGraphicsRows wide = {ARCEUS_AA_DLSS, ARCEUS_SCALE_COUNT, 0};
    arceus_graphics_map(&wide, out);
    assert(!strcmp(out[ARCEUS_GFX_UPSCALER], "dlss") && !out[ARCEUS_GFX_SCENE_SCALE] &&
           !out[ARCEUS_GFX_FIXED_RESOLUTION]);

    /* the reader before any rows are set: the defaults, so nothing */
    clear_all();
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++) assert(!arceus_graphics(g_arceus_graphics_names[i]));
    check_rows_used(&defaults);
    /* rows set, nothing in the environment: the rows' values. A name of the TEMPORAL family that is not one of
       the seven is getenv's, and being set takes nothing from the rows */
    const ArceusGraphicsRows dlss_half = {ARCEUS_AA_DLSS, ARCEUS_SCALE_1_2, 0};
    arceus_graphics_set_rows(&dlss_half);
    check_rows_used(&dlss_half);
    assert(!strcmp(arceus_graphics("SNX_ARCEUS_SCENE_SCALE"), "1/2"));
    put("SNX_ARCEUS_TEMPORAL_SHARPEN", "0.5");
    check_rows_used(&dlss_half);
    assert(arceus_graphics("SNX_ARCEUS_TEMPORAL_SHARPEN") == getenv("SNX_ARCEUS_TEMPORAL_SHARPEN"));
    clear("SNX_ARCEUS_TEMPORAL_SHARPEN");
    assert(!arceus_graphics("SNX_ARCEUS_TEMPORAL_SHARPEN"));
    const ArceusGraphicsRows off_free = {ARCEUS_AA_OFF, ARCEUS_SCALE_2_3, 0};
    arceus_graphics_set_rows(&off_free);
    assert(!strcmp(arceus_graphics("SNX_ARCEUS_FIXED_RESOLUTION"), "0") && !arceus_graphics("SNX_ARCEUS_SCENE_SCALE"));
    /* one of the seven in the environment: all seven from the environment, never a mix */
    arceus_graphics_set_rows(&dlss_half);
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++) {
        put(g_arceus_graphics_names[i], "x");
        check_env_used(g_arceus_graphics_names[i]);
        assert(!strcmp(arceus_graphics(g_arceus_graphics_names[i]), "x"));
        clear(g_arceus_graphics_names[i]);
        check_rows_used(&dlss_half);
    }
#ifndef _WIN32
    /* set but empty is set: an empty SNX_ARCEUS_FIXED_RESOLUTION= reads as off today, an empty UPSCALER= as none */
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++) {
        put(g_arceus_graphics_names[i], "");
        check_env_used(g_arceus_graphics_names[i]);
        assert(!strcmp(arceus_graphics(g_arceus_graphics_names[i]), ""));
        clear(g_arceus_graphics_names[i]);
    }
    put("SNX_ARCEUS_MENU", "");
    check_rows_used(&dlss_half);
    clear("SNX_ARCEUS_MENU");
#endif
    /* SNX_ARCEUS_MENU=0 alone: the environment, which sets none of the seven, so nothing. Another value is the rows */
    put("SNX_ARCEUS_MENU", "0");
    check_env_used("SNX_ARCEUS_MENU");
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++) assert(!arceus_graphics(g_arceus_graphics_names[i]));
    const char* not_off[] = {"1", "00", "0 ", "off"};
    for (unsigned k = 0; k < sizeof not_off / sizeof *not_off; k++) {
        put("SNX_ARCEUS_MENU", not_off[k]);
        check_rows_used(&dlss_half);
    }
    /* MENU=0 and one of the seven: that name is the one the start line gives */
    put("SNX_ARCEUS_MENU", "0");
    put("SNX_ARCEUS_UPSCALER", "off");
    check_env_used("SNX_ARCEUS_UPSCALER");
    clear_all();
    check_rows_used(&dlss_half);
    printf("arceus_graphics: %d combinations and the reader's precedence passed\n", combinations);
    return 0;
}
