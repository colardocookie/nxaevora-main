/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* snx_old_data_dir: where an older version kept the data folder, the portable .scarletnx beside the program or the
   old user's folder. It reports which one is there and does not move or change it. */
#define _POSIX_C_SOURCE 200809L /* setenv */
#include "common/utilities/common.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* join(const char* a, const char* b) {
    char* p = snx_path_join(a, b);
    assert(p);
    return p;
}

static char* save_env(const char* name) {
    const char* value = getenv(name);
    return value ? snx_strdup(value) : NULL;
}

static void set_env(const char* name, const char* value) {
#ifdef _WIN32
    assert(!_putenv_s(name, value));
#else
    assert(!setenv(name, value, 1));
#endif
}

static void clear_env(const char* name) {
#ifdef _WIN32
    assert(!_putenv_s(name, ""));
#else
    assert(!unsetenv(name));
#endif
}

static void restore_env(const char* name, const char* value) {
    if (value)
        set_env(name, value);
    else
        clear_env(name);
}

int main(void) {
    char* root = snx_temp_dir(NULL);
    char *program, *appdata, *xdg, *home;
    char *portable, *user_old;
    char *was_appdata = save_env("APPDATA"), *was_xdg = save_env("XDG_DATA_HOME"), *was_home = save_env("HOME");
    char *was_appimage = save_env("APPIMAGE"), *was_appdir = save_env("APPDIR");
    assert(root);
    program = join(root, "program");
    portable = join(program, ".scarletnx");
    appdata = join(root, "appdata");
    xdg = join(root, "xdg");
    home = join(root, "home");
    assert(!snx_mkdirs(program) && !snx_mkdirs(appdata) && !snx_mkdirs(xdg) && !snx_mkdirs(home));
    set_env("APPDATA", appdata);
    set_env("XDG_DATA_HOME", xdg);
    set_env("HOME", home);
    clear_env("APPIMAGE");
    clear_env("APPDIR");
#ifdef _WIN32
    user_old = join(appdata, "scarletnx");
#elif defined(SNX_XDG)
    user_old = join(xdg, "scarletnx");
#endif

    /* no old folder beside the program and none in the old user's data folder */
    assert(snx_old_data_dir(program) == SNX_OLD_DATA_NONE);
    /* the old user's data folder is found */
    assert(!snx_mkdirs(user_old));
    assert(snx_old_data_dir(program) == SNX_OLD_DATA_USER);
    /* a portable .scarletnx beside the program wins */
    assert(!snx_mkdirs(portable));
    assert(snx_old_data_dir(program) == SNX_OLD_DATA_PORTABLE);
    /* no portable folder: the old user's data folder again */
    assert(!snx_remove_tree(portable));
    assert(snx_old_data_dir(program) == SNX_OLD_DATA_USER);

    assert(!snx_remove_tree(root));
    restore_env("APPDATA", was_appdata);
    restore_env("XDG_DATA_HOME", was_xdg);
    restore_env("HOME", was_home);
    restore_env("APPIMAGE", was_appimage);
    restore_env("APPDIR", was_appdir);
    free(was_appdata);
    free(was_xdg);
    free(was_home);
    free(was_appimage);
    free(was_appdir);
    free(user_old);
    free(portable);
    free(home);
    free(xdg);
    free(appdata);
    free(program);
    free(root);
    puts("snx_old_data_dir: the portable .scarletnx beside the program, else the old user's data folder, not moved");
    return 0;
}
