/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* UTF-8 text counted as the game counts it (UTF-16 units), for the software keyboard's limit. Plain C. */
#ifndef SNX_IMGUI_TEXT_H
#define SNX_IMGUI_TEXT_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the UTF-16 units of the first n bytes of s: a four byte character is a pair */
unsigned imgui_text_utf16_units(const char* s, size_t n);
/* the longest start of the n bytes that holds at most max units, in bytes: it ends between characters */
size_t imgui_text_fit_units(const char* s, size_t n, unsigned max);

#ifdef __cplusplus
}
#endif
#endif
