/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "nx/runtime/platform/hud.h"
#include "nx/runtime/platform/ui.h"
#include <stdio.h>
#include <string.h>

static struct {
    char debug[HUD_DEBUG_MAX];
} g_hud;

int hud_debug_set(const char* text) {
    int changed;
    ui_lock();
    changed = strcmp(g_hud.debug, text) != 0;
    snprintf(g_hud.debug, sizeof g_hud.debug, "%s", text);
    ui_unlock();
    return changed;
}

void hud_debug_get(char* out, size_t size) {
    if (out == NULL || size == 0) return;
    ui_lock();
    snprintf(out, size, "%s", g_hud.debug);
    ui_unlock();
}
