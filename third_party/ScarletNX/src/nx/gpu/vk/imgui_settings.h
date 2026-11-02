/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The game's settings as the Settings window reads them: pages from the .ini's sections (settings.txt), rows from their
   tables, so the window and the file cannot differ. Plain C, so that the window (C++) includes no runtime header. */
#ifndef SNX_IMGUI_SETTINGS_H
#define SNX_IMGUI_SETTINGS_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { IMGUI_SETTING_INT, IMGUI_SETTING_FLOAT, IMGUI_SETTING_TEXT };
enum {
    IMGUI_SETTING_STARTUP = 1, /* read when the game starts: a change is saved and applies at the next start */
    IMGUI_SETTING_LOCKED = 2,  /* the command line holds it for this run: a change is not taken */
    IMGUI_SETTING_RESTART = 4  /* the value shown is saved, and the running game has another: it applies at the next start */
};
typedef struct {
    const char* key;
    const char* name; /* the menus' name, or the key */
    const char* help; /* "" for none */
    int type, flags;
    double lo, hi;
    int step;          /* of an int with words or arrows, at least 1 */
    int has_words;     /* an int that is a choice (words between lo and hi), not a number */
    int presets;       /* a choice from a short list (imgui_settings_preset), 0 for none; INT or FLOAT */
    int ivalue;        /* INT */
    float fvalue;      /* FLOAT */
} ImguiSettingRow;

/* the windows that show pages: Controller&Keyboard and Display */
enum { IMGUI_WINDOW_INPUT, IMGUI_WINDOW_DISPLAY };

int imgui_settings_pages(void);
const char* imgui_settings_page_name(int page);
/* the window (IMGUI_WINDOW_) that shows the page, -1 for an index outside the pages */
int imgui_settings_page_window(int page);
int imgui_settings_rows(int page);
/* row i of page into r: 1, or 0 for an index outside the page */
int imgui_settings_row(int page, int i, ImguiSettingRow* r);
/* the word of an int choice's value v (the number when it has none) */
void imgui_settings_word(const char* key, int v, char* out, size_t n);
/* preset i of a setting that is chosen from a list (the resolution, the anisotropic filtering): its value as the .ini
   writes it, and its label. 1, or 0 for an index past the last */
int imgui_settings_preset(const char* key, int i, char* value, size_t vn, char* label, size_t ln);
/* key set to value (the .ini's text). The main thread applies it and saves it, so the row shows the old value for a
   moment */
void imgui_settings_set(const char* key, const char* value);

/* The key bindings (keyboard_map, which the rows above leave out): a row for each button of the console. The sizes are
   those of backend.h's BackendKeybind */
enum { IMGUI_KEYBIND_COUNT = 24, IMGUI_KEYBIND_KEYS = 64 };
typedef struct {
    const char* button;
    char keys[IMGUI_KEYBIND_KEYS]; /* the keys that send it now, "None" for none */
    int custom;                    /* the .ini names the button: its keys are not the defaults */
    int locked;                    /* the command line holds keyboard_map: the bindings do not change */
} ImguiKeybind;
void imgui_keybind_view(ImguiKeybind v[IMGUI_KEYBIND_COUNT]);
/* key: a name with _ for a space, "None" for no key, NULL for the defaults. A NULL button: the defaults of all */
void imgui_keybind_set(const char* button, const char* key);
void imgui_keybind_capture(int on);
/* 0 while the key is awaited, 1 with its name in key, -1 when it was cancelled, -2 for a key that cannot be bound */
int imgui_keybind_captured(char* key, size_t n);

/* The gamepad button map (gamepad_map, which the rows leave out): for each control of a gamepad the console button it
   sends now. The sizes are those of backend.h's BackendPadMap */
enum { IMGUI_PADMAP_CONTROLS = 16, IMGUI_PADMAP_BUTTONS = 16 };
typedef struct {
    int bit[IMGUI_PADMAP_CONTROLS]; /* the button each control sends: 0 to IMGUI_PADMAP_BUTTONS - 1, -1 for none */
    int custom;                     /* the .ini names a map: it is not the console's layout */
    int locked;                     /* the command line holds gamepad_map: the map does not change */
} ImguiPadMap;
void imgui_padmap_view(ImguiPadMap* v);
const char* imgui_padmap_control_name(int control);
enum { IMGUI_PADMAP_NONE = -1 }; /* the bit of a control that sends nothing */
const char* imgui_padmap_button_name(int bit);
/* control sends bit; the control that sent it gets control's button */
void imgui_padmap_set(int control, int bit);
/* the console's layout for all the controls */
void imgui_padmap_reset(void);

#ifdef __cplusplus
}
#endif
#endif
