/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The logs export and cache clean commands (maintenance_cli.h): one zip of a game's diagnostic files, and the deletion
   of the caches it makes again. No window, no game start and no progress line (docs/records/ui/engine-ui-plan.md,
   unit 3) */
#include "nx/runtime/maintenance_cli.h"

#include "common/utilities/common.h"
#include "common/utilities/exit_codes.h"
#include "common/utilities/logzip.h"
#include "nx/runtime/command.h"
#include "nx/runtime/main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAINTENANCE_CLI_PATH   1400 /* a game's file path, as settings_cli.c's */
#define MAINTENANCE_CLI_BASE   256  /* the base game's name of an update */
#define MAINTENANCE_CLI_STAMP  32   /* YYYY-MM-DD_HH-MM-SS */
#define MAINTENANCE_CLI_ERROR  1024 /* the zip writer's reason */
#define MAINTENANCE_CLI_GROWTH 8    /* the zip list's first slots */

/* The game's settings file, <config>/<name>.ini, for the program path of --game: main.c's naming of the game's path and
   folders, then the .ini that snx_main opens. 0, -1 for a path that names no game (the folders are not made), or the
   add-on kind (SNX_ADDON_UPDATE or SNX_ADDON_DLC) for an update's or a DLC's program, whose run opens the base game's
   .ini (addon_init), the base game's name then in base. The settings commands share it */
int snx_cli_game_file(const char* program, char* out, size_t n, char* base, size_t base_size) {
    int kind;
    snx_game_locate(program);
    if (!g_exename[0]) return -1;
    kind = snx_game_addon(g_exedir, g_exename, base, base_size, NULL);
    if (kind != SNX_ADDON_NONE) return kind;
    snx_game_folders();
    snprintf(out, n, "%s%s.ini", g_confdir, g_exename);
    return 0;
}

/* the zip's files: their paths on disk and their names in it, both owned */
typedef struct {
    GuiZipFile* files;
    size_t count, cap;
} ZipList;

static void zip_list_add(ZipList* list, char* path, char* name) {
    if (list->count == list->cap) {
        list->cap = list->cap ? list->cap * 2 : MAINTENANCE_CLI_GROWTH;
        list->files = (GuiZipFile*)snx_realloc(list->files, list->cap * sizeof *list->files);
    }
    list->files[list->count].path = path;
    list->files[list->count].name = name;
    list->count++;
}

static void zip_list_free(ZipList* list) {
    size_t i;
    for (i = 0; i < list->count; ++i) {
        free((char*)list->files[i].path);
        free((char*)list->files[i].name);
    }
    free(list->files);
    memset(list, 0, sizeof *list);
}

/* The log, the kept older runs in logs/, threads.txt, the .ini and the early error file, each with its name in the zip.
   A missing file stays in the list: the zip writer leaves it out and names it in summary.txt */
static void collect_logs(ZipList* list) {
    char* dir = snx_format("%slogs", g_statedir);
    char** names = NULL;
    size_t count = 0, i;
    zip_list_add(list, snx_format("%s%s.log", g_statedir, g_exename), snx_format("%s.log", g_exename));
    if (snx_is_dir(dir) && !snx_list_dir(dir, &names, &count)) {
        for (i = 0; i < count; ++i) {
            char* path = snx_path_join(dir, names[i]);
            if (snx_is_file(path))
                zip_list_add(list, path, snx_format("logs/%s", names[i]));
            else
                free(path);
        }
        snx_free_strings(names, count);
    }
    free(dir);
    zip_list_add(list, snx_format("%sthreads.txt", g_statedir), snx_strdup("threads.txt"));
    zip_list_add(list, snx_format("%s%s.ini", g_confdir, g_exename), snx_format("%s.ini", g_exename));
    {
        char* early = snx_early_error_path(0);
        if (early) zip_list_add(list, early, snx_strdup(SNX_ERROR_LOG));
    }
}

/* the local time as the kept logs name it: YYYY-MM-DD_HH-MM-SS. 0 when the system gives no time */
static int local_stamp(char* out, size_t size) {
    time_t t = time(NULL);
    struct tm tm;
    int ok;
#if defined(_WIN32)
    ok = localtime_s(&tm, &t) == 0;
#else
    ok = localtime_r(&t, &tm) != NULL;
#endif
    return ok && strftime(out, size, "%Y-%m-%d_%H-%M-%S", &tm) != 0;
}

/* the zip's path: --out, else <state>/<name>-logs-<date>_<time>.zip; NULL when the time cannot be read */
static char* output_path(const char* out) {
    char stamp[MAINTENANCE_CLI_STAMP];
    if (out) return snx_strdup(out);
    if (!local_stamp(stamp, sizeof stamp)) return NULL;
    return snx_format("%s%s-logs-%s.zip", g_statedir, g_exename, stamp);
}

int snx_logs_export(int argc, char** argv) {
    char path[MAINTENANCE_CLI_PATH], base[MAINTENANCE_CLI_BASE], error[MAINTENANCE_CLI_ERROR];
    const char *game = NULL, *out = NULL;
    ZipList list;
    char* made;
    size_t added = 0;
    int i, status;
    for (i = 3; i < argc; ++i) {
        if (!strncmp(argv[i], "--game=", 7)) {
            game = argv[i] + 7;
        } else if (!strncmp(argv[i], "--out=", 6)) {
            out = argv[i] + 6;
        } else if (!strcmp(argv[i], "--out")) {
            if (++i == argc) {
                snx_command_error("logs export: --out needs a file");
                return SNX_EXIT_USAGE;
            }
            out = argv[i];
        } else {
            snx_command_error("logs export: %s: unexpected argument", argv[i]);
            return SNX_EXIT_USAGE;
        }
    }
    if (!game) {
        snx_command_error("logs export: --game=PATH names the game's program");
        return SNX_EXIT_USAGE;
    }
    status = snx_cli_game_file(game, path, sizeof path, base, sizeof base);
    if (status > 0) {
        snx_command_error("logs export: %s is %s %s: name the game's program", game,
                          status == SNX_ADDON_UPDATE ? "an update of" : "a DLC of", base);
        return SNX_EXIT_USAGE;
    }
    if (status < 0) {
        snx_command_error("logs export: %s: not a game's program: check --game", game);
        return SNX_EXIT_GAME_FILES;
    }
    made = output_path(out);
    if (!made) {
        snx_command_error("logs export: the local time cannot be read");
        return SNX_EXIT_INTERNAL;
    }
    if (snx_path_exists(made)) {
        snx_command_error("logs export: %s: the file is there already: name another --out", made);
        free(made);
        return SNX_EXIT_USAGE;
    }
    memset(&list, 0, sizeof list);
    collect_logs(&list);
    status = snx_logs_zip(made, "", list.files, list.count, &added, error, sizeof error);
    zip_list_free(&list);
    if (status) {
        snx_command_error("logs export: %s", error);
        free(made);
        return SNX_EXIT_INTERNAL;
    }
    {
        char* line = snx_format("Wrote %s (%lu files)", made, (unsigned long)added);
        snx_command_print(line);
        free(line);
    }
    free(made);
    return SNX_EXIT_OK;
}

/* the path deleted, one line: deleted or not present; a failure names the reason and gives 1 */
static int remove_cache(char* path, int shared) {
    char why[256];
    char* line;
    int failed;
    if (!snx_path_exists(path)) {
        line = shared ? snx_format("not present %s (shared by all games of the data folder)", path)
                      : snx_format("not present %s", path);
        snx_command_print(line);
        free(line);
        free(path);
        return 0;
    }
    failed = snx_remove_tree(path);
    if (failed) {
        snx_os_error(why, sizeof why);
        snx_command_error("cache clean: %s: %s", path, why);
    } else {
        line = shared ? snx_format("deleted %s (shared by all games of the data folder)", path)
                      : snx_format("deleted %s", path);
        snx_command_print(line);
        free(line);
    }
    free(path);
    return failed;
}

/* the unit cache: <menu cache>/units, the folder every game of the game's data folder shares (recompile.c) */
static char* units_path(void) {
    char* folder = snx_menu_dir(g_exedir, SNX_USER_CACHE);
    char* units = folder ? snx_path_join(folder, "units") : NULL;
    free(folder);
    return units;
}

int snx_cache_clean(int argc, char** argv) {
    char path[MAINTENANCE_CLI_PATH], base[MAINTENANCE_CLI_BASE];
    const char* game = NULL;
    int shaders = 0, code = 0, units = 0, both, i, status, failed = 0;
    for (i = 3; i < argc; ++i) {
        if (!strncmp(argv[i], "--game=", 7)) {
            game = argv[i] + 7;
        } else if (!strcmp(argv[i], "--shaders")) {
            shaders = 1;
        } else if (!strcmp(argv[i], "--code")) {
            code = 1;
        } else if (!strcmp(argv[i], "--units")) {
            units = 1;
        } else {
            snx_command_error("cache clean: %s: unexpected argument", argv[i]);
            return SNX_EXIT_USAGE;
        }
    }
    if (!game) {
        snx_command_error("cache clean: --game=PATH names the game's program");
        return SNX_EXIT_USAGE;
    }
    status = snx_cli_game_file(game, path, sizeof path, base, sizeof base);
    if (status > 0) {
        snx_command_error("cache clean: %s is %s %s: name the game's program", game,
                          status == SNX_ADDON_UPDATE ? "an update of" : "a DLC of", base);
        return SNX_EXIT_USAGE;
    }
    if (status < 0) {
        snx_command_error("cache clean: %s: not a game's program: check --game", game);
        return SNX_EXIT_GAME_FILES;
    }
    both = !shaders && !code && !units;
    if (shaders || both) failed |= remove_cache(snx_format("%sshader_cache", g_cachedir), 0);
    if (code || both) failed |= remove_cache(snx_format("%s%s.jitcache", g_cachedir, g_exename), 0);
    if (units) {
        char* shared = units_path();
        if (!shared) {
            snx_command_error("cache clean: units: no cache folder (HOME or APPDATA is not set)");
            failed = 1;
        } else {
            failed |= remove_cache(shared, 1);
        }
    }
    return failed ? SNX_EXIT_INTERNAL : SNX_EXIT_OK;
}
