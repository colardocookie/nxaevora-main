/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_VITA_MAIN_H
#define SNX_VITA_MAIN_H
#include <stdbool.h>
/* scarletnx-vita's command line (main.c) */
int snx_vita_main(int argc, char** argv);
/* whether path's last part is a Vita title ID (a game's copy of the runtime is named after its title, PCSE00001) */
bool snx_vita_title_name(const char* path);
#endif
