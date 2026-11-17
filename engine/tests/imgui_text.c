/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The UTF-16 count of a UTF-8 text and the cut that keeps it within a limit (src/nx/gpu/vk/imgui_text.c): the
   software keyboard's name limit, which the game counts in UTF-16 units. */
#include "nx/gpu/vk/imgui_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "imgui_text: FAILED: %s\n", what);
        exit(1);
    }
}

#define A "a"
#define E_ACUTE "\xC3\xA9"         /* U+00E9, two bytes, one unit */
#define KANA "\xE3\x81\x82"        /* U+3042, three bytes, one unit */
#define EMOJI "\xF0\x9F\x98\x80"   /* U+1F600, four bytes, two units (a pair) */

int main(void) {
    const char* mixed = A E_ACUTE KANA EMOJI A;

    /* one unit for a character up to three bytes, two for a four byte one, none for the bytes inside a character */
    check(imgui_text_utf16_units("", 0) == 0, "empty");
    check(imgui_text_utf16_units(A, 1) == 1, "ascii");
    check(imgui_text_utf16_units(E_ACUTE, 2) == 1, "two bytes");
    check(imgui_text_utf16_units(KANA, 3) == 1, "three bytes");
    check(imgui_text_utf16_units(EMOJI, 4) == 2, "four bytes are a pair");
    check(imgui_text_utf16_units(mixed, strlen(mixed)) == 1 + 1 + 1 + 2 + 1, "mixed text");
    check(imgui_text_utf16_units(mixed, 3) == 2, "a count of the first bytes only");

    /* the cut ends between characters and never passes the limit */
    check(imgui_text_fit_units(mixed, strlen(mixed), 100) == strlen(mixed), "all fits");
    check(imgui_text_fit_units(mixed, strlen(mixed), 0) == 0, "no room");
    check(imgui_text_fit_units(mixed, strlen(mixed), 1) == 1, "one unit: the first character");
    check(imgui_text_fit_units(mixed, strlen(mixed), 3) == 1 + 2 + 3, "three units: three characters");
    check(imgui_text_fit_units(mixed, strlen(mixed), 4) == 1 + 2 + 3, "a pair does not fit in one unit: it is not cut");
    check(imgui_text_fit_units(mixed, strlen(mixed), 5) == 1 + 2 + 3 + 4, "the pair fits in two");
    check(imgui_text_fit_units(mixed, strlen(mixed), 6) == strlen(mixed), "the last character");

    /* a character cut by n (the text ends inside it) is left out */
    check(imgui_text_fit_units(KANA, 2, 10) == 0, "a character that the length cuts");
    check(imgui_text_fit_units(A KANA, 3, 10) == 1, "the whole characters before it stay");

    puts("imgui_text: ok");
    return 0;
}
