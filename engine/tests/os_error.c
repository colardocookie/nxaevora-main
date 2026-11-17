/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* snx_os_error: the system's reason for the file or folder call that just failed, on Windows too, whose calls set
   GetLastError and not errno */
#include "common/utilities/common.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char *root = snx_temp_dir(NULL), *file, *under, *missing, why[256];
    FILE* f;
    assert(root);
    file = snx_path_join(root, "a file");
    under = snx_path_join(file, "a folder");
    missing = snx_path_join(root, "missing");
    assert(file && under && missing && !snx_write_file(file, "x", 1));
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
    free(missing);
    free(under);
    free(file);
    free(root);
    puts("PASS snx_os_error says the system's reason");
    return 0;
}
