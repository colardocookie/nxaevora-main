/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Startup-only switches, parsed before loading game code or showing any fatal error box. */
#include "nx/runtime/startup.h"
#include "common/utilities/exit_codes.h"
#include "common/utilities/progress.h"
#include "nx/runtime/cli.h"
#include <stdio.h>

int g_opt_headless, g_cli_fullscreen = -1;
int g_opt_gdb, g_opt_gdb_stop, g_opt_tracy_capture, g_opt_tracy_client;

int snx_startup_options(int argc, char** argv) {
    unsigned flags = 0;
    int i, result;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--")) break;
        if (!strncmp(argv[i], "--progress=", 11) || !strcmp(argv[i], "--progress")) {
            int mode = snx_progress_parse(argv[i]);
            if (mode == SNX_PROGRESS_JSONL) {
                /* only the driver writes the exit line: a game takes the internal stage mode from it */
                fprintf(stderr, "scarletnx: --progress=jsonl goes to nxaevora-engine recompile or run\n");
                return SNX_EXIT_USAGE;
            }
            if (mode < 0) {
                fprintf(stderr, "scarletnx: %s: not jsonl-stage\n", argv[i]);
                return SNX_EXIT_USAGE;
            }
            if (snx_progress_open(mode)) return SNX_EXIT_INTERNAL;
            continue;
        }
        result = snx_cli_flag(argv[i], &flags);
        /* -r and its long form are the driver's: a game's runtime refuses them like any other it doesn't take, as a
           game is never recompiled from its own copy */
        if (result < 0 || (flags & SNX_RECOMP) ||
            (!result && argv[i][0] == '-' && argv[i][1] != '-' && strcmp(argv[i], "-v"))) {
            fprintf(stderr, "scarletnx: unknown startup option: %s\n", argv[i]);
            return SNX_EXIT_USAGE;
        }
    }
    g_opt_headless = !!(flags & SNX_HEADLESS);
    g_cli_fullscreen = flags & SNX_FULLSCREEN ? 1 : flags & SNX_WINDOWED ? 0 : -1;
    g_opt_gdb = !!(flags & SNX_GDB);
    g_opt_gdb_stop = !!(flags & SNX_STOP);
    g_opt_tracy_capture = !!(flags & SNX_TRACY_CAPTURE);
    g_opt_tracy_client = !!(flags & SNX_TRACY_CLIENT);
#if !defined(SNX_TRACY)
    if (g_opt_tracy_capture || g_opt_tracy_client) {
        fputs("scarletnx: --tracy and --tracy-capture need a runtime built with SNX_TRACY\n", stderr);
        return SNX_EXIT_USAGE;
    }
#endif
    return SNX_EXIT_OK;
}
