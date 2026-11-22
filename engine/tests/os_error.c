/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* snx_os_error: the system's reason for the file or folder call that just failed, on Windows too, whose calls set
   GetLastError and not errno; and snx_path_missing: a path that is not there, at a name or a folder above it */
#include "common/utilities/common.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LONG_NAME_LEN 300

int main(void) {
    char *root = snx_temp_dir(NULL), *file, *under, *missing, *gone, *deep, *long_path, why[256];
#ifndef _WIN32
    char long_name[LONG_NAME_LEN + 1];
#endif
    FILE* f;
    assert(root);
    file = snx_path_join(root, "a file");
    under = snx_path_join(file, "a folder");
    missing = snx_path_join(root, "missing");
    gone = snx_path_join(root, "gone");
    deep = snx_path_join(gone, "a file");
    assert(file && under && missing && gone && deep && !snx_write_file(file, "x", 1));
    /* a name that is not there, or one below a folder that is not there: missing; an existing file or folder is not */
    assert(snx_path_missing(missing) && snx_path_missing(deep));
    assert(!snx_path_missing(file) && !snx_path_missing(root));
    /* An invalid name on Windows, or an overlong component on POSIX, fails for a reason other than missing. */
#ifdef _WIN32
    long_path = snx_path_join(root, "bad<name");
#else
    memset(long_name, 'a', LONG_NAME_LEN);
    long_name[LONG_NAME_LEN] = 0;
    long_path = snx_path_join(root, long_name);
#endif
    assert(long_path && !snx_path_missing(long_path));
    /* a folder under a file: the file is where its parent goes, which Windows' CreateDirectoryW says without errno */
    errno = 0;
    assert(snx_mkdirs(under));
    snx_os_error(why, sizeof why);
#ifdef _WIN32
    assert(why[0] && !strstr(why, "successfully"));
#else
    assert(!strcmp(why, strerror(EEXIST)));
#endif
    printf("a folder under a file: %s\n", why);
    /* a file that is not there */
    errno = 0;
    f = snx_fopen(missing, "rb");
    assert(!f);
    snx_os_error(why, sizeof why);
#ifdef _WIN32
    assert(why[0] && !strstr(why, "successfully"));
#else
    assert(!strcmp(why, strerror(ENOENT)));
#endif
    printf("a file not there: %s\n", why);
    /* a buffer too small is cut, never overrun */
    snx_os_error(why, 4);
    assert(strlen(why) <= 3);
    assert(!snx_remove_tree(root));
    free(long_path);
    free(deep);
    free(gone);
    free(missing);
    free(under);
    free(file);
    free(root);
    puts("PASS snx_os_error says the system's reason, and snx_path_missing sees what is not there");
    return 0;
}
