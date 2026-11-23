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
/* The game's settings file, <config>/<name>.ini, for the program path: snx_game_locate, then the folders from the
   game's record or the environment, without making one. 0, -1 for a path that names no game, or the add-on kind
   (SNX_ADDON_UPDATE or SNX_ADDON_DLC) for an update's or a DLC's program, the base game's name then in base */
int snx_cli_game_file(const char* program, char* out, size_t n, char* base, size_t base_size);
/* The checks that logs export, cache clean, settings get and settings set share: --game exists, is not an add-on's
   program and names a game's program. 0 for a game, with out and base filled as snx_cli_game_file; else the exit code
   (SNX_EXIT_GAME_FILES or SNX_EXIT_USAGE), with the command's name in the message */
int snx_cli_game_require(const char* command, const char* game, char* out, size_t n, char* base, size_t base_size);
#endif
