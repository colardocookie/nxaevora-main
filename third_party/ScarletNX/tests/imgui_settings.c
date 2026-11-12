/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The Settings window's view of the .ini's tables (gpu/vk/imgui_settings.c), on small tables of its own: which rows show,
   the choice lists (the render resolution, the anisotropic filtering, the FPS limit), the text rows (a choice of words, a
   number of megabytes), and the saved value that a startup setting shows after a change. */
#include "nx/gpu/vk/imgui_settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nx/gpu/vk/scale.h"
#include "nx/runtime/ini.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/backend.h"

/* ---- the runtime the view reads, replaced by what each case needs ---- */
int g_opt_fps;
static const char* g_locked_key;
static const char* g_saved_key;
static const char* g_saved_value;
static int g_scale_limit_x100 = 400; /* vk_scale_fits: the most scale (times 100) that fits */

int options_overridden(const char* key) { return g_locked_key && !strcmp(key, g_locked_key); }
int options_restart_value(const char* key, char* out, size_t n) {
    if (!g_saved_key || strcmp(key, g_saved_key)) return 0;
    snprintf(out, n, "%s", g_saved_value);
    return 1;
}
int vk_scale_fits(float scale) { return scale * 100.0f <= (float)g_scale_limit_x100 + 0.5f; }
void backend_setting_request(const char* key, const char* value) { (void)key, (void)value; }
void backend_keybind_view(BackendKeybind v[BACKEND_KEYBIND_COUNT]) { (void)v; }
void backend_keybind_request(const char* button, const char* key) { (void)button, (void)key; }
void backend_key_capture(int on) { (void)on; }
int backend_key_captured(char* name, size_t n) { return (void)name, (void)n, 0; }
void backend_padmap_view(BackendPadMap* v) { (void)v; }
void backend_padmap_request(int control, int bit) { (void)control, (void)bit; }

/* ---- the tables ---- */
static int g_showcase, g_fullscreen = 1, g_language, g_vsync = 1, g_aniso, g_resolution = 1;
static char g_fps_text[16] = "game", g_heap_text[32] = "512", g_jit_text[2] = "2";

#define NO_SET NULL
static const IniOpt DISPLAY[] = {
    {"showcase", "0", "hidden", INI_INT, 0, &g_showcase, 0, 1, NULL, "Showcase", "Off|On", 0, 0, 0},
    {"fullscreen", "1", "shown", INI_INT, 0, &g_fullscreen, 0, 1, NULL, "Fullscreen", "Off|On", 0, 0, 0},
    {"language", "1", "the game's own", INI_INT, INI_IN_GAME, &g_language, 0, 17, NULL, "Language", NULL, 0, 0, 0},
    {"gamepad_p1_device", "", "a player's", INI_INT, 0, &g_language, 0, 1, NULL, "Device", NULL, 0, 0, 0},
    {"stats_corner", "tl", "hidden", INI_TEXT, 0, g_jit_text, 0, 0, NULL, "Stats corner", "tl=Top left|tr=Top right", 0,
     0, sizeof g_jit_text},
    {NULL, NULL, NULL, 0, 0, NULL, 0, 0, NULL, NULL, NULL, 0, 0, 0}};
static const IniOpt GRAPHICS[] = {
    {"render_resolution", "1", "the size", INI_INT, INI_STARTUP, &g_resolution, 0, 3, NULL, "Render resolution",
     "720p|1080p|1440p|2160p", 0, 0, 0},
    {"anisotropy", "0", "filtering", INI_INT, INI_STARTUP, &g_aniso, 0, 16, NULL, "Anisotropic filtering", "Game's|#x", 0, 0, 0},
    {"vsync", "1", "sync", INI_INT, 0, &g_vsync, 0, 2, NULL, "VSync", "Off|On (FIFO)|On (Mailbox)", 0, 0, 0},
    {NULL, NULL, NULL, 0, 0, NULL, 0, 0, NULL, NULL, NULL, 0, 0, 0}};
static const IniOpt PERFORMANCE[] = {
    {"fps_limit", "game", "the limit", INI_TEXT, 0, g_fps_text, 0, 0, NO_SET, "FPS limit", NULL, 0, 0, sizeof g_fps_text},
    {"heap_max_mb", "0", "the heap", INI_TEXT, INI_STARTUP, g_heap_text, 0, 0, NO_SET, "Heap limit (MB)", NULL, 0, 0,
     sizeof g_heap_text},
    {"jit_opt", "2", "the optimization", INI_TEXT, INI_STARTUP, g_jit_text, 0, 0, NO_SET, "JIT optimization",
     "0=O0|1=O1|2=O2|3=O3|s=Os|z=Oz", 0, 0, sizeof g_jit_text},
    {NULL, NULL, NULL, 0, 0, NULL, 0, 0, NULL, NULL, NULL, 0, 0, 0}};

const IniSection g_ini_sections[] = {{"Mouse", 0, {NULL}, {NULL}},
                                     {"Keyboard", 0, {NULL}, {NULL}},
                                     {"Controllers", 0, {NULL}, {NULL}},
                                     {"Display", 0, {DISPLAY}, {NULL}},
                                     {"Graphics", 0, {GRAPHICS}, {NULL}},
                                     {"Performance", 0, {PERFORMANCE}, {NULL}}};
const int g_ini_sections_count = sizeof g_ini_sections / sizeof *g_ini_sections;

static void check(int ok, const char* what) {
    if (ok) return;
    fprintf(stderr, "imgui_settings: FAILED: %s\n", what);
    exit(1);
}

static int page_of(const char* name) {
    for (int p = 0; p < imgui_settings_pages(); ++p)
        if (!strcmp(imgui_settings_page_name(p), name)) return p;
    return -1;
}

static int row_of(const char* page, const char* key, ImguiSettingRow* r) {
    int p = page_of(page);
    for (int i = 0; p >= 0 && imgui_settings_row(p, i, r); ++i)
        if (!strcmp(r->key, key)) return 1;
    return 0;
}

int main(void) {
    ImguiSettingRow r;
    char value[32], label[96];
    double number;

    /* the pages and the window of each */
    check(imgui_settings_pages() == 6, "six pages");
    check(imgui_settings_page_window(page_of("Mouse")) == IMGUI_WINDOW_INPUT &&
              imgui_settings_page_window(page_of("Controllers")) == IMGUI_WINDOW_INPUT &&
              imgui_settings_page_window(page_of("Display")) == IMGUI_WINDOW_DISPLAY &&
              imgui_settings_page_window(page_of("Performance")) == IMGUI_WINDOW_DISPLAY &&
              imgui_settings_page_window(99) == -1,
          "the window of each page");

    /* the rows: the game's own, a player's, and the ones with a row of their own do not show */
    check(imgui_settings_rows(page_of("Display")) == 1 && row_of("Display", "fullscreen", &r), "the Display rows");
    check(!row_of("Display", "showcase", &r) && !row_of("Display", "stats_corner", &r) &&
              !row_of("Display", "language", &r) && !row_of("Display", "gamepad_p1_device", &r),
          "hidden rows");
    check(imgui_settings_rows(page_of("Performance")) == 3 && !imgui_settings_row(page_of("Display"), 1, &r) &&
              !imgui_settings_row(-1, 0, &r),
          "the end of a page");

    /* an int with words, an int with presets */
    check(row_of("Display", "fullscreen", &r) && r.type == IMGUI_SETTING_INT && r.has_words && r.ivalue == 1 &&
              !r.presets && !(r.flags & IMGUI_SETTING_STARTUP),
          "a choice of words");
    check(row_of("Graphics", "render_resolution", &r) && r.type == IMGUI_SETTING_INT && r.has_words && r.ivalue == 1 &&
              r.presets == 0 && (r.flags & IMGUI_SETTING_STARTUP) && !(r.flags & IMGUI_SETTING_RESTART),
          "the render resolution row");
    check(row_of("Graphics", "anisotropy", &r) && r.presets == 5 && !r.has_words, "the anisotropy row: a number with presets");

    /* the render resolution's words, and the note on a choice that needs too much GPU memory */
    g_scale_limit_x100 = 150;
    static const char* const want[] = {"720p", "1080p", "1440p", "2160p - too much GPU memory"};
    for (int c = 0; c < 4; c++) {
        imgui_settings_word("render_resolution", c, value, sizeof value);
        check(!strcmp(value, want[c]), "the word and the note of each choice at a limit of 1.5");
    }
    check(imgui_settings_fits("render_resolution", 3) == 0 && imgui_settings_fits("render_resolution", 1) == 1 &&
              imgui_settings_fits("anisotropy", 16.0) == 1,
          "only the render resolution is checked");
    g_scale_limit_x100 = 400;
    check(imgui_settings_preset("anisotropy", 0, value, sizeof value, label, sizeof label, &number) &&
              !strcmp(label, "Game's") && number == 0 && !imgui_settings_preset("nothing", 0, value, sizeof value, label, sizeof label, &number),
          "other lists, and a key with none");

    /* the scale of each choice, a choice outside 0..3, and the sizes the game renders at */
    check(vk_render_scale(0) == 2.0f / 3.0f && vk_render_scale(1) == 1.0f && vk_render_scale(2) == 4.0f / 3.0f &&
              vk_render_scale(3) == 2.0f && vk_render_scale(-1) == 1.0f && vk_render_scale(4) == 1.0f,
          "the scales of the choices");
    check((unsigned)(1920 * (2.0f / 3.0f) + 0.5f) == 1280 && (unsigned)(1080 * (2.0f / 3.0f) + 0.5f) == 720 &&
              (unsigned)(1920 * vk_render_scale(3) + 0.5f) == 3840 && (unsigned)(1080 * vk_render_scale(3) + 0.5f) == 2160,
          "the 720p and 2160p sizes");

    /* a startup setting that this run changed shows the saved value and says that it waits for a start */
    g_saved_key = "render_resolution";
    g_saved_value = "3";
    check(row_of("Graphics", "render_resolution", &r) && r.ivalue == 3 && (r.flags & IMGUI_SETTING_RESTART), "a saved int");
    g_saved_value = "1";
    check(row_of("Graphics", "render_resolution", &r) && r.ivalue == 1 && !(r.flags & IMGUI_SETTING_RESTART),
          "a saved int that is the running one");
    g_saved_key = "anisotropy";
    g_saved_value = "8";
    check(row_of("Graphics", "anisotropy", &r) && r.ivalue == 8 && (r.flags & IMGUI_SETTING_RESTART), "a saved int");
    g_saved_key = "vsync"; /* not a startup setting: the running value shows */
    g_saved_value = "2";
    check(row_of("Graphics", "vsync", &r) && r.ivalue == 1 && !(r.flags & IMGUI_SETTING_RESTART), "only a startup setting is saved");
    g_saved_key = NULL;

    /* the command line holds a setting */
    g_locked_key = "vsync";
    check(row_of("Graphics", "vsync", &r) && (r.flags & IMGUI_SETTING_LOCKED) && row_of("Graphics", "anisotropy", &r) &&
              !(r.flags & IMGUI_SETTING_LOCKED),
          "a locked row");
    g_locked_key = NULL;

    /* the FPS limit: a text in the file, a number here, with presets and a custom value in 10 to 1000 */
    g_opt_fps = -1; /* the game's pace */
    check(row_of("Performance", "fps_limit", &r) && r.type == IMGUI_SETTING_INT && r.ivalue == -1 &&
              (r.flags & IMGUI_SETTING_CUSTOM) && r.lo == 10 && r.hi == 1000 && r.presets == 7,
          "the FPS limit row");
    g_opt_fps = 144;
    check(row_of("Performance", "fps_limit", &r) && r.ivalue == 144, "the running limit");
    check(imgui_settings_preset("fps_limit", 0, value, sizeof value, label, sizeof label, &number) &&
              !strcmp(value, "game") && number == -1 && !strcmp(label, "Game's pace"),
          "the game's pace preset");
    check(imgui_settings_preset("fps_limit", 6, value, sizeof value, label, sizeof label, &number) &&
              !strcmp(value, "0") && number == 0 && !strcmp(label, "Unlimited"),
          "the unlimited preset");

    /* a text that is a choice of words: value=label */
    check(row_of("Performance", "jit_opt", &r) && r.type == IMGUI_SETTING_TEXT && r.has_words && !strcmp(r.text, "2") &&
              (r.flags & IMGUI_SETTING_STARTUP) && !(r.flags & IMGUI_SETTING_RESTART),
          "the JIT optimization row");
    check(imgui_settings_text_word("jit_opt", 0, value, sizeof value, label, sizeof label) && !strcmp(value, "0") &&
              !strcmp(label, "O0"),
          "the first word");
    check(imgui_settings_text_word("jit_opt", 4, value, sizeof value, label, sizeof label) && !strcmp(value, "s") &&
              !strcmp(label, "Os"),
          "a word with a letter");
    check(imgui_settings_text_word("jit_opt", 5, value, sizeof value, label, sizeof label) && !strcmp(value, "z") &&
              !strcmp(label, "Oz") && !imgui_settings_text_word("jit_opt", 6, value, sizeof value, label, sizeof label),
          "the last word and the end");
    check(!imgui_settings_text_word("fullscreen", 0, value, sizeof value, label, sizeof label) &&
              !imgui_settings_text_word("nothing", 0, value, sizeof value, label, sizeof label),
          "an int and an unknown key have no text words");
    g_saved_key = "jit_opt";
    g_saved_value = "3";
    check(row_of("Performance", "jit_opt", &r) && !strcmp(r.text, "3") && (r.flags & IMGUI_SETTING_RESTART),
          "a saved text");
    g_saved_value = "2";
    check(row_of("Performance", "jit_opt", &r) && !strcmp(r.text, "2") && !(r.flags & IMGUI_SETTING_RESTART),
          "a saved text that is the running one");
    g_saved_key = NULL;

    /* a text that holds a whole number of megabytes is a number */
    check(row_of("Performance", "heap_max_mb", &r) && r.type == IMGUI_SETTING_INT && r.ivalue == 512 && r.lo == 0 &&
              r.hi == 1048576 && !r.has_words,
          "the heap limit row");
    g_saved_key = "heap_max_mb";
    g_saved_value = "2048";
    check(row_of("Performance", "heap_max_mb", &r) && r.ivalue == 2048 && (r.flags & IMGUI_SETTING_RESTART),
          "a saved number in a text");
    g_saved_key = NULL;

    puts("imgui_settings: ok");
    return 0;
}
