/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "common/utilities/logzip.h"
#include "common/utilities/common.h"
#include "common/utilities/logscan.h"
#include <miniz.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* text, with its homes as ~, into the zip under name, last changed at when: 0, or the miniz error */
static mz_zip_error add(mz_zip_archive* zip, const char* name, const char* text, size_t n, time_t when) {
    size_t shown;
    char* tilde = snx_logscan_tilde(text, n, &shown);
    mz_bool done;
    if (!tilde) return MZ_ZIP_ALLOC_FAILED;
    done =
        mz_zip_writer_add_mem_ex_v2(zip, name, tilde, shown, NULL, 0, MZ_DEFAULT_LEVEL, 0, 0, &when, NULL, 0, NULL, 0);
    free(tilde);
    return done ? MZ_ZIP_NO_ERROR : mz_zip_get_last_error(zip);
}

/* the zip at path, made in memory then written whole: with new_file the file is made exclusively, so it must not be
   there yet. 0; 1 when new_file and path is there already; -1 otherwise, error with the reason */
static int logs_zip(const char* path, const char* summary, const GuiZipFile* files, size_t count, size_t* added,
                    int new_file, char* error, size_t size) {
    mz_zip_archive zip;
    mz_zip_error failed = MZ_ZIP_NO_ERROR;
    void* bytes = NULL;
    size_t length = 0, i, left = 0, made = 0;
    int written = 0;
    char* text;
    if (added) *added = 0;
    if (!error || !size) return -1;
    error[0] = 0;
    text = snx_strdup(summary);
    mz_zip_zero_struct(&zip);
    if (!mz_zip_writer_init_heap(&zip, 0, (size_t)1 << 20)) {
        snprintf(error, size, "%s could not be made: %s", path, mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        free(text);
        return -1;
    }
    for (i = 0; i < count && !failed; ++i) {
        size_t n = 0;
        unsigned char* data = snx_read_file(files[i].path, &n);
        char why[256], *more;
        if (data) { /* miniz's own failure ends the zip: it says nothing of what it leaves behind */
            failed = add(&zip, files[i].name, (const char*)data, n, (time_t)snx_file_time(files[i].path));
            if (failed)
                snprintf(error, size, "%s could not be made: %s: %s", path, files[i].path,
                         mz_zip_get_error_string(failed));
            else
                ++made;
            free(data);
            continue;
        }
        more = snx_format("%s%sNot in the zip: %s (%s)\n", text, left++ ? "" : "\n", files[i].path,
                          snx_os_error(why, sizeof why));
        free(text);
        text = more;
    }
    if (!failed && (failed = add(&zip, "summary.txt", text, strlen(text), time(NULL))) != MZ_ZIP_NO_ERROR)
        snprintf(error, size, "%s could not be made: summary.txt: %s", path, mz_zip_get_error_string(failed));
    if (!failed && !mz_zip_writer_finalize_heap_archive(&zip, &bytes, &length)) {
        failed = mz_zip_get_last_error(&zip);
        snprintf(error, size, "%s could not be made: %s", path, mz_zip_get_error_string(failed));
    }
    if (!failed) {
        if (new_file)
            written = snx_write_new_file(path, bytes, length, error, size);
        else if (snx_write_file(path, bytes, length)) {
            char why[256];
            snprintf(error, size, "%s could not be written: %s", path, snx_os_error(why, sizeof why));
            written = -1;
        }
    }
    mz_zip_writer_end(&zip);
    mz_free(bytes);
    free(text);
    if (added) *added = made;
    if (failed) return -1;
    return new_file ? written : error[0] != 0;
}

int snx_logs_zip(const char* path, const char* summary, const GuiZipFile* files, size_t count, size_t* added,
                 char* error, size_t size) {
    return logs_zip(path, summary, files, count, added, 0, error, size);
}

int snx_logs_zip_new(const char* path, const char* summary, const GuiZipFile* files, size_t count, size_t* added,
                     char* error, size_t size) {
    return logs_zip(path, summary, files, count, added, 1, error, size);
}
