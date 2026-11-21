/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The process's exit codes, one table for the CLI and the runtime (docs/ui/engine-ui-plan.md, unit 3) */
#ifndef SNX_EXIT_CODES_H
#define SNX_EXIT_CODES_H

#define SNX_EXIT_OK 0          /* success, a normal game end or a window close */
#define SNX_EXIT_INTERNAL 1    /* an internal error: a bug or out of memory */
#define SNX_EXIT_USAGE 2       /* wrong usage: an unknown option or command, a bad argument count */
#define SNX_EXIT_GAME_FILES 3  /* game files missing or wrong: the input, exefs/main, the NPDM or the RomFS */
#define SNX_EXIT_RECOMPILE 4   /* the recompile failed: compile, link, staging or build */
#define SNX_EXIT_START 5       /* the start failed before the window */
#define SNX_EXIT_CRASH 6       /* a crash of the game: fatal, a crash report or an unhandled exception */
#define SNX_EXIT_SETTING 7     /* a setting value was refused: settings set and settings check */
#define SNX_EXIT_GAME_CODE 8   /* the game ended with a nonzero code */
#define SNX_EXIT_INTERRUPT 130 /* stopped by the user (Ctrl+C) */

#endif
