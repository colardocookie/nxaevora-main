/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_COMMAND_H
#define SNX_COMMAND_H
/* The subcommand dispatch (command.c): argv[1] as recompile, run or settings list, translated to the old option form
   before the driver reads it (docs/ui/engine-ui-plan.md, unit 3). -1: the old form goes on, with *out_argc and
   *out_argv set (maybe argv); else an exit code of exit_codes.h and the command is finished */
int snx_command_translate(int argc, char** argv, int* out_argc, char*** out_argv);

#endif
