/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The rectangles of a screen's boxes, in pixels of the area it is drawn in, as main.c's place_screen lays them out
   from ui.txt's frames Screen and Wide: the head round the tabs, the title and the user, the tips, the list and the
   box beside it or under it, the status box, and the foot's keys. Nothing overlaps. */
#ifndef SNX_GUI_PLACE_H
#define SNX_GUI_PLACE_H

typedef struct {
    int x, y, w, h;
} GuiRect;
/* app.ini's side_box: the side box beside the list where both fit, none below 45 columns (auto); there too, under the
   list (on); never, the list the whole width (off) */
typedef enum { GUI_SIDE_AUTO, GUI_SIDE_ON, GUI_SIDE_OFF } GuiSide;
typedef struct {
    GuiRect head; /* the tabs and the title, the whole width */
    GuiRect tabs, title, badge, tips, list, side, status, keys;
    int stacked; /* the side box under the list */
} GuiPlace;
/* a box at x, y, w by h, of no size rather than a negative one */
GuiRect gui_rect(int x, int y, int w, int h);
#endif
