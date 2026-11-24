/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The .ini's store (src/nx/runtime/options.c): an in-game change written to the file and its flag cleared, a change
   that a full store cannot save, a write blocked by an incomplete parse, a reload that keeps the value made in the
   game over the file's, the store's limit on the keys a write puts in the file, a write that fails and stays dirty,
   and a change that is in the store before apply reads it */
#include "common/utilities/common.h"
#include "nx/runtime/options.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the store's limit, STORE_MAX in options.c */
#define TEST_STORE_MAX 256
/* the value's limit, VALUE_MAX in options.c */
#define TEST_VALUE_MAX 2048

extern char g_test_log[8192];
extern size_t g_test_log_len;
extern int g_test_warn_count;
extern char g_test_mem[64];
extern char g_test_new[64];
extern char g_test_backend[64];
extern char g_test_backend_store[64];
extern const IniOpt g_test_table[];
extern char g_test_def[64];
extern size_t g_test_backend_len;
extern char g_test_backend_last;

static void log_clear(void) {
    g_test_log[0] = 0;
    g_test_log_len = 0;
    g_test_warn_count = 0;
}
/* the file's text: mem_key=mem, then others k1..kN with vN */
static char* make_text(int others, const char* mem) {
    size_t cap = 32 + (size_t)others * 24;
    char* text = malloc(cap);
    size_t at;
    int i;
    assert(text);
    at = (size_t)snprintf(text, cap, "mem_key=%s\n", mem);
    for (i = 1; i <= others; i++) at += (size_t)snprintf(text + at, cap - at, "k%d=v%d\n", i, i);
    assert(at < cap);
    return text;
}
static void put(const char* path, const char* text) {
    assert(!snx_write_file(path, text, strlen(text)));
}
static char* read_all(const char* path, size_t* n) {
    char* text = (char*)snx_read_file(path, n);
    assert(text);
    return text;
}
static void assert_has(const char* path, const char* line) {
    size_t n;
    char* text = read_all(path, &n);
    if (!strstr(text, line)) {
        fprintf(stderr, "missing %s in %s:\n%s", line, path, text);
        free(text);
        assert(0);
    }
    free(text);
}
static void assert_same(const char* path, const char* before, size_t n) {
    size_t now;
    char* text = read_all(path, &now);
    assert(now == n && !memcmp(text, before, n));
    free(text);
}
static void assert_lacks(const char* path, const char* line) {
    size_t n;
    char* text = read_all(path, &n);
    if (strstr(text, line)) {
        fprintf(stderr, "unexpected %s in %s:\n%s", line, path, text);
        free(text);
        assert(0);
    }
    free(text);
}
/* the file's key rows: a line with a '=' that is not a comment and not a section header */
static int count_key_rows(const char* path) {
    size_t n;
    char* text = read_all(path, &n);
    const char* line = text;
    int count = 0;
    while (*line) {
        const char* nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        if (len && line[0] != ';' && line[0] != '[' && memchr(line, '=', len)) count++;
        if (!nl) break;
        line = nl + 1;
    }
    free(text);
    return count;
}
/* a file of zz_test_000 to zz_test_(keys - 1), keys that no section knows */
static char* make_keys_text(int keys) {
    size_t cap = (size_t)keys * 24 + 1;
    char* text = malloc(cap);
    size_t at = 0;
    int i;
    assert(text);
    for (i = 0; i < keys; i++) at += (size_t)snprintf(text + at, cap - at, "zz_test_%03d=1\n", i);
    assert(at < cap);
    return text;
}
/* back_key with a value of len bytes, the last one last: a value that the 64-byte captures cannot hold */
static char* make_backend_text(size_t len, char last) {
    const char* key = "back_key";
    size_t klen = strlen(key);
    size_t cap = klen + len + 3;
    char* text = malloc(cap);
    size_t i;
    assert(text);
    memcpy(text, key, klen);
    text[klen] = '=';
    for (i = 0; i + 1 < len; i++) text[klen + 1 + i] = 'a';
    text[klen + len] = last;
    text[klen + len + 1] = '\n';
    text[klen + len + 2] = 0;
    return text;
}
/* #86: a store of TEST_STORE_MAX unknown keys beside a section's default row writes at most TEST_STORE_MAX key rows,
   keeps every key, and the next start parses the file whole and writes a change */
static void case_full_store(const char* root) {
    char* path = snx_path_join(root, "full.ini");
    char line[32];
    char* text;
    int i;
    assert(path);
    text = make_keys_text(TEST_STORE_MAX);
    put(path, text);
    free(text);
    ini_open(path);
    assert(count_key_rows(path) <= TEST_STORE_MAX);
    assert_lacks(path, "def_key=");
    for (i = 0; i < TEST_STORE_MAX; i++) {
        snprintf(line, sizeof line, "zz_test_%03d=1\n", i);
        assert_has(path, line);
    }
    ini_open(path);
    ini_set("zz_test_000", "2");
    ini_poll(1);
    assert_has(path, "zz_test_000=2\n");
    free(path);
}
/* #86: a small store still gets the default rows of the keys that it lacks */
static void case_defaults_kept(const char* root) {
    char* path = snx_path_join(root, "small.ini");
    assert(path);
    put(path, "mem_key=x\n");
    ini_open(path);
    assert_has(path, "mem_key=x\n");
    assert_has(path, "def_key=dflt\n");
    free(path);
}
/* #88: ini_set before the first ini_open does not crash. The store keeps the value as a dirty entry, so the next
   ini_open keeps it over the file's value and writes it */
static void case_set_before_open(const char* root) {
    char* path = snx_path_join(root, "early.ini");
    assert(path);
    ini_set("mem_key", "early");
    put(path, "mem_key=file\n");
    ini_open(path);
    assert_has(path, "mem_key=early\n");
    free(path);
}
/* #88: a write that fails keeps the value dirty, and the next poll writes it */
static void case_write_failed_keeps_dirty(const char* root) {
    char* path = snx_path_join(root, "retry.ini");
    char tmp[4096];
    assert(path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    put(path, "mem_key=one\n");
    ini_open(path);
    assert(!snx_mkdirs(tmp));
    ini_set("mem_key", "two");
    ini_poll(0);
    assert_has(path, "mem_key=one\n");
    assert(!snx_remove_empty_dir(tmp));
    ini_poll(0);
    assert_has(path, "mem_key=two\n");
    free(path);
}
/* #89: the store holds the new value when apply runs, and the value is written at the next poll */
static void case_store_before_apply(const char* root) {
    char* path = snx_path_join(root, "order.ini");
    assert(path);
    put(path, "mem_key=x\n");
    ini_open(path);
    snprintf(g_test_backend_store, sizeof g_test_backend_store, "(unset)");
    assert(options_change("back_key", "fresh") == OPTIONS_OK);
    assert(!strcmp(g_test_backend, "fresh"));
    assert(!strcmp(g_test_backend_store, "fresh"));
    ini_poll(1);
    assert_has(path, "back_key=fresh\n");
    free(path);
}

/* #90: the limit counts the store's keys and the missing default rows together: at TEST_STORE_MAX the write is full */
static void case_store_limit_boundary(const char* root) {
    char* path = snx_path_join(root, "boundary.ini");
    char* text;
    assert(path);
    text = make_keys_text(TEST_STORE_MAX - 1);
    put(path, text);
    free(text);
    ini_open(path);
    assert_has(path, "def_key=dflt\n");
    free(path);
}
/* #90: one past the limit the write warns once, with the store's key count and the missing default count */
static void case_write_limit_warns(const char* root) {
    char* path = snx_path_join(root, "over.ini");
    char expect[128];
    char* text;
    assert(path);
    text = make_keys_text(TEST_STORE_MAX);
    put(path, text);
    free(text);
    log_clear();
    ini_open(path);
    snprintf(expect, sizeof expect, "settings: more than %d keys (%d in the store and 1 with a default)",
             TEST_STORE_MAX, TEST_STORE_MAX);
    assert(g_test_warn_count == 1);
    assert(strstr(g_test_log, expect));
    free(path);
}
/* #90: a store value of TEST_VALUE_MAX - 1 bytes reaches a backend_ini row whole, not cut */
static void case_row_value_long(const char* root) {
    char* path = snx_path_join(root, "long.ini");
    char* text;
    assert(path);
    text = make_backend_text(TEST_VALUE_MAX - 1, 'Z');
    put(path, text);
    free(text);
    ini_open(path);
    assert(g_test_backend_len == (size_t)(TEST_VALUE_MAX - 1));
    assert(g_test_backend_last == 'Z');
    free(path);
}
/* #90: the command line's value wins over the store's and reaches the backend_ini row */
static void case_row_value_override(const char* root) {
    char* path = snx_path_join(root, "override.ini");
    char* text;
    assert(path);
    text = make_backend_text(4, 'x');
    put(path, text);
    free(text);
    g_test_backend[0] = 0;
    g_test_backend_len = 0;
    ini_override("back_key", "cli");
    ini_open(path);
    assert(!strcmp(g_test_backend, "cli"));
    assert(g_test_backend_len == (size_t)3);
    free(path);
}
/* #90: with no store value and no override, the row's default reaches the row's variable */
static void case_row_value_default(const char* root) {
    char* path = snx_path_join(root, "default.ini");
    assert(path);
    put(path, "mem_key=x\n");
    g_test_def[0] = 0;
    ini_open(path);
    assert(!strcmp(g_test_def, "dflt"));
    free(path);
}

int main(void) {
    char* root = snx_temp_dir(NULL);
    char* path;
    char tmp[4096];
    char* text;
    char* before;
    size_t n;
    int i;
    assert(root);
    path = snx_path_join(root, "game.ini");
    assert(path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);

    /* before the first ini_open of the run, as the game's menu can set a value there */
    case_set_before_open(root);
    /* the first write that goes over the store's limit: the warning is once for the run */
    case_write_limit_warns(root);

    /* a full store: a change of a key in the file is written, and its flag clears */
    text = make_text(255, "one");
    put(path, text);
    free(text);
    ini_open(path);
    assert_has(path, "mem_key=one");
    ini_set("mem_key", "two");
    ini_poll(1);
    assert_has(path, "mem_key=two");
    /* the flag is clear: a file edited afterwards wins over the value that the store had */
    text = make_text(255, "three");
    put(path, text);
    free(text);
    ini_poll(0);
    ini_poll(0);
    assert_has(path, "mem_key=three");
    assert(!strcmp(g_test_mem, "three"));

    /* a change that a full store cannot save: one WARN, no write, the value is in memory */
    before = read_all(path, &n);
    log_clear();
    assert(options_change("new_key", "fresh") == OPTIONS_OK);
    ini_poll(1);
    assert_same(path, before, n);
    assert(!snx_path_exists(tmp));
    assert(!strcmp(g_test_new, "fresh"));
    assert(g_test_warn_count == 1);
    assert(strstr(g_test_log, "settings: the store is full, new_key is not saved"));
    free(before);

    /* blocked: the changes of the file's keys are not written, and a complete reload writes them */
    text = make_text(256, "zero"); /* 257 keys: k256 is left out */
    put(path, text);
    free(text);
    ini_open(path);
    before = read_all(path, &n);
    log_clear();
    for (i = 1; i <= 100; i++) {
        char key[16], value[16];
        snprintf(key, sizeof key, "k%d", i);
        snprintf(value, sizeof value, "game%d", i);
        ini_set(key, value);
    }
    ini_poll(0);
    assert_same(path, before, n);
    assert(!snx_path_exists(tmp));
    free(before);
    /* the file the store keeps whole: the 100 changes come back from the store */
    text = make_text(100, "zero");
    put(path, text);
    free(text);
    ini_poll(0);
    ini_poll(0);
    for (i = 1; i <= 100; i++) {
        char line[32];
        snprintf(line, sizeof line, "k%d=game%d\n", i, i);
        assert_has(path, line);
    }
    assert(!snx_path_exists(tmp));

    /* a reload keeps a change made in the game while the writes were blocked over the file's value */
    text = make_text(256, "four"); /* 257 keys: the writes are blocked */
    put(path, text);
    free(text);
    ini_open(path);
    ini_set("mem_key", "ingame");
    text = make_text(3, "five");
    put(path, text);
    free(text);
    ini_poll(0);
    ini_poll(0);
    assert(!strcmp(g_test_mem, "ingame"));
    assert(!strcmp(snx_settings_value(&g_test_table[0]), "ingame"));
    assert_has(path, "mem_key=ingame\n");

    case_full_store(root);
    case_defaults_kept(root);
    case_write_failed_keeps_dirty(root);
    case_store_before_apply(root);
    case_store_limit_boundary(root);
    case_row_value_long(root);
    case_row_value_default(root);
    case_row_value_override(root); /* last: an override lasts for the rest of the run */

    free(path);
    free(root);
    puts("runtime_settings_dirty passed");
    return 0;
}
