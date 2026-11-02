/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The keyboard map (runtime/platform/backend/keybind.c): the default keys, keyboard_map's text compiled into a table of
   keys, the line of keys that the window shows for a button, and the edits the window makes to the text. */
#include "nx/runtime/platform/backend/keybind.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_said[256];
void logf_(int lvl, const char* fmt, ...) {
    va_list ap;
    (void)lvl;
    va_start(ap, fmt);
    vsnprintf(g_said, sizeof g_said, fmt, ap);
    va_end(ap);
}

static void check(int ok, const char* what) {
    if (ok) return;
    fprintf(stderr, "keybind: FAILED: %s\n", what);
    exit(1);
}

enum { BIT_A = 0, BIT_B = 1, BIT_L = 6, BIT_ZL = 8, BIT_LEFT = 12, BIT_LSTICK_UP = 41 };
#define BIT(b) (1ull << (b))

static uint64_t g_map[SDL_SCANCODE_COUNT];

static void defaults(uint64_t map[SDL_SCANCODE_COUNT]) {
    for (int k = 0; k < SDL_SCANCODE_COUNT; ++k) map[k] = keybind_default((SDL_Scancode)k);
}

int main(void) {
    char line[64], out[128];
    uint64_t def[SDL_SCANCODE_COUNT];
    defaults(def);

    /* the default keys, and the table of the buttons */
    check(keybind_default(SDL_SCANCODE_F) == BIT(BIT_A) && keybind_default(SDL_SCANCODE_W) == BIT(BIT_LSTICK_UP) &&
              keybind_default(SDL_SCANCODE_LEFT) == BIT(BIT_LEFT) && keybind_default(SDL_SCANCODE_ESCAPE) == 0,
          "default keys");
    for (int i = 0; i < KEYBIND_BUTTON_COUNT; ++i)
        check(KEYBIND_BUTTONS[i].name && KEYBIND_BUTTONS[i].bit >= 0 && KEYBIND_BUTTONS[i].bit < 64, "button table");

    /* the runtime's own keys */
    check(keybind_key_reserved(SDL_SCANCODE_ESCAPE) && keybind_key_reserved(SDL_SCANCODE_F1) &&
              keybind_key_reserved(SDL_SCANCODE_F12) && keybind_key_reserved(SDL_SCANCODE_PAUSE) &&
              keybind_key_reserved(SDL_SCANCODE_LALT) && keybind_key_reserved(SDL_SCANCODE_RALT) &&
              !keybind_key_reserved(SDL_SCANCODE_F) && !keybind_key_reserved(SDL_SCANCODE_F13),
          "reserved keys");

    /* an empty text is the defaults */
    check(keybind_compile("", g_map, 0) == 0 && !memcmp(g_map, def, sizeof def), "empty text");
    check(keybind_compile(NULL, g_map, 0) == 0 && !memcmp(g_map, def, sizeof def), "no text");

    /* a button that the text names loses its default keys, and a key it names belongs to the buttons named with it */
    check(keybind_compile("A:G", g_map, 0) == 0, "A:G");
    check(g_map[SDL_SCANCODE_F] == 0 && g_map[SDL_SCANCODE_G] == BIT(BIT_A),
          "A moves to G: F is free, G is only A (Plus lost it)");
    check(g_map[SDL_SCANCODE_Q] == BIT(BIT_L), "a key the text does not name keeps its button");
    check(keybind_compile("ZL:Q", g_map, 0) == 0 && g_map[SDL_SCANCODE_Q] == BIT(BIT_ZL), "ZL takes Q from L");
    check(keybind_compile("a:f", g_map, 0) == 0 && g_map[SDL_SCANCODE_F] == BIT(BIT_A), "names in any case");
    check(keybind_compile("A:F B:F", g_map, 0) == 0 && g_map[SDL_SCANCODE_F] == (BIT(BIT_A) | BIT(BIT_B)),
          "two buttons on one key");
    check(keybind_compile("A:Left_Shift", g_map, 0) == 0 && g_map[SDL_SCANCODE_LSHIFT] == BIT(BIT_A) &&
              g_map[SDL_SCANCODE_F] == 0,
          "_ stands for a space in a key name");
    check(keybind_compile("A:None", g_map, 0) == 0 && g_map[SDL_SCANCODE_F] == 0, "None frees a button");
    check(keybind_compile("  A:G \t ZL:Q  ", g_map, 0) == 0 && g_map[SDL_SCANCODE_G] == BIT(BIT_A),
          "spaces and tabs between words");

    /* a word that cannot be read: -1, the table is not touched, and it is said when asked */
    memset(g_map, 0x5A, sizeof g_map);
    g_said[0] = 0;
    check(keybind_compile("A:G Nonsense", g_map, 1) == -1 && g_map[0] == 0x5A5A5A5A5A5A5A5Aull,
          "a word that is not a pair");
    check(strstr(g_said, "Nonsense") != NULL, "the bad word is named in the log");
    g_said[0] = 0;
    check(keybind_compile("Q:F", g_map, 0) == -1 && !g_said[0], "an unknown button, not said when say is 0");
    check(keybind_compile("A:NoSuchKey", g_map, 0) == -1, "an unknown key");
    check(keybind_compile("A:Escape", g_map, 0) == -1 && keybind_compile("A:F5", g_map, 0) == -1, "a reserved key");
    check(keybind_compile("A:", g_map, 0) == -1 && keybind_compile(":F", g_map, 0) == -1, "a missing half");
    {
        char many[KEYBIND_ENTRIES_MAX * 5 + 16] = "";
        for (int i = 0; i < KEYBIND_ENTRIES_MAX; ++i) strcat(many, "A:F ");
        check(keybind_compile(many, g_map, 0) == 0, "the most words");
        strcat(many, "A:F");
        check(keybind_compile(many, g_map, 0) == -1, "one word past the most");
    }

    /* the buttons that a text names: those it can read, whatever the rest */
    check(keybind_named("") == 0 && keybind_named("A:F ZL:Q") == (BIT(BIT_A) | BIT(BIT_ZL)) &&
              keybind_named("bogus:X ZL:Q") == BIT(BIT_ZL),
          "named buttons");

    /* the line of keys of a button */
    defaults(g_map);
    keybind_keys(g_map, BIT(BIT_A), line, sizeof line);
    check(!strcmp(line, "F"), "default key of A");
    keybind_keys(g_map, BIT(BIT_ZL), line, sizeof line);
    check(!strcmp(line, "None"), "ZL has no default key");
    check(keybind_compile("A:G A:F", g_map, 0) == 0, "two keys for A");
    keybind_keys(g_map, BIT(BIT_A), line, sizeof line);
    check(!strcmp(line, "F, G"), "keys in scancode order, with commas");
    keybind_keys(g_map, BIT(BIT_A), line, 4);
    check(strlen(line) == 3, "a line that does not fit is cut");

    /* the edits: the words of a button go, then its new word is added at the end */
    check(keybind_edit("A:F B:Left_Shift", "A", "G", out, sizeof out) == 0 && !strcmp(out, "B:Left_Shift A:G"),
          "replace a word");
    check(keybind_edit("A:F B:G", "a", NULL, out, sizeof out) == 0 && !strcmp(out, "B:G"), "remove a word, any case");
    check(keybind_edit("L3:X L:Y", "L", "Q", out, sizeof out) == 0 && !strcmp(out, "L3:X L:Q"), "L is not L3");
    check(keybind_edit("A:F B:G", NULL, NULL, out, sizeof out) == 0 && !strcmp(out, ""), "no button: nothing is left");
    check(keybind_edit("", "A", "F", out, sizeof out) == 0 && !strcmp(out, "A:F"), "a first word has no space before it");
    check(keybind_edit("  A:F   B:G ", "ZL", "Q", out, sizeof out) == 0 && !strcmp(out, "A:F B:G ZL:Q"),
          "spaces are one space");
    check(keybind_edit("A:F B:G", "ZL", "Q", out, 8) == -1, "a text that does not fit");
    check(keybind_edit("A:F", "B", "G", out, 8) == 0 && !strcmp(out, "A:F B:G"), "a text that fills the room");
    check(keybind_edit("A:F", "B", "G", out, 7) == -1, "one byte too few");

    puts("keybind: ok");
    return 0;
}
