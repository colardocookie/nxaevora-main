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
static int open_stage_stream(int argc, char** argv) {
    int i;
    for (i = 1; i < argc && strcmp(argv[i], "--"); ++i)
        if (snx_progress_parse(argv[i]) == SNX_PROGRESS_JSONL_STAGE)
            return snx_progress_open(SNX_PROGRESS_JSONL_STAGE);
    return 0;
}
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
    if (open_stage_stream(argc, argv)) return SNX_EXIT_INTERNAL;
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
