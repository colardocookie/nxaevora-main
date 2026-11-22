/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The program's entry: the MCP server (--mcp) and the host driver's recompile first, and anything else is a game to
   run */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/utilities/exit_codes.h"
#include "common/utilities/progress.h"
#include "common/utilities/setup.h"
#include "nx/runtime/command.h"
#include "nx/runtime/driver/driver.h"
#include "nx/runtime/driver/mcp.h"
#include "nx/runtime/entry.h"
#include "nx/runtime/main.h"
#include "nx/runtime/options.h" /* g_ini_sections, ini_list_text */
#ifdef SNX_WITH_GUI
#include "gui/gui.h"
#include "gui/theme.h"
#endif
#ifdef SNX_WITH_GUI
/* the game's settings as scarletnx --settings prints them, for the menu built into this program (snx_menu_settings) */
static char* runtime_settings(void) {
    size_t n = ini_list_text(g_ini_sections, g_ini_sections_count, NULL, 0);
    char* text = (char*)malloc(n + 1);
    if (text) ini_list_text(g_ini_sections, g_ini_sections_count, text, n + 1);
    return text;
}
#endif
/* Opens the stream of an internal child stage before the driver reads any option: the driver gives it to its children
   and they write to the parent's private stream */
static int open_stage_stream(int argc, char** argv) {
    int i;
    for (i = 1; i < argc && strcmp(argv[i], "--"); ++i)
        if (snx_progress_parse(argv[i]) == SNX_PROGRESS_JSONL_STAGE)
            return snx_progress_open(SNX_PROGRESS_JSONL_STAGE);
    return 0;
}
int snx_switch_main(int argc, char** argv) {
    int result;
    char why[160];
    /* a launcher reads our progress through pipes: lines as they end, errors at once */
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
    setvbuf(stderr, NULL, _IONBF, 0);
    /* the setup's own options, for the driver and the menu, before anything reads the rest */
    argc = snx_setup_options(argc, argv, why, sizeof why);
    if (argc < 0) {
        fprintf(stderr, "nxaevora-engine: %s\n", why);
        return SNX_EXIT_USAGE;
    }
    result = snx_command_translate(argc, argv, &argc, &argv);
    if (result >= 0) {
        snx_progress_finish(result);
        return result;
    }
    if (open_stage_stream(argc, argv)) return SNX_EXIT_INTERNAL;
#ifdef SNX_WITH_GUI
    snx_menu = snx_gui_main;
    snx_menu_run_theme = gui_run_theme;
    snx_menu_settings = runtime_settings;
#endif
    if (argc > 1 && !strcmp(argv[1], "--mcp")) return snx_mcp(argc, argv);
#if defined(SNX_WITH_GUI)
    if (argc > 1 && !strncmp(argv[1], "--migrate", 9) && (!argv[1][9] || argv[1][9] == '=')) {
        if (argc > 2) {
            fprintf(stderr, "scarletnx: %s takes nothing after it: %s\n", argv[1], argv[2]);
            return SNX_EXIT_USAGE;
        }
        return snx_gui_migrate(argv[0], argv[1]);
    }
#endif
    result = snx_driver(argc, argv);
    if (result >= 0) {
        snx_progress_finish(result);
        return result;
    }
    return snx_main(argc, argv);
}
