/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The screen (present.c): the window, the framebuffer sceDisplaySetFrameBuf names shown at each vblank, and the run's
   screenshots */
#ifndef SNX_VITA_PRESENT_H
#define SNX_VITA_PRESENT_H
#include "vita/runtime/process.h"

/* the window titled title (none headless or without a display), its input and the device; shot: where the run's last
   frame goes (a PPM), NULL for none */
void vita_present_init(const char* title, const char* shot);
/* a vblank: the framebuffer shown, the window's events read. False once the player closed the window */
bool vita_present_vblank(VitaProcess* p);
/* the screenshot of the framebuffer shown last, at the end of the run */
void vita_present_shot(VitaProcess* p);
void vita_present_free(void);
#endif
