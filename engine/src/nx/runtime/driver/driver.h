/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_DRIVER_H
#define SNX_DRIVER_H
#include "nx/loader/loader.h"
/* -1 leaves the arguments to the runtime; other results are exit codes. */
int snx_driver(int argc, char** argv);
/* the exit code of a failed --dump-modules: SNX_EXIT_GAME_FILES for the program's files, SNX_EXIT_INTERNAL for a
   failed write or a failure of the host */
int snx_dump_exit_code(SnxDumpResult result);
/* 1 when key is a setting of the game's .ini (runtime/options.c); weak, as a build of the driver alone has no runtime
   and so no setting */
int snx_setting_known(const char* key) __attribute__((weak));
#endif
