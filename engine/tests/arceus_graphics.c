/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The Arceus graphics page's map and its reader: every combination of the three rows against the sets the design
   names (coord research 2026-11-24 19:36 and 19:38), the reader's precedence of the environment over the rows, the
   title gate, reads before the decision, and the start line. */
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
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++) clear(arceus_graphics_name(i));
    clear("SNX_ARCEUS_MENU");
    clear("SNX_ARCEUS_TEMPORAL_SHARPEN");
}
static int same(const char* a, const char* b) { return a == b || (a && b && !strcmp(a, b)); }
/* the stand-in for vk_upscaler_present: dlss is missing, every other upscaler is there. Each question counted */
static unsigned present_calls;
static const char* present_name;
static int present_stub(const char* upscaler, char* why, size_t size) {
    present_calls++;
    present_name = upscaler;
    if (strcmp(upscaler, "dlss")) return 1;
    snprintf(why, size, "snx_upscaler_dlss.dll is not beside the program");
    return 0;
}

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
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++) assert(arceus_graphics(arceus_graphics_name(i)) == out[i]);
}
/* the environment's value for every one of the seven, the very pointer getenv gives, and none of the rows' */
static void check_env_used(const char* why) {
    const char* by = arceus_graphics_overridden();
    assert(by && !strcmp(by, why));
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++)
        assert(arceus_graphics(arceus_graphics_name(i)) == getenv(arceus_graphics_name(i)));
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

    /* reads before the decision: the environment's value, the very pointer, each one counted */
    clear_all();
    put("SNX_ARCEUS_UPSCALER", "fsr");
    assert(arceus_graphics("SNX_ARCEUS_UPSCALER") == getenv("SNX_ARCEUS_UPSCALER"));
    assert(!arceus_graphics("SNX_ARCEUS_SCENE_SCALE"));
    assert(!arceus_graphics("SNX_ARCEUS_TEMPORAL_SHARPEN")); /* not one of the seven: not counted */
    clear("SNX_ARCEUS_UPSCALER");
    char line[1024]; /* as main.c has it */
    static const char early[] = "arceus-graphics: 2 reads of the graphics page's switches came before the settings "
                                "were decided and took the environment's values";
    assert(arceus_graphics_line(line, sizeof line) && !strcmp(line, early));
    /* the title: the game's program and its update's, no other */
    assert(arceus_graphics_title(ARCEUS_GRAPHICS_TITLE) && arceus_graphics_title(ARCEUS_GRAPHICS_TITLE + 0x800));
    assert(!arceus_graphics_title(0) && !arceus_graphics_title(UINT64_C(0x0100000000010000)) &&
           !arceus_graphics_title(ARCEUS_GRAPHICS_TITLE + 0x1001) &&
           !arceus_graphics_title(ARCEUS_GRAPHICS_TITLE ^ UINT64_C(0x1000)));
    /* another game: its rows are the defaults, so nothing, and the line has only the early reads */
    const ArceusGraphicsRows defaults_too = ARCEUS_GRAPHICS_ROWS_DEFAULT;
    const ArceusGraphicsRows dlss_half = {ARCEUS_AA_DLSS, ARCEUS_SCALE_1_2, 0};
    arceus_graphics_decide(UINT64_C(0x0100000000010000), &dlss_half, NULL);
    check_rows_used(&defaults_too);
    assert(arceus_graphics_line(line, sizeof line) && !strcmp(line, early));
    /* the game: the rows' values, and the update's program the same. A name of the TEMPORAL family that is not one
       of the seven is getenv's, and being set takes nothing from the rows */
    arceus_graphics_decide(ARCEUS_GRAPHICS_TITLE + 0x800, &dlss_half, NULL);
    check_rows_used(&dlss_half);
    arceus_graphics_decide(ARCEUS_GRAPHICS_TITLE, &dlss_half, NULL);
    check_rows_used(&dlss_half);
    assert(!strcmp(arceus_graphics("SNX_ARCEUS_SCENE_SCALE"), "1/2"));
    static const char second[] = "\narceus-graphics: anti-aliasing DLSS, scene scale 1/2, fixed resolution on from the "
                                 "settings";
    assert(arceus_graphics_line(line, sizeof line) && !strncmp(line, early, sizeof early - 1) &&
           !strcmp(line + sizeof early - 1, second));
    char small[24]; /* cut, never past its end */
    assert(arceus_graphics_line(small, sizeof small) && strlen(small) == sizeof small - 1);
    put("SNX_ARCEUS_TEMPORAL_SHARPEN", "0.5");
    check_rows_used(&dlss_half);
    assert(arceus_graphics("SNX_ARCEUS_TEMPORAL_SHARPEN") == getenv("SNX_ARCEUS_TEMPORAL_SHARPEN"));
    clear("SNX_ARCEUS_TEMPORAL_SHARPEN");
    assert(!arceus_graphics("SNX_ARCEUS_TEMPORAL_SHARPEN"));
    const ArceusGraphicsRows off_free = {ARCEUS_AA_OFF, ARCEUS_SCALE_2_3, 0};
    arceus_graphics_decide(ARCEUS_GRAPHICS_TITLE, &off_free, NULL);
    assert(!strcmp(arceus_graphics("SNX_ARCEUS_FIXED_RESOLUTION"), "0") && !arceus_graphics("SNX_ARCEUS_SCENE_SCALE"));
    assert(arceus_graphics_line(line, sizeof line) &&
           !strcmp(strchr(line, '\n') + 1,
                   "arceus-graphics: anti-aliasing off, scene scale native, fixed resolution off from the settings"));
    /* rows that hand the code nothing: no line of their own (Off with a scale, or the defaults) */
    const ArceusGraphicsRows off_scaled = {ARCEUS_AA_OFF, ARCEUS_SCALE_1_2, 1};
    arceus_graphics_decide(ARCEUS_GRAPHICS_TITLE, &off_scaled, NULL);
    assert(arceus_graphics_line(line, sizeof line) && !strcmp(line, early));
    arceus_graphics_decide(ARCEUS_GRAPHICS_TITLE, &defaults_too, NULL);
    assert(arceus_graphics_line(line, sizeof line) && !strcmp(line, early));
    /* one of the seven in the environment: all seven from the environment, never a mix */
    arceus_graphics_decide(ARCEUS_GRAPHICS_TITLE, &dlss_half, NULL);
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++) {
        put(arceus_graphics_name(i), "x");
        check_env_used(arceus_graphics_name(i));
        assert(!strcmp(arceus_graphics(arceus_graphics_name(i)), "x"));
        char want[200];
        snprintf(want, sizeof want, "arceus-graphics: the environment sets %s: the settings' rows are not used",
                 arceus_graphics_name(i));
        assert(arceus_graphics_line(line, sizeof line) && !strcmp(strchr(line, '\n') + 1, want));
        clear(arceus_graphics_name(i));
        check_rows_used(&dlss_half);
    }
#ifndef _WIN32
    /* set but empty is set: an empty SNX_ARCEUS_FIXED_RESOLUTION= reads as off today, an empty UPSCALER= as none */
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++) {
        put(arceus_graphics_name(i), "");
        check_env_used(arceus_graphics_name(i));
        assert(!strcmp(arceus_graphics(arceus_graphics_name(i)), ""));
        clear(arceus_graphics_name(i));
    }
    put("SNX_ARCEUS_MENU", "");
    check_rows_used(&dlss_half);
    clear("SNX_ARCEUS_MENU");
#endif
    /* SNX_ARCEUS_MENU=0 alone: the environment, which sets none of the seven, so nothing. Another value is the rows */
    put("SNX_ARCEUS_MENU", "0");
    check_env_used("SNX_ARCEUS_MENU");
    for (int i = 0; i < ARCEUS_GFX_NAMES; i++) assert(!arceus_graphics(arceus_graphics_name(i)));
    assert(arceus_graphics_line(line, sizeof line) &&
           !strcmp(strchr(line, '\n') + 1, "arceus-graphics: SNX_ARCEUS_MENU=0: the settings' rows are not used"));
    const char* not_off[] = {"1", "00", "0 ", "off"};
    for (unsigned k = 0; k < sizeof not_off / sizeof *not_off; k++) {
        put("SNX_ARCEUS_MENU", not_off[k]);
        check_rows_used(&dlss_half);
    }
    /* an upscaler of the rows that is not there: TAA at native size, with its line. One that is: the rows as they are.
       The question is asked only for the rows' own upscaler, never with the environment's, nor for another game */
    present_calls = 0;
    clear("SNX_ARCEUS_MENU");
    arceus_graphics_decide(ARCEUS_GRAPHICS_TITLE, &dlss_half, present_stub);
    assert(present_calls == 1 && !strcmp(present_name, "dlss"));
    assert(!strcmp(arceus_graphics("SNX_ARCEUS_TEMPORAL"), "taa") && !arceus_graphics("SNX_ARCEUS_UPSCALER") &&
           !arceus_graphics("SNX_ARCEUS_SCENE_SCALE") && !arceus_graphics("SNX_ARCEUS_FIXED_RESOLUTION"));
    assert(arceus_graphics_line(line, sizeof line));
    {
        static const char missing[] = "arceus-graphics: DLSS is not available (snx_upscaler_dlss.dll is not beside the "
                                      "program): TAA at native size is used\n";
        const char* second_line = strchr(line, '\n') + 1;
        assert(!strncmp(second_line, missing, sizeof missing - 1));
        assert(!strcmp(second_line + sizeof missing - 1, "arceus-graphics: anti-aliasing TAA, scene scale native, "
                                                          "fixed resolution on from the settings"));
    }
    const ArceusGraphicsRows fsr_half = {ARCEUS_AA_FSR, ARCEUS_SCALE_1_2, 1};
    arceus_graphics_decide(ARCEUS_GRAPHICS_TITLE, &fsr_half, present_stub);
    assert(present_calls == 2 && !strcmp(present_name, "fsr"));
    check_rows_used(&fsr_half);
    assert(arceus_graphics_line(line, sizeof line) && !strstr(line, "is not available"));
    const ArceusGraphicsRows taa_only = {ARCEUS_AA_TAA, ARCEUS_SCALE_1_2, 0};
    arceus_graphics_decide(ARCEUS_GRAPHICS_TITLE, &taa_only, present_stub);
    arceus_graphics_decide(UINT64_C(0x0100000000010000), &dlss_half, present_stub);
    assert(present_calls == 2);
    put("SNX_ARCEUS_UPSCALER", "dlss");
    arceus_graphics_decide(ARCEUS_GRAPHICS_TITLE, &dlss_half, present_stub);
    assert(present_calls == 2 && !strcmp(arceus_graphics("SNX_ARCEUS_UPSCALER"), "dlss"));
    clear("SNX_ARCEUS_UPSCALER");
    put("SNX_ARCEUS_MENU", "0");
    arceus_graphics_decide(ARCEUS_GRAPHICS_TITLE, &dlss_half, present_stub);
    assert(present_calls == 2);
    clear("SNX_ARCEUS_MENU");
    arceus_graphics_decide(ARCEUS_GRAPHICS_TITLE, &dlss_half, NULL);
    check_rows_used(&dlss_half);
    /* MENU=0 and one of the seven: that name is the one the start line gives */
    put("SNX_ARCEUS_MENU", "0");
    put("SNX_ARCEUS_UPSCALER", "off");
    check_env_used("SNX_ARCEUS_UPSCALER");
    clear_all();
    check_rows_used(&dlss_half);
    printf("arceus_graphics: %d combinations, the precedence, the title gate and the start line passed\n",
           combinations);
    return 0;
}
