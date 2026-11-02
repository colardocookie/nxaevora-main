/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* A game's cheats by their names, as the launcher lists them and the runtime runs them (nx/runtime/cheats.c): the
   Atmosphere-format files of its data folder's cheats/, <build id>.txt each, a cheat a [name] (or a {name}, the
   master code) and its words under it. Every cheat runs unless the game's cheats_off= names it */
#ifndef SNX_CHEAT_LIST_H
#define SNX_CHEAT_LIST_H
#include "common/utilities/common.h"

/* cheats_off=: the cheats not run, their names with a | between two; a name with a | in it cannot be one of them */
#define SNX_CHEATS_SEPARATOR '|'
/* the cheats folder of the game whose data folder is data, owned */
char* snx_cheats_folder(const char* data);
/* the names of the cheats in the .txt files of folder (snx_cheats_folder), the files by their names, each name once,
   as a file has them, into *names (snx_free_strings): their count, 0 for none or no folder */
size_t snx_cheats_list(const char* folder, char*** names);
/* the name of the cheat a line of a cheats file starts ([name] or {name}, spaces before it), into name: 1, else 0 */
int snx_cheat_name(const char* line, char* name, size_t size);
/* 1 when off (cheats_off=, NULL for none) names the cheat name */
int snx_cheat_off(const char* off, const char* name);
#endif
