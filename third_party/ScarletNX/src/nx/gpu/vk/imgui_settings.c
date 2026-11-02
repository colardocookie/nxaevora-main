/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The Settings window's view of g_ini_sections (imgui_settings.h). Only the pages in PAGES show; the rest of the
   sections wait for their own work (docs/imgui-plan.md). A row of the game's own (INI_IN_GAME) is left out. */
#include "nx/gpu/vk/imgui_settings.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/utilities/pad_map.h"
#include "nx/runtime/ini.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/backend.h"

enum { SAVED_VALUE_MAX = 64 };
#define SAVED_FLOAT_EPSILON 1e-6 /* a saved float this close to the running one is the same */

typedef struct {
    const char* name; /* the section of settings.txt */
    int window;       /* the window that shows it */
} Page;
static const Page PAGES[] = {{"Mouse", IMGUI_WINDOW_INPUT},       {"Keyboard", IMGUI_WINDOW_INPUT},
                             {"Controllers", IMGUI_WINDOW_INPUT}, {"Display", IMGUI_WINDOW_DISPLAY},
                             {"Graphics", IMGUI_WINDOW_DISPLAY}, {"Performance", IMGUI_WINDOW_DISPLAY}};
enum { PAGE_COUNT = sizeof PAGES / sizeof PAGES[0] };

/* keyboard_map and gamepad_map have their own rows in place of a text field. mouse_native_address is the guest address
   of a word that a code patch of the game reads: a value that is not 8 aligned and mapped breaks the mouse look, so
   only the launcher that installs the patch sets it */
/* Settings that the window shows as a choice from a list, not a slider. A value in the .ini that is not in the list
   (resolution 16) shows as custom. The sizes are the game's render size at 1x: 1920 x 1080 (the log of resolution 16
   has targets of 30720 x 17280), and every step costs the square of its scale in GPU memory */
typedef struct {
    const char* key;
    const char* value;
    const char* label;
} Preset;
static const Preset PRESETS[] = {
    {"resolution", "0.5", "0.5x (960 x 540)"},    {"resolution", "0.75", "0.75x (1440 x 810)"},
    {"resolution", "1", "1x (1920 x 1080)"},      {"resolution", "1.5", "1.5x (2880 x 1620)"},
    {"resolution", "2", "2x (3840 x 2160)"},      {"resolution", "3", "3x (5760 x 3240)"},
    {"resolution", "4", "4x (7680 x 4320)"},      {"anisotropy", "0", "Game's"},
    {"anisotropy", "2", "2x"},                    {"anisotropy", "4", "4x"},
    {"anisotropy", "8", "8x"},                    {"anisotropy", "16", "16x"}};

static const char* const HIDDEN[] = {"keyboard_map", "mouse_native_address", "gamepad_map"};

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

int imgui_settings_preset(const char* key, int i, char* value, size_t vn, char* label, size_t ln) {
    for (size_t p = 0; p < sizeof PRESETS / sizeof *PRESETS; p++)
        if (!strcmp(PRESETS[p].key, key) && !i--) {
            snprintf(value, vn, "%s", PRESETS[p].value);
            snprintf(label, ln, "%s", PRESETS[p].label);
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
    if (o->type == INI_INT) r->ivalue = __atomic_load_n((const int*)o->var, __ATOMIC_RELAXED);
    if (o->type == INI_FLOAT) r->fvalue = *(const float*)o->var;
    saved_value(o, r);
    return 1;
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
