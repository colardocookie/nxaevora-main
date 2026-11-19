/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "gui/place.h"

GuiRect gui_rect(int x, int y, int w, int h) {
    GuiRect r;
    r.x = x;
    r.y = y;
    r.w = w > 0 ? w : 0;
    r.h = h > 0 ? h : 0;
    return r;
}
