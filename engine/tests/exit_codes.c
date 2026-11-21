/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The exit-code table of the CLI and the runtime start (docs/ui/engine-ui-plan.md, "Unit 3: CLI", "Exit codes").
   The numbers are written here from that table, not read from the header */
#include "common/utilities/exit_codes.h"
#include <stdio.h>

static int check(int got, int want, const char* name) {
    if (got == want) return 0;
    fprintf(stderr, "%s = %d, the table says %d\n", name, got, want);
    return 1;
}

int main(void) {
    int bad = 0;
    bad |= check(SNX_EXIT_OK, 0, "SNX_EXIT_OK");
    bad |= check(SNX_EXIT_INTERNAL, 1, "SNX_EXIT_INTERNAL");
    bad |= check(SNX_EXIT_USAGE, 2, "SNX_EXIT_USAGE");
    bad |= check(SNX_EXIT_GAME_FILES, 3, "SNX_EXIT_GAME_FILES");
    bad |= check(SNX_EXIT_RECOMPILE, 4, "SNX_EXIT_RECOMPILE");
    bad |= check(SNX_EXIT_START, 5, "SNX_EXIT_START");
    bad |= check(SNX_EXIT_CRASH, 6, "SNX_EXIT_CRASH");
    bad |= check(SNX_EXIT_SETTING, 7, "SNX_EXIT_SETTING");
    bad |= check(SNX_EXIT_GAME_CODE, 8, "SNX_EXIT_GAME_CODE");
    bad |= check(SNX_EXIT_INTERRUPT, 130, "SNX_EXIT_INTERRUPT");
    return bad ? 1 : 0;
}
