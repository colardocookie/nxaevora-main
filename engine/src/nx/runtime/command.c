/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The subcommand dispatch: argv[1] recompile, run or settings list, translated to the old option form. The old flags
   stay as aliases (docs/ui/engine-ui-plan.md, unit 3) */
#include "nx/runtime/command.h"

#include "common/utilities/common.h"
#include "common/utilities/exit_codes.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SNX_COMMAND_PREFIX "nxaevora-engine: "

/* a branch that ends the program prints after the console is attached: on Windows this is a GUI program, and its
   output reaches a terminal only then (snx_tool_console). The paths that return -1 print nothing here */
static void say_error(const char* fmt, ...) {
    va_list ap;
    snx_tool_console();
    fputs(SNX_COMMAND_PREFIX, stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void say_help(const char* text) {
    snx_tool_console();
    puts(text);
}

/* the old form's argv: one allocation the caller keeps until exit, never freed. NULL on failure */
static char** new_argv(int slots) {
    return (char**)malloc((size_t)slots * sizeof(char*));
}

static void settings_usage(void) {
    say_help("Usage: nxaevora-engine settings list [--scope=game|recompile]\n"
             "  --scope=game           The game's settings (the default)\n"
             "  --scope=recompile      The launcher's and the recompiler's settings\n"
             "settings schema|get|set|check, logs and cache come later");
}

static int translate_recompile(int argc, char** argv, int* out_argc, char*** out_argv) {
    char** out = new_argv(argc + 1);
    int i;
    if (!out) {
        say_error("out of memory");
        return SNX_EXIT_INTERNAL;
    }
    out[0] = argv[0];
    out[1] = "-r";
    for (i = 2; i < argc; ++i) out[i] = argv[i];
    out[argc] = NULL;
    *out_argc = argc;
    *out_argv = out;
    return -1;
}

static int translate_run(int argc, char** argv, int* out_argc, char*** out_argv) {
    char** out;
    int i, n = 0;
    if (argc < 3) {
        say_error("run: name a game program");
        return SNX_EXIT_USAGE;
    }
    if (!strcmp(argv[2], "--help")) {
        say_help("Usage: nxaevora-engine run PROGRAM [--KEY=VALUE] [-- GAME_ARGUMENTS...]\n"
                 "  PROGRAM                A game made already, or an NRO");
        return SNX_EXIT_OK;
    }
    if (argv[2][0] == '-') {
        say_error("run: %s is an option, not a program", argv[2]);
        return SNX_EXIT_USAGE;
    }
    out = new_argv(argc);
    if (!out) {
        say_error("out of memory");
        return SNX_EXIT_INTERNAL;
    }
    out[n++] = argv[0];
    for (i = 2; i < argc; ++i) out[n++] = argv[i];
    out[n] = NULL;
    *out_argc = n;
    *out_argv = out;
    return -1;
}

static int translate_settings(int argc, char** argv, int* out_argc, char*** out_argv) {
    char** out;
    const char* job = argc > 2 ? argv[2] : NULL;
    if (!job) {
        say_error("settings: name a job: list");
        return SNX_EXIT_USAGE;
    }
    if (!strcmp(job, "--help") && argc == 3) {
        settings_usage();
        return SNX_EXIT_OK;
    }
    if (strcmp(job, "list")) {
        say_error("settings: %s: not available in this build", job);
        return SNX_EXIT_USAGE;
    }
    if (argc == 4 && !strcmp(argv[3], "--help")) {
        settings_usage();
        return SNX_EXIT_OK;
    }
    if (argc > 4) {
        say_error("settings list: %s: unexpected argument", argv[4]);
        return SNX_EXIT_USAGE;
    }
    if (argc == 4 && strncmp(argv[3], "--scope=", 8)) {
        say_error("settings list: %s: unexpected argument", argv[3]);
        return SNX_EXIT_USAGE;
    }
    if (argc == 4 && strcmp(argv[3] + 8, "game") && strcmp(argv[3] + 8, "recompile")) {
        say_error("settings list: --scope %s: not game or recompile", argv[3] + 8);
        return SNX_EXIT_USAGE;
    }
    out = new_argv(3);
    if (!out) {
        say_error("out of memory");
        return SNX_EXIT_INTERNAL;
    }
    out[0] = argv[0];
    out[1] = argc == 4 && !strcmp(argv[3] + 8, "recompile") ? "--settings=recompile" : "--settings";
    out[2] = NULL;
    *out_argc = 2;
    *out_argv = out;
    return -1;
}

int snx_command_translate(int argc, char** argv, int* out_argc, char*** out_argv) {
    if (!out_argc || !out_argv) return SNX_EXIT_INTERNAL;
    *out_argc = argc;
    *out_argv = argv;
    if (argc < 2 || !argv) return -1;
    if (!strcmp(argv[1], "recompile")) return translate_recompile(argc, argv, out_argc, out_argv);
    if (!strcmp(argv[1], "run")) return translate_run(argc, argv, out_argc, out_argv);
    if (!strcmp(argv[1], "settings")) return translate_settings(argc, argv, out_argc, out_argv);
    if (!strcmp(argv[1], "logs") || !strcmp(argv[1], "cache")) {
        say_error("%s: command not available in this build", argv[1]);
        return SNX_EXIT_USAGE;
    }
    return -1;
}
