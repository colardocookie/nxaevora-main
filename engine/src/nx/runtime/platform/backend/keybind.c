/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The keyboard map: the default keys, keyboard_map's text read into a table of keys, and the edits the Controller&Keyboard
   window makes to that text. No state of its own: the table is the caller's (sdl3.c keeps the one in use). */
#include "nx/runtime/platform/backend/keybind.h"
#include <stdio.h>
#include <string.h>
#include "nx/runtime/platform/log.h"

/* keys -> HidNpadButton bits; 40..47 are the stick keys, turned into axes in video_poll. By scancode, the key's
   position: the keycode depends on the layout and on the modifiers, so Shift pressed while 1 was held made its release
   a different key, and ZL stayed held */
uint64_t keybind_default(SDL_Scancode k) {
    switch (k) {
        case SDL_SCANCODE_F: return 1ull << 0;      /* A */
        case SDL_SCANCODE_LSHIFT: return 1ull << 1; /* B */
        case SDL_SCANCODE_R: return 1ull << 2;      /* X */
        case SDL_SCANCODE_SPACE: return 1ull << 3;  /* Y */
        case SDL_SCANCODE_LCTRL: return 1ull << 4;  /* L3 */
        case SDL_SCANCODE_Q: return 1ull << 6;      /* L */
        case SDL_SCANCODE_E: return 1ull << 7;      /* R */
        case SDL_SCANCODE_G: return 1ull << 10;     /* Plus */
        case SDL_SCANCODE_M: return 1ull << 11;     /* Minus */
        case SDL_SCANCODE_LEFT: return 1ull << 12;
        case SDL_SCANCODE_UP: return 1ull << 13;
        case SDL_SCANCODE_RIGHT: return 1ull << 14;
        case SDL_SCANCODE_DOWN: return 1ull << 15;
        case SDL_SCANCODE_A: return 1ull << 40;
        case SDL_SCANCODE_W: return 1ull << 41;
        case SDL_SCANCODE_D: return 1ull << 42;
        case SDL_SCANCODE_S: return 1ull << 43;
        default: break;
    }
    return 0;
}
/* keyboard_map's button names and the keys_held bit each sets (the stick bits are turned into axes in video_poll) */
const KeybindButton KEYBIND_BUTTONS[KEYBIND_BUTTON_COUNT] = {
    {"A", 0},           {"B", 1},           {"X", 2},           {"Y", 3},           {"L3", 4},
    {"R3", 5},          {"L", 6},           {"R", 7},           {"ZL", 8},          {"ZR", 9},
    {"Plus", 10},       {"Minus", 11},      {"Left", 12},       {"Up", 13},         {"Right", 14},
    {"Down", 15},       {"LStickLeft", 40}, {"LStickUp", 41},   {"LStickRight", 42}, {"LStickDown", 43},
    {"RStickLeft", 44}, {"RStickUp", 45},   {"RStickRight", 46}, {"RStickDown", 47},
};
typedef struct {
    int bit;
    SDL_Scancode key; /* SDL_SCANCODE_UNKNOWN: the button has no key */
} KeyEntry;

/* the runtime's own keys: a button on one of them would fire the hotkey too */
int keybind_key_reserved(SDL_Scancode k) {
    return k == SDL_SCANCODE_ESCAPE || (k >= SDL_SCANCODE_F1 && k <= SDL_SCANCODE_F12) || k == SDL_SCANCODE_PAUSE ||
           k == SDL_SCANCODE_LALT || k == SDL_SCANCODE_RALT;
}
/* one word, Button:Key: 0, else -1 */
static int key_entry_read(const char* at, size_t n, KeyEntry* out) {
    char key[48];
    size_t colon = 0, len;
    while (colon < n && at[colon] != ':') ++colon;
    if (colon == 0 || colon >= n - 1 || n - colon - 1 >= sizeof key) return -1;
    out->bit = -1;
    for (size_t i = 0; i < sizeof KEYBIND_BUTTONS / sizeof *KEYBIND_BUTTONS; ++i)
        if (strlen(KEYBIND_BUTTONS[i].name) == colon && SDL_strncasecmp(KEYBIND_BUTTONS[i].name, at, colon) == 0)
            out->bit = KEYBIND_BUTTONS[i].bit;
    if (out->bit < 0) return -1;
    len = n - colon - 1;
    for (size_t i = 0; i < len; ++i) key[i] = at[colon + 1 + i] == '_' ? ' ' : at[colon + 1 + i];
    key[len] = 0;
    if (SDL_strcasecmp(key, "None") == 0) {
        out->key = SDL_SCANCODE_UNKNOWN;
        return 0;
    }
    out->key = SDL_GetScancodeFromName(key);
    return out->key == SDL_SCANCODE_UNKNOWN || keybind_key_reserved(out->key) || out->key >= SDL_SCANCODE_COUNT ? -1 : 0;
}
/* keyboard_map's text as the buttons each key sends, into map. A button named in the text loses its default keys, and a
   key named in it belongs to the buttons named with it only. 0, or -1 for a word it cannot read (said when say): map is
   then not made */
int keybind_compile(const char* v, uint64_t map[SDL_SCANCODE_COUNT], int say) {
    KeyEntry e[KEYBIND_ENTRIES_MAX];
    int count = 0;
    uint64_t named = 0;
    const char* at = v ? v : "";
    while (*at) {
        size_t n = strcspn(at, " \t");
        if (!n) {
            ++at;
            continue;
        }
        if (count == KEYBIND_ENTRIES_MAX || key_entry_read(at, n, &e[count]) < 0) {
            if (say)
                LOG("settings: keyboard_map's \"%.*s\" is not a button and a key (A:F): the map stays as it was\n", (int)n,
                    at);
            return -1;
        }
        ++count;
        at += n;
    }
    for (int k = 0; k < SDL_SCANCODE_COUNT; ++k) map[k] = keybind_default((SDL_Scancode)k);
    for (int i = 0; i < count; ++i) named |= 1ull << e[i].bit;
    for (int k = 0; k < SDL_SCANCODE_COUNT; ++k) map[k] &= ~named;
    for (int i = 0; i < count; ++i)
        if (e[i].key) map[e[i].key] = 0;
    for (int i = 0; i < count; ++i)
        if (e[i].key) map[e[i].key] |= 1ull << e[i].bit;
    return 0;
}
/* the buttons that text names */
uint64_t keybind_named(const char* text) {
    uint64_t named = 0;
    for (const char* at = text; *at;) {
        size_t n = strcspn(at, " \t");
        KeyEntry e;
        if (!n) {
            ++at;
            continue;
        }
        if (key_entry_read(at, n, &e) == 0) named |= 1ull << e.bit;
        at += n;
    }
    return named;
}

/* the keys that send a button in map, as a line */
void keybind_keys(const uint64_t map[SDL_SCANCODE_COUNT], uint64_t bit, char* out, size_t size) {
    size_t at = 0;
    for (int k = 1; k < SDL_SCANCODE_COUNT; ++k) {
        if (!(map[k] & bit)) continue;
        at += (size_t)snprintf(out + at, size - at, "%s%s", at ? ", " : "", SDL_GetScancodeName((SDL_Scancode)k));
        if (at >= size) { /* cut: the rest of the keys do not fit the line */
            at = size - 1;
            break;
        }
    }
    if (!at) snprintf(out, size, "None");
}

/* text without the words of button, then Button:key when key is given (button NULL: nothing is left). 0, or -1 when it
   does not fit; out is then not to be used */
int keybind_edit(const char* text, const char* button, const char* key, char* out, size_t n) {
    size_t blen = button ? strlen(button) : 0, at = 0;
    out[0] = 0;
    for (const char* p = text; button && *p;) {
        size_t len = strcspn(p, " \t");
        if (!len) {
            ++p;
            continue;
        }
        if (strcspn(p, ":") != blen || SDL_strncasecmp(p, button, blen)) {
            int w = snprintf(out + at, n - at, "%s%.*s", at ? " " : "", (int)len, p);
            if (w < 0 || (size_t)w >= n - at) return -1;
            at += (size_t)w;
        }
        p += len;
    }
    if (button && key) {
        int w = snprintf(out + at, n - at, "%s%s:%s", at ? " " : "", button, key);
        if (w < 0 || (size_t)w >= n - at) return -1;
    }
    return 0;
}
