/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_MAINTENANCE_CLI_H
#define SNX_MAINTENANCE_CLI_H
#include <stddef.h>
/* The logs export and cache clean commands of command.c's dispatch (maintenance_cli.c): one zip of a game's
   diagnostic files, and the deletion of its shader, code and recompile unit caches. They need no window and no game
   start. argv[1] is "logs" or "cache" and argv[2] the command's word; each returns an exit code of exit_codes.h */
int snx_logs_export(int argc, char** argv);
int snx_cache_clean(int argc, char** argv);
/* The game's settings file, <config>/<name>.ini, for the program path: snx_game_locate, then snx_game_folders. 0, -1
   for a path that names no game (the folders are not made), or the add-on kind (SNX_ADDON_UPDATE or SNX_ADDON_DLC)
   for an update's or a DLC's program, the base game's name then in base. settings_cli.c's get and set share it */
int snx_cli_game_file(const char* program, char* out, size_t n, char* base, size_t base_size);
#endif
