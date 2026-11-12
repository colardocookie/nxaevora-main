/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The Settings window's view of g_ini_sections (imgui_settings.h). Only the pages in PAGES show; the rest of the
   sections wait for their own work (docs/ui/imgui-plan.md). A row of the game's own (INI_IN_GAME) is left out. */
#include "nx/gpu/vk/imgui_settings.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/utilities/pad_map.h"
#include "nx/gpu/vk/scale.h"
#include "nx/runtime/ini.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/backend.h"

enum { SAVED_VALUE_MAX = 64 };
#define SAVED_FLOAT_EPSILON 1e-6 /* a saved float this close to the running one is the same */
#define FPS_LIMIT_KEY "fps_limit"
enum { FPS_RANGE_MIN = 10, FPS_RANGE_MAX = 1000 }; /* the range options_fps_set takes for a frame rate */

typedef struct {
    const char* name; /* the section of settings.txt */
    int window;       /* the window that shows it */
} Page;
static const Page PAGES[] = {{"Mouse", IMGUI_WINDOW_INPUT},       {"Keyboard", IMGUI_WINDOW_INPUT},
                             {"Controllers", IMGUI_WINDOW_INPUT}, {"Display", IMGUI_WINDOW_DISPLAY},
                             {"Graphics", IMGUI_WINDOW_DISPLAY}, {"Performance", IMGUI_WINDOW_DISPLAY}};
enum { PAGE_COUNT = sizeof PAGES / sizeof PAGES[0] };

/* Settings that the window shows as a choice from a list, not a slider. A value in the .ini that is not in the list
   (resolution 16) shows as custom. The label gives the render size docked / handheld: the game renders 1920 x 1080
   docked and two thirds of that handheld (the log of resolution 16 has targets of 30720 x 17280). Every step costs the
   square of its scale in GPU memory */
typedef struct {
    const char* key;
    const char* value;
    const char* label;
    double number; /* the row's value with this preset on: fps_limit's "game" is SNX_FPS_GAME */
} Preset;
static const Preset PRESETS[] = {
    {"resolution", "0.5", "0.5x (960 x 540 / 640 x 360)", 0.5},
    {"resolution", "0.75", "0.75x (1440 x 810 / 960 x 540)", 0.75},
    {"resolution", "1", "1x (1920 x 1080 / 1280 x 720)", 1},
    {"resolution", "1.333333", "1.33x (2560 x 1440 / 1707 x 960)", 1.333333},
    {"resolution", "1.5", "1.5x (2880 x 1620 / 1920 x 1080)", 1.5},
    {"resolution", "2", "2x (3840 x 2160 / 2560 x 1440)", 2},
    {"resolution", "3", "3x (5760 x 3240 / 3840 x 2160)", 3},
    {"resolution", "4", "4x (7680 x 4320 / 5120 x 2880)", 4},
    {"anisotropy", "0", "Game's", 0},
    {"anisotropy", "2", "2x", 2},
    {"anisotropy", "4", "4x", 4},
    {"anisotropy", "8", "8x", 8},
    {"anisotropy", "16", "16x", 16},
    {"fps_limit", "game", "Game's pace", SNX_FPS_GAME},
    {"fps_limit", "30", "30", 30},
    {"fps_limit", "60", "60", 60},
    {"fps_limit", "120", "120", 120},
    {"fps_limit", "144", "144", 144},
    {"fps_limit", "240", "240", 240},
    {"fps_limit", "0", "Unlimited", 0}};

/* keyboard_map and gamepad_map have their own rows in place of a text field. mouse_native_address is the guest address
   of a word that a code patch of the game reads: a value that is not 8 aligned and mapped breaks the mouse look, so
   only the launcher that installs the patch sets it. The showcase and the stats overlay have the F6 and F12 keys and
   the Debug window, so they have no rows */
/* A text setting that holds a whole number of megabytes: the window shows it as a number field */
static const char* const NUMBER_TEXT[] = {"heap_max_mb"};
enum { NUMBER_TEXT_MAX = 1048576 };

static const char* const HIDDEN[] = {"keyboard_map", "mouse_native_address", "gamepad_map", "showcase",
                                     "showcase_corner", "stats", "stats_corner"};

/* The rows of each player (gamepad_p1_device and the like) are left out: the window edits the settings every player
   shares and their button map */
#define PLAYER_KEY_PREFIX "gamepad_p"

_Static_assert(IMGUI_PADMAP_CONTROLS == BACKEND_PADMAP_CONTROLS && IMGUI_PADMAP_CONTROLS == SNX_PAD_CONTROLS,
               "the window lists the controls the backend has");
_Static_assert(IMGUI_PADMAP_NONE == SNX_PAD_NONE, "the window's none is the runtime's");
_Static_assert(IMGUI_PADMAP_BUTTONS == SNX_PAD_BUTTONS, "the window lists the buttons the console has");
_Static_assert(IMGUI_KEYBIND_COUNT == BACKEND_KEYBIND_COUNT, "the window lists the buttons the backend has");
_Static_assert(sizeof(((ImguiKeybind*)0)->keys) == sizeof(((BackendKeybind*)0)->keys), "the keys' line");

static const IniSection* section_of(int page) {
    if (page < 0 || page >= PAGE_COUNT) return NULL;
    for (int s = 0; s < g_ini_sections_count; s++)
        if (!strcmp(g_ini_sections[s].name, PAGES[page].name)) return &g_ini_sections[s];
    return NULL;
}

static int shown(const IniOpt* o) {
    if (o->flags & INI_IN_GAME) return 0;
    if (!strncmp(o->key, PLAYER_KEY_PREFIX, sizeof PLAYER_KEY_PREFIX - 1) && isdigit((unsigned char)o->key[sizeof PLAYER_KEY_PREFIX - 1]))
        return 0;
    for (size_t h = 0; h < sizeof HIDDEN / sizeof *HIDDEN; h++)
        if (!strcmp(o->key, HIDDEN[h])) return 0;
    return 1;
}

/* the i-th shown row of a section's tables, NULL past the last */
static const IniOpt* row_of(const IniSection* sec, int i) {
    for (int t = 0; t < 3 && sec->table[t]; t++)
        for (const IniOpt* o = sec->table[t]; o->key; o++)
            if (shown(o) && !i--) return o;
    return NULL;
}

static const IniOpt* find(const char* key) {
    for (int p = 0; p < PAGE_COUNT; p++) {
        const IniSection* sec = section_of(p);
        for (int i = 0; sec && row_of(sec, i); i++)
            if (!strcmp(row_of(sec, i)->key, key)) return row_of(sec, i);
    }
    return NULL;
}

static int preset_count(const char* key) {
    int n = 0;
    for (size_t p = 0; p < sizeof PRESETS / sizeof *PRESETS; p++) n += !strcmp(PRESETS[p].key, key);
    return n;
}

#define RESOLUTION_KEY "resolution"
#define NO_MEMORY_NOTE " - too much GPU memory"

int imgui_settings_fits(const char* key, double number) {
    return strcmp(key, RESOLUTION_KEY) ? 1 : vk_scale_fits((float)number);
}

int imgui_settings_preset(const char* key, int i, char* value, size_t vn, char* label, size_t ln, double* number) {
    for (size_t p = 0; p < sizeof PRESETS / sizeof *PRESETS; p++)
        if (!strcmp(PRESETS[p].key, key) && !i--) {
            snprintf(value, vn, "%s", PRESETS[p].value);
            snprintf(label, ln, "%s", PRESETS[p].label);
            *number = PRESETS[p].number;
            if (!imgui_settings_fits(key, *number) && strlen(label) + sizeof NO_MEMORY_NOTE <= ln)
                strcat(label, NO_MEMORY_NOTE);
            return 1;
        }
    return 0;
}

int imgui_settings_pages(void) { return PAGE_COUNT; }

const char* imgui_settings_page_name(int page) { return page >= 0 && page < PAGE_COUNT ? PAGES[page].name : ""; }

int imgui_settings_page_window(int page) { return page >= 0 && page < PAGE_COUNT ? PAGES[page].window : -1; }

int imgui_settings_rows(int page) {
    const IniSection* sec = section_of(page);
    int n = 0;
    while (sec && row_of(sec, n)) n++;
    return n;
}

/* a startup setting that this run changed shows the saved value, and says that the game must start again for it */
static void saved_value(const IniOpt* o, ImguiSettingRow* r) {
    char saved[SAVED_VALUE_MAX];
    if (!(o->flags & INI_STARTUP) || (o->type != INI_INT && o->type != INI_FLOAT)) return;
    if (!options_restart_value(o->key, saved, sizeof saved)) return;
    if (o->type == INI_INT && atoi(saved) != r->ivalue) {
        r->ivalue = atoi(saved);
        r->flags |= IMGUI_SETTING_RESTART;
    }
    if (o->type == INI_FLOAT && fabs(atof(saved) - (double)r->fvalue) > SAVED_FLOAT_EPSILON) {
        r->fvalue = (float)atof(saved);
        r->flags |= IMGUI_SETTING_RESTART;
    }
}

/* a text setting's value: the saved one when this run changed a startup setting, else what the game read. A text that
   holds a number (NUMBER_TEXT) becomes an int row */
static void text_row(const IniOpt* o, ImguiSettingRow* r) {
    char saved[SAVED_VALUE_MAX];
    if (r->type != IMGUI_SETTING_TEXT || !o->var || !o->size) return;
    snprintf(r->text, sizeof r->text, "%s", (const char*)o->var);
    if ((o->flags & INI_STARTUP) && options_restart_value(o->key, saved, sizeof saved) && strcmp(saved, r->text)) {
        snprintf(r->text, sizeof r->text, "%s", saved);
        r->flags |= IMGUI_SETTING_RESTART;
    }
    for (size_t k = 0; k < sizeof NUMBER_TEXT / sizeof *NUMBER_TEXT; k++)
        if (!strcmp(o->key, NUMBER_TEXT[k])) {
            r->type = IMGUI_SETTING_INT;
            r->lo = 0;
            r->hi = NUMBER_TEXT_MAX;
            r->ivalue = atoi(r->text);
        }
    r->has_words = o->words && strchr(o->words, '=');
}

/* fps_limit is a text in the .ini ("game", 0 or 10 to 1000) and a number in the running game (g_opt_fps): the row
   shows the number, as a choice of presets that also takes any value of the range */
static void fps_limit_row(ImguiSettingRow* r) {
    r->type = IMGUI_SETTING_INT;
    r->flags |= IMGUI_SETTING_CUSTOM;
    r->lo = FPS_RANGE_MIN;
    r->hi = FPS_RANGE_MAX;
    r->ivalue = __atomic_load_n(&g_opt_fps, __ATOMIC_RELAXED);
}

int imgui_settings_row(int page, int i, ImguiSettingRow* r) {
    const IniSection* sec = section_of(page);
    const IniOpt* o = sec ? row_of(sec, i) : NULL;
    if (!o) return 0;
    *r = (ImguiSettingRow){.key = o->key,
                           .name = o->name ? o->name : o->key,
                           .help = o->help ? o->help : "",
                           .type = o->type == INI_INT ? IMGUI_SETTING_INT : o->type == INI_FLOAT ? IMGUI_SETTING_FLOAT : IMGUI_SETTING_TEXT,
                           .flags = (o->flags & INI_STARTUP ? IMGUI_SETTING_STARTUP : 0) |
                                    (options_overridden(o->key) ? IMGUI_SETTING_LOCKED : 0),
                           .lo = o->lo,
                           .hi = o->hi,
                           .step = o->step > 0 ? o->step : 1,
                           .has_words = o->words && !strchr(o->words, '#'),
                           .presets = preset_count(o->key)};
    if (!strcmp(o->key, FPS_LIMIT_KEY)) fps_limit_row(r);
    if (o->type == INI_INT) r->ivalue = __atomic_load_n((const int*)o->var, __ATOMIC_RELAXED);
    if (o->type == INI_FLOAT) r->fvalue = *(const float*)o->var;
    saved_value(o, r);
    text_row(o, r);
    return 1;
}

int imgui_settings_text_word(const char* key, int i, char* value, size_t vn, char* label, size_t ln) {
    const IniOpt* o = find(key);
    const char* w = o && o->type == INI_TEXT ? o->words : NULL;
    while (w && *w) {
        size_t len = strcspn(w, "|"), eq = strcspn(w, "=|");
        if (!i--) {
            snprintf(value, vn, "%.*s", (int)eq, w);
            snprintf(label, ln, "%.*s", (int)(eq < len ? len - eq - 1 : len), w + (eq < len ? eq + 1 : 0));
            return 1;
        }
        w += len + (w[len] == '|');
    }
    return 0;
}

void imgui_settings_word(const char* key, int v, char* out, size_t n) {
    const IniOpt* o = find(key);
    if (o)
        ini_word(o, v, out, n);
    else
        out[0] = 0;
}

void imgui_settings_set(const char* key, const char* value) { backend_setting_request(key, value); }

void imgui_keybind_view(ImguiKeybind v[IMGUI_KEYBIND_COUNT]) {
    BackendKeybind b[BACKEND_KEYBIND_COUNT];
    backend_keybind_view(b);
    for (int i = 0; i < IMGUI_KEYBIND_COUNT; ++i) {
        v[i].button = b[i].button;
        memcpy(v[i].keys, b[i].keys, sizeof v[i].keys);
        v[i].custom = b[i].custom;
        v[i].locked = b[i].locked;
    }
}
void imgui_keybind_set(const char* button, const char* key) { backend_keybind_request(button, key); }
void imgui_keybind_capture(int on) { backend_key_capture(on); }
int imgui_keybind_captured(char* key, size_t n) { return backend_key_captured(key, n); }

/* the controls in pad_map.h's order, named by where they sit and by the labels of the common pads */
static const char* const PAD_CONTROLS[IMGUI_PADMAP_CONTROLS] = {
    "Bottom face button (A, Cross)", "Right face button (B, Circle)", "Left face button (X, Square)",
    "Top face button (Y, Triangle)", "Left shoulder (LB, L1)",        "Right shoulder (RB, R1)",
    "Left trigger (LT, L2)",         "Right trigger (RT, R2)",        "Left stick press",
    "Right stick press",             "Back (Select)",                 "Start",
    "D-pad Up",                      "D-pad Down",                    "D-pad Left",
    "D-pad Right"};

void imgui_padmap_view(ImguiPadMap* v) {
    BackendPadMap b;
    backend_padmap_view(&b);
    memcpy(v->bit, b.bit, sizeof v->bit);
    v->custom = b.custom;
    v->locked = b.locked;
}
const char* imgui_padmap_control_name(int control) {
    return control >= 0 && control < IMGUI_PADMAP_CONTROLS ? PAD_CONTROLS[control] : "";
}
const char* imgui_padmap_button_name(int bit) { return snx_pad_button_name(bit); }
void imgui_padmap_set(int control, int bit) { backend_padmap_request(control, bit); }
void imgui_padmap_reset(void) { backend_padmap_request(BACKEND_PADMAP_ALL, SNX_PAD_NONE); }
