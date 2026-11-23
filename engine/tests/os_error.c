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
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#define LONG_NAME_LEN 300

#ifdef _WIN32
/* path as UTF-16, owned: NULL when it cannot be converted */
static wchar_t* wide_path(const char* path) {
    int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    wchar_t* wide = n > 0 ? (wchar_t*)malloc((size_t)n * sizeof *wide) : NULL;
    if (wide && MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, n) != n) {
        free(wide);
        wide = NULL;
    }
    return wide;
}
#endif

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
    /* a child that cannot be deleted: snx_remove_tree keeps the child's error, not the folder's own that follows */
#ifdef _WIN32
    {
        char* folder = snx_path_join(root, "held");
        char* child = snx_path_join(folder, "child");
        wchar_t* wchild = NULL;
        HANDLE held;
        DWORD code;
        int result;
        assert(folder && child && !snx_mkdirs(folder) && !snx_write_file(child, "x", 1));
        wchild = wide_path(child);
        assert(wchild);
        held = CreateFileW(wchild, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
        free(wchild);
        assert(held != INVALID_HANDLE_VALUE);
        result = snx_remove_tree(folder);
        code = GetLastError();
        assert(result == 1);
        assert(code == ERROR_SHARING_VIOLATION); /* not ERROR_DIR_NOT_EMPTY from the folder's own RemoveDirectoryW */
        assert(CloseHandle(held));
        assert(!snx_remove_tree(folder)); /* the handle is closed: the child and the folder go */
        free(child);
        free(folder);
    }
#else
    {
        char* folder = snx_path_join(root, "held");
        char* child = snx_path_join(folder, "child");
        int result, code;
        assert(folder && child && !snx_mkdirs(folder) && !snx_write_file(child, "x", 1));
        assert(!chmod(folder, 0555));
        result = snx_remove_tree(folder);
        code = errno;
        assert(result == 1);
        assert(code == EACCES); /* the child's unlink, not the folder's own rmdir */
        assert(!chmod(folder, 0755));
        assert(!snx_remove_tree(folder)); /* the folder is writable again: the child and the folder go */
        free(child);
        free(folder);
    }
#endif
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
