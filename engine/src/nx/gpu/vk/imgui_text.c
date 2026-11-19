/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/vk/imgui_text.h"

enum { UTF8_LEAD4 = 0xF0, UTF8_LEAD3 = 0xE0, UTF8_LEAD2 = 0xC0, UTF8_CONT_MASK = 0xC0, UTF8_CONT = 0x80 };

unsigned imgui_text_utf16_units(const char* s, size_t n) {
    unsigned units = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c >= UTF8_LEAD4)
            units += 2;
        else if ((c & UTF8_CONT_MASK) != UTF8_CONT)
            units += 1;
    }
    return units;
}

size_t imgui_text_fit_units(const char* s, size_t n, unsigned max) {
    unsigned units = 0;
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        size_t len = c >= UTF8_LEAD4 ? 4 : c >= UTF8_LEAD3 ? 3 : c >= UTF8_LEAD2 ? 2 : 1;
        unsigned add = c >= UTF8_LEAD4 ? 2 : 1;
        if (units + add > max || i + len > n) break;
        units += add;
        i += len;
    }
    return i;
}
