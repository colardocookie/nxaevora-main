/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The .ini's store (src/nx/runtime/options.c): an in-game change written to the file and its flag cleared, a change
   that a full store cannot save, a write blocked by an incomplete parse, and a reload that keeps the value made in
   the game over the file's */
#include "common/utilities/common.h"
#include "nx/runtime/options.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char g_test_log[8192];
extern size_t g_test_log_len;
extern int g_test_warn_count;
extern char g_test_mem[64];
extern char g_test_new[64];
extern const IniOpt g_test_table[];

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
    assert_has(path, "mem_key=ingame");

    free(path);
    free(root);
    puts("runtime_settings_dirty passed");
    return 0;
}
