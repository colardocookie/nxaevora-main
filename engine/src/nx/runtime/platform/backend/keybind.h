/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The keyboard map (keybind.c): keyboard_map's text and the table of keys it makes. A key is an SDL scancode, and a
   table entry holds the keys_held bits the key sends (bits 40 to 47 are the sticks, turned into axes by video_poll). */
#ifndef SNX_KEYBIND_H
#define SNX_KEYBIND_H
#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdint.h>

enum { KEYBIND_BUTTON_COUNT = 24, KEYBIND_ENTRIES_MAX = 64 };
typedef struct {
    const char* name; /* keyboard_map's name of the button */
    int bit;          /* the keys_held bit it sets */
} KeybindButton;
extern const KeybindButton KEYBIND_BUTTONS[KEYBIND_BUTTON_COUNT];

/* the buttons a key sends without a keyboard_map */
uint64_t keybind_default(SDL_Scancode k);
/* the runtime's own keys (Esc, F1 to F12, Pause, Alt): a button on one of them would fire the hotkey too */
int keybind_key_reserved(SDL_Scancode k);
/* keyboard_map's text as the buttons each key sends, into map. A button named in the text loses its default keys, and a
   key named in it belongs to the buttons named with it only. 0, or -1 for a word it cannot read (said when say): map is
   then not made */
int keybind_compile(const char* v, uint64_t map[SDL_SCANCODE_COUNT], int say);
/* the buttons (a bit each) that text names */
uint64_t keybind_named(const char* text);
/* the keys that send a button in map, as a line; "None" for none, cut where the line ends */
void keybind_keys(const uint64_t map[SDL_SCANCODE_COUNT], uint64_t bit, char* out, size_t size);
/* text without the words of button, then Button:key when key is given (button NULL: nothing is left). 0, or -1 when it
   does not fit; out is then not to be used */
int keybind_edit(const char* text, const char* button, const char* key, char* out, size_t n);

#endif
