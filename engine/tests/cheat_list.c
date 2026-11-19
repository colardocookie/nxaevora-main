/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* common/utilities/cheat_list.c: a cheat's name from its line, the names of a cheats folder's files, each once, and
   cheats_off='s names */
#include "common/utilities/cheat_list.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put(const char* dir, const char* name, const char* text) {
    char* path = snx_path_join(dir, name);
    assert(path && !snx_write_file(path, text, strlen(text)));
    free(path);
}

int main(void) {
    char name[64], *root = snx_temp_dir(NULL), *data, *folder, *tail, **names;
    size_t n;
    assert(root);
    /* a line's name: [name] or the master code's {name}, spaces before it; a code line has none */
    assert(snx_cheat_name("[60 FPS]", name, sizeof name) && !strcmp(name, "60 FPS"));
    assert(snx_cheat_name("  {Master Code}\r", name, sizeof name) && !strcmp(name, "Master Code"));
    assert(snx_cheat_name("[Speed x2/x4", name, sizeof name) && !strcmp(name, "Speed x2/x4")); /* unclosed: the rest */
    assert(!snx_cheat_name("04000000 01234567 00000001", name, sizeof name));
    assert(snx_cheat_name("[abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij]", name, 8) &&
           !strcmp(name, "abcdefg")); /* cut to the room given */
    /* a folder's .txt files by their names, each name once as the first file has it; other files and hidden ones out */
    data = snx_path_join(root, "Game_data");
    folder = snx_cheats_folder(data);
    tail = snx_path_join("Game_data", "cheats"); /* in the system's separators, as the folder is */
    assert(strlen(folder) > strlen(tail) && !strcmp(folder + strlen(folder) - strlen(tail), tail));
    free(tail);
    assert(!snx_cheats_list(folder, &names) && !names); /* no folder: none */
    assert(!snx_mkdirs(folder));
    put(folder, "B000000000000000.txt", "[Infinite HP]\n04000000 00000010 00000063\n[60 FPS]\n04000000 0 1\n");
    put(folder, "A000000000000000.txt", "{Master}\n\n[60 FPS]\r\n04000000 0 3C\r\n");
    put(folder, "notes.md", "[Not a cheat]\n");
    put(folder, ".hidden.txt", "[Hidden]\n");
    n = snx_cheats_list(folder, &names);
    assert(n == 3 && !strcmp(names[0], "Master") && !strcmp(names[1], "60 FPS") && !strcmp(names[2], "Infinite HP"));
    snx_free_strings(names, n);
    /* cheats_off=: names a | apart, a name whole, not a part of one */
    assert(snx_cheat_off("60 FPS|Infinite HP", "Infinite HP") && snx_cheat_off("60 FPS|Infinite HP", "60 FPS"));
    assert(!snx_cheat_off("60 FPS|Infinite HP", "60") && !snx_cheat_off("", "60 FPS") && !snx_cheat_off(NULL, "x"));
    assert(snx_cheat_off("Speed x2/x4", "Speed x2/x4")); /* a / is a name's own */
    assert(!snx_remove_tree(root));
    free(folder);
    free(data);
    free(root);
    puts("PASS cheat names: a line's, a folder's each once, cheats_off='s");
    return 0;
}
