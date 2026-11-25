/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "common/utilities/common.h"
#include "common/utilities/exit_codes.h"
#include "common/utilities/progress.h"
#include "common/utilities/setup.h"
#include "nx/runtime/command.h"
#include "nx/runtime/driver/driver.h"
#include "nx/runtime/driver/mcp.h"
#include "nx/runtime/entry.h"
#include <stdio.h>
#include <string.h>
/* The Switch driver of a build of the host tools (SNX_TOOLS_ONLY), in the program as entry.c's is in the runtime's
   (src/multicall). The full runtime uses the same driver */
int snx_switch_main(int argc, char** argv) {
    char why[160];
    int result;
    argc = snx_setup_options(argc, argv, why, sizeof why);
    if (argc < 0) {
        snx_tool_console();
        fprintf(stderr, "scarletnx: %s\n", why);
        return SNX_EXIT_USAGE;
    }
    result = snx_command_translate(argc, argv, &argc, &argv);
    if (result >= 0) {
        snx_progress_finish(result);
        return result;
    }
    if (snx_progress_open_stage(argc, argv)) return SNX_EXIT_INTERNAL;
    if (argc > 1 && !strcmp(argv[1], "--mcp")) return snx_mcp(argc, argv);
    result = snx_driver(argc, argv);
    if (result >= 0) {
        snx_progress_finish(result);
        return result;
    }
    snx_tool_console();
    fputs("Pass a game folder with -r, or an NRO, or -h. This build contains host tools only.\n", stderr);
    return SNX_EXIT_USAGE;
}
