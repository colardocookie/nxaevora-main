/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The Debug settings' HUD list (runtime/platform/hud.c): the set and the copy
   getter, with the copy cut to the buffer's size. */
#include "nx/runtime/platform/hud.h"
#include "nx/runtime/platform/plat.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ui.c's lock and log, without the platform layer */
void pm_lock(PMutex* m) { (void)m; }
void pm_unlock(PMutex* m) { (void)m; }
void logf_(int lvl, const char* fmt, ...) {
    (void)lvl;
    (void)fmt;
}

int main(void) {
    static const char list[] = "log=2, vk_validation=1, shader_corpus";
    static const char longer[] = "0123456789abcdef";
    char out[8];
    char full[HUD_DEBUG_MAX];
    /* none: no change, and the copy is empty */
    assert(!hud_debug_set(""));
    hud_debug_get(out, sizeof out);
    assert(out[0] == '\0');
    /* the set says whether it changed, and the copy gets the whole list */
    assert(hud_debug_set(list) && !hud_debug_set(list));
    hud_debug_get(full, sizeof full);
    assert(!strcmp(full, list));
    /* a copy cut to the buffer's size keeps size - 1 letters and the NUL */
    assert(hud_debug_set(longer));
    hud_debug_get(out, sizeof out);
    assert(!strcmp(out, "0123456"));
    hud_debug_get(full, sizeof full);
    assert(!strcmp(full, longer));
    /* back to none */
    assert(hud_debug_set(""));
    hud_debug_get(full, sizeof full);
    assert(full[0] == '\0');
    puts("hud passed");
    return 0;
}
