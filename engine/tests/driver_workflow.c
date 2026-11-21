/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Exercise the real driver/supervisor with synthetic recompilation and game children. */
#define _POSIX_C_SOURCE 200809L
#include "common/utilities/common.h"
#include "common/utilities/setup.h"
#include "nx/runtime/driver/driver.h"
#include "nx/runtime/driver/game_name.h"
#include "nx/runtime/driver/recompile.h"
#include "snx_settings.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef _WIN32
#define TEST_PROGRAM_SUFFIX ".exe"
#else
#define TEST_PROGRAM_SUFFIX ""
#endif

static unsigned checks;
static void check(int good, const char* expression, int line) {
    ++checks;
    if (!good) {
        fprintf(stderr, "driver_workflow.c:%d: check failed: %s\n", line, expression);
        exit(90);
    }
}
#define CHECK(value) check((value) != 0, #value, __LINE__)
static void set_env(const char* name, const char* value) {
#ifdef _WIN32
    CHECK(!_putenv_s(name, value));
#else
    CHECK(!setenv(name, value, 1));
#endif
}
static int event(char stage) {
    FILE* log = snx_fopen(getenv("SNX_WORKFLOW_LOG"), "a");
    const char *fail = getenv("SNX_WORKFLOW_FAIL"), *mode = getenv("SNX_WORKFLOW_MODE");
    CHECK(log != NULL);
    CHECK(fputc(stage, log) != EOF);
    CHECK(!fclose(log));
    if (!fail || fail[0] != stage || !strcmp(mode, "missing")) return 0;
#ifndef _WIN32
    if (!strcmp(mode, "interrupt")) raise(SIGINT);
    if (!strcmp(mode, "terminate")) raise(SIGTERM);
    if (!strcmp(mode, "stop")) raise(SIGSTOP);
    if (!strcmp(mode, "parent")) {
        int ready[2];
        char byte;
        pid_t worker;
        CHECK(!pipe(ready));
        worker = fork();
        CHECK(worker >= 0);
        if (!worker) {
            close(ready[0]);
            signal(SIGTERM, SIG_IGN);
            CHECK(write(ready[1], "x", 1) == 1);
            close(ready[1]);
            sleep(2);
            /* A surviving compiler grandchild would keep the capture pipe open and log X. */
            event('X');
            _exit(0);
        }
        close(ready[1]);
        CHECK(read(ready[0], &byte, 1) == 1);
        close(ready[0]);
        CHECK(!kill(getppid(), SIGTERM));
        for (;;) pause();
    }
#else
    (void)mode;
#endif
    return 7;
}
/* this build's stamp: ABI 4, made from commit abc1234 */
#define SNX_WORKFLOW_STAMP "4\nabc1234\n"
char* snx_game_stamp(const char* mods) {
    return mods[0] ? snx_format("%s mods %s", SNX_WORKFLOW_STAMP, mods) : snx_strdup(SNX_WORKFLOW_STAMP);
}
/* recompile.c's: the runtime a game's folder gets, this program (which the recompile below copies as the game), and
   its deploy, as recompile.c's: a copy made executable that replaces the game's program, a new file */
char* snx_own_runtime(const SnxPaths* paths) { return snx_strdup(paths->executable); }
int snx_deploy_runtime(const char* run, const char* exe) {
    char* tmp = snx_format("%s.tmp", exe);
    int rc = snx_copy_file(run, tmp) || snx_make_executable(tmp) || snx_replace(tmp, exe);
    free(tmp);
    return rc;
}
/* recompile.c's: a setting of the recompile, from the generated tables */
const IniOpt* snx_recompile_setting(const char* key) {
    const IniOpt* o = ini_find(g_recompile_ini, key);
    if (!o) o = ini_find(g_llvm_ini, key);
    return o ? o : ini_find(g_recompile_game_ini, key);
}
/* the game's program and its stamp, as a recompile makes them: the program a copy of this one, a code file beside it */
static void make_game(const char* folder, const char* name, const SnxPaths* paths) {
    char* game = snx_format("%s/%s%s", folder, name, TEST_PROGRAM_SUFFIX);
    char* stamp = snx_format("%s/%s.abi", folder, name);
    char* code = snx_format("%s/%s_code.%s", folder, name,
#ifdef _WIN32
                            "dll"
#else
                            "so"
#endif
    );
    CHECK(!snx_mkdirs(folder) && !snx_copy_file(paths->executable, game) && !snx_make_executable(game));
    CHECK(!snx_write_file(stamp, SNX_WORKFLOW_STAMP, strlen(SNX_WORKFLOW_STAMP)));
    CHECK(!snx_write_file(code, "synthetic", 9));
    free(game);
    free(stamp);
    free(code);
}
/* the driver's -r stage, as recompile.c makes it: the game's folder, data, program and stamp from the source, which
   is an NRO or an ExeFS folder; the source is left where it is */
int snx_recompile_game(int argc, char** argv, const SnxPaths* paths) {
    char *partial, *name, *folder, *data;
    int rc;
    CHECK((argc == 4 || argc == 5) && !strcmp(argv[0], "--outdir") && !strcmp(argv[2], "--"));
    CHECK(snx_is_dir(argv[3]) || snx_is_file(argv[3])); /* an NRO, or a game folder's _data */
    CHECK(!snx_mkdirs(argv[1]));
    partial = snx_path_join(argv[1], "partial-build");
    CHECK(!snx_write_file(partial, "partial", 7));
    free(partial);
    rc = event('R');
    if (rc || (!strcmp(getenv("SNX_WORKFLOW_MODE"), "missing") && getenv("SNX_WORKFLOW_FAIL") &&
               !strcmp(getenv("SNX_WORKFLOW_FAIL"), "R")))
        return rc;
    { /* the program loaded, as the recompile loads it: a broken NPDM stops it */
        SnxNpdm npdm;
        char error[256];
        if (snx_load_npdm(argv[3], &npdm, error, sizeof error)) {
            fprintf(stderr, "scarletnx: %s\n", error);
            return 1;
        }
        name = argc == 5 ? snx_strdup(argv[4]) : snx_game_name(NULL, &npdm, argv[3]);
    }
    folder = snx_path_join(argv[1], name);
    data = snx_format("%s/%s_data", folder, name);
    CHECK(!snx_mkdirs(folder) && !snx_mkdirs(data));
    partial = snx_format("%s/exefs", data);
    CHECK(!snx_mkdirs(partial));
    if (snx_is_file(argv[3])) { /* an NRO's RomFS stays in its image; its ExeFS is the NRO */
        char* main = snx_path_join(partial, "main.nro");
        CHECK(!snx_copy_file(argv[3], main));
        free(main);
    } else { /* a folder's ExeFS files */
        static const char* const files[] = {"main", "main.npdm"};
        size_t k;
        for (k = 0; k < 2; ++k) {
            char *from = snx_format("%s/exefs/%s", argv[3], files[k]), *to = snx_path_join(partial, files[k]);
            if (snx_is_file(from) && strcmp(from, to)) CHECK(!snx_copy_file(from, to));
            free(from);
            free(to);
        }
    }
    free(partial);
    {
        char* from = snx_path_join(argv[3], "romfs"), *to = snx_format("%s/romfs", data);
        if (snx_is_dir(from) && !snx_is_dir(to)) CHECK(!snx_copy_tree(from, to));
        free(from);
        free(to);
    }
    make_game(folder, name, paths);
    free(data);
    free(folder);
    free(name);
    return 0;
}
static void run(const char* exe, const char* repo, const char* input, const char* log, int expected, const char* events,
                const char* diagnostic) {
    const char* args[16];
    const char *headless = getenv("SNX_WORKFLOW_HEADLESS"), *flags = getenv("SNX_WORKFLOW_FLAGS");
    const char* outdir = getenv("SNX_WORKFLOW_OUTDIR");
    int n = 5;
    char* output = NULL;
    unsigned char* recorded;
    size_t size;
    int rc;
    CHECK(!snx_write_file(log, "", 0));
    args[0] = exe;
    args[1] = "--repo";
    args[2] = repo;
    args[3] = "--build-dir";
    args[4] = repo;
    if (flags && *flags) args[n++] = flags;
    if (headless && *headless) args[n++] = headless;
    args[n++] = input;
    if (outdir && *outdir) { /* "none": the option without its folder */
        args[n++] = "--outdir";
        if (strcmp(outdir, "none")) args[n++] = outdir;
    }
    if (getenv("SNX_WORKFLOW_ARGS") && *getenv("SNX_WORKFLOW_ARGS")) {
        args[n++] = "--";
        args[n++] = "some arg";
        args[n++] = "";
        args[n++] = "-rx";
        args[n++] = "--headless=0";
    }
    args[n] = NULL;
    rc = snx_run(args, NULL, NULL, &output);
    if (rc != expected) fprintf(stderr, "expected %d, got %d: %s\n", expected, rc, output);
    CHECK(rc == expected);
    if (diagnostic && !strstr(output, diagnostic))
        fprintf(stderr, "expected it to say %s, got: %s\n", diagnostic, output);
    if (diagnostic) CHECK(strstr(output, diagnostic) != NULL);
    recorded = snx_read_file(log, &size);
    if (recorded && strcmp((const char*)recorded, events))
        fprintf(stderr, "expected the stages %s, got %s: %s\n", events, (const char*)recorded, output);
    CHECK(recorded && !strcmp((const char*)recorded, events));
    free(recorded);
    free(output);
}
/* the runtime's settings (runtime/options.c): those this test gives */
int snx_setting_known(const char* key) {
    static const char* const known[] = {"touch", "controls", "render_resolution", "controls_a", "seconds", "slp_vectorizer"};
    size_t i;
    for (i = 0; i < sizeof known / sizeof *known; ++i)
        if (!strcmp(key, known[i])) return 1;
    return 0;
}
/* snx_record_source on the .ini a game has: made with [Game] when there is none, an older source= anywhere taken out
   and the new one put under [Game], CRLF lines and a last line without its newline kept, a file naming the dump
   already left as it is, and a path with a line break refused */
static void record_source(const char* root) {
    static const char before[] = "; a comment\r\n[Display]\r\nlanguage=2\r\n[Game]\r\n[Other]\r\n source = old\r\nkey=1";
    static const char after[] = "; a comment\r\n[Display]\r\nlanguage=2\r\n[Game]\r\nsource=/new dump.nsp\n[Other]\r\n"
                                "key=1\n";
    static const char twice[] = "source=/x\n[Display]\nsource = /x\n";
    char *folder = snx_path_join(root, "record"), *ini = snx_game_ini(folder, "G"), *text, *source;
    size_t size;
    int recorded;
    CHECK(!snx_record_source(folder, "G", "/a dump.xci"));
    text = (char*)snx_read_file(ini, &size);
    CHECK(text && !strcmp(text, "[Game]\nsource=/a dump.xci\n"));
    free(text);
    CHECK(!snx_write_file(ini, before, sizeof before - 1) && !snx_record_source(folder, "G", "/new dump.nsp"));
    text = (char*)snx_read_file(ini, &size);
    CHECK(text && !strcmp(text, after));
    free(text);
    source = snx_game_source(folder, "G", &recorded);
    CHECK(recorded && !strcmp(source, "/new dump.nsp"));
    free(source);
    CHECK(!snx_write_file(ini, twice, sizeof twice - 1) && !snx_record_source(folder, "G", "/x"));
    text = (char*)snx_read_file(ini, &size);
    CHECK(text && !strcmp(text, twice)); /* named already */
    free(text);
    CHECK(snx_record_source(folder, "G", "/a\nb"));
    CHECK(!snx_remove_tree(ini));
    source = snx_game_source(folder, "G", &recorded);
    text = snx_path_join(folder, "G.source");
    CHECK(!recorded && source && !strcmp(source, text));
    free(text);
    free(source);
    free(ini);
    free(folder);
}
/* snx_record_romfs: as snx_record_source, with romfs=: [Game] made when there is none, an older romfs= taken out, an
   empty value recorded, and source= in the same file kept */
static void record_romfs(const char* root) {
    static const char before[] = "[Game]\nsource=/a dump.nsp\nromfs=/old\n[Other]\nkey=1\n";
    static const char after[] = "[Game]\nromfs=/new romfs\nsource=/a dump.nsp\n[Other]\nkey=1\n";
    char *folder = snx_path_join(root, "record"), *ini = snx_game_ini(folder, "R"), *text, *value;
    size_t size;
    CHECK(!snx_record_romfs(folder, "R", "/a romfs"));
    text = (char*)snx_read_file(ini, &size);
    CHECK(text && !strcmp(text, "[Game]\nromfs=/a romfs\n"));
    free(text);
    CHECK(!snx_write_file(ini, before, sizeof before - 1) && !snx_record_romfs(folder, "R", "/new romfs"));
    text = (char*)snx_read_file(ini, &size);
    CHECK(text && !strcmp(text, after));
    free(text);
    value = snx_ini_value(ini, "source");
    CHECK(value && !strcmp(value, "/a dump.nsp"));
    free(value);
    CHECK(!snx_record_romfs(folder, "R", ""));
    text = (char*)snx_read_file(ini, &size);
    CHECK(text && strstr(text, "romfs=\n") && strstr(text, "source=/a dump.nsp"));
    free(text);
    CHECK(!snx_remove_tree(ini));
    free(ini);
    free(folder);
}
/* snx_record_folders: a game of the user's data folder gets <name>.folders naming the config, state and cache folders
   snx_user_path moves its files to, each made, the logs and caches Settings > App > Folders picked in app.ini among
   them; a game elsewhere none, and an older one there is taken away. The user's folders in root for the test, from the
   variables that name them */
static void record_folders(const char* root) {
#ifdef _WIN32
    static const char* const vars[] = {"APPDATA"};
#elif defined(SNX_XDG)
    static const char* const vars[] = {"XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"};
#else
    static const char* const vars[] = {"HOME"};
#endif
    static const char* const keys[] = {"config", "state", "cache"};
    char *old[4], *value, *data, *folder, *record, *text, *expected, *line, *other;
    size_t i, size;
    int kind;
    for (i = 0; i < sizeof vars / sizeof *vars; ++i) {
        old[i] = getenv(vars[i]) ? snx_strdup(getenv(vars[i])) : NULL;
        value = snx_format("%s/user %d", root, (int)i);
        set_env(vars[i], value);
        free(value);
    }
    data = snx_user_data_dir();
    CHECK(data != NULL);
    folder = snx_format("%s/games/G", data);
    record = snx_format("%s/G.folders", folder);
    expected = snx_strdup("");
    CHECK(!snx_mkdirs(folder) && !snx_record_folders(folder, "G"));
    for (kind = SNX_USER_CONFIG; kind <= SNX_USER_CACHE; ++kind) {
        char* moved = snx_user_path(folder, kind);
        CHECK(strcmp(moved, folder) && snx_is_dir(moved));
        line = snx_format("%s%s=%s\n", expected, keys[kind - SNX_USER_CONFIG], moved);
        free(expected);
        free(moved);
        expected = line;
    }
    text = (char*)snx_read_file(record, &size);
    if (!text || strcmp(text, expected))
        fprintf(stderr, "%s has:\n%s\nnot:\n%s", record, text ? text : "(no such file)\n", expected);
    CHECK(text && !strcmp(text, expected));
    free(text);
    { /* Logs and Caches picked, outside the user's folders: named too, so a game started elsewhere finds them */
        char *config = snx_user_dir(SNX_USER_CONFIG), *ini = snx_path_join(config, "app.ini"), *picked[2], *game[2],
             *at;
        picked[0] = snx_path_join(root, "picked logs");
        picked[1] = snx_path_join(root, "picked caches");
        text = snx_format("[folders]\nstate = %s\ncache = %s\n", picked[0], picked[1]);
        CHECK(!snx_mkdirs(config) && !snx_write_file(ini, text, strlen(text)));
        free(text);
        CHECK(!snx_record_folders(folder, "G"));
        for (i = 0; i < 2; ++i) { /* games/G under each picked folder, made */
            game[i] = snx_user_path(folder, i ? SNX_USER_CACHE : SNX_USER_STATE);
            CHECK(strcmp(game[i], folder) && snx_path_within(game[i], picked[i]) && snx_is_dir(game[i]));
        }
        at = snx_user_path(folder, SNX_USER_CONFIG);
        free(expected);
        expected = snx_format("config=%s\nstate=%s\ncache=%s\n", at, game[0], game[1]);
        free(at);
        text = (char*)snx_read_file(record, &size);
        if (!text || strcmp(text, expected))
            fprintf(stderr, "%s has:\n%s\nnot:\n%s", record, text ? text : "(no such file)\n", expected);
        CHECK(text && !strcmp(text, expected));
        free(text);
        for (i = 0; i < 2; ++i) {
            free(picked[i]);
            free(game[i]);
        }
        CHECK(!remove(ini));
        free(ini);
        free(config);
    }
    free(record);
    other = snx_format("%s/elsewhere/G", root);
    record = snx_format("%s/G.folders", other);
    CHECK(!snx_mkdirs(other) && !snx_write_file(record, expected, strlen(expected)));
    CHECK(!snx_record_folders(other, "G") && !snx_path_exists(record));
    for (i = 0; i < sizeof vars / sizeof *vars; ++i) {
#ifdef _WIN32
        CHECK(!_putenv_s(vars[i], old[i] ? old[i] : ""));
#else
        CHECK(old[i] ? !setenv(vars[i], old[i], 1) : !unsetenv(vars[i]));
#endif
        free(old[i]);
    }
    free(data);
    free(folder);
    free(record);
    free(expected);
    free(other);
}
static void reset(const char* repo, const char* input) {
    CHECK(!snx_remove_tree(repo));
    CHECK(!snx_mkdirs(repo));
    CHECK(!snx_write_file(input, "synthetic", 9));
    set_env("SNX_WORKFLOW_FAIL", "");
}
int main(int argc, char** argv) {
    char *exe, *root, *repo, *nro, *xci, *log, *folder, *game, *cwd;
    const char* args[4];
    char* output;
    int rc;
#ifndef _WIN32
    const char* modes[] = {"interrupt", "terminate", "stop", "parent"};
    int statuses[] = {128 + SIGINT, 128 + SIGTERM, 128 + SIGSTOP, 128 + SIGTERM};
    size_t i;
#endif
    if (argc == 3 && !strcmp(argv[1], "--probe")) {
        args[0] = argv[0];
        args[1] = NULL;
        if (!strcmp(argv[2], "exec")) args[0] = "/nonexistent/scarletnx-test";
        return snx_run_stage(args, !strcmp(argv[2], "cwd") ? "/nonexistent/scarletnx-test" : NULL, "Probe");
    }
    if (argc > 1) { /* the setup's options taken out first, as the program's entry does (runtime/entry.c) */
        char why[160];
        argc = snx_setup_options(argc, argv, why, sizeof why);
        if (argc < 0) {
            fprintf(stderr, "scarletnx: %s\n", why);
            return 2;
        }
        rc = snx_driver(argc, argv);
        if (rc >= 0) return rc;
    }
    if (getenv("SNX_WORKFLOW_CHILD")) {
        const char* flag = getenv("SNX_WORKFLOW_HEADLESS");
        const char* fullscreen = getenv("SNX_WORKFLOW_FLAGS");
        if (fullscreen &&
            (!strcmp(fullscreen, "--fullscreen") || !strcmp(fullscreen, "--fullscreen=1") || !strcmp(fullscreen, "-f")))
            CHECK(argc == 2 && !strcmp(argv[1], "-f"));
        if (fullscreen && !strcmp(fullscreen, "--fullscreen=0")) CHECK(argc == 2 && !strcmp(argv[1], "--fullscreen=0"));
        /* the game's own settings, any --key=value of its .ini, reach it as they were given */
        if (fullscreen && strchr(fullscreen, '=') && strncmp(fullscreen, "--fullscreen=", 13))
            CHECK(argc == 2 && !strcmp(argv[1], fullscreen));
        if (flag && *flag) CHECK(argc == 2 && !strcmp(argv[1], "-H"));
        if (getenv("SNX_WORKFLOW_ARGS") && *getenv("SNX_WORKFLOW_ARGS")) {
            CHECK(argc == 6 && !strcmp(argv[1], "--"));
            CHECK(!strcmp(argv[2], "some arg") && !strcmp(argv[3], ""));
            CHECK(!strcmp(argv[4], "-rx") && !strcmp(argv[5], "--headless=0"));
        }
        /* All game invocations must chdir, including repeat launches. */
        cwd = snx_absolute(".");
        {
            char* expected_cwd = snx_absolute(getenv("SNX_WORKFLOW_GAMES"));
            CHECK(cwd && expected_cwd && !strcmp(cwd, expected_cwd));
            free(expected_cwd);
        }
        free(cwd);
        return event('G');
    }
    exe = snx_executable(argv[0]);
    root = snx_temp_dir(NULL);
    CHECK(exe && root);
    record_source(root);
    record_romfs(root);
    record_folders(root);
    repo = snx_path_join(root, "checkout with spaces");
    CHECK(!snx_mkdirs(repo));
    nro = snx_path_join(repo, "Test [game];.NRO");
    xci = snx_path_join(repo, "Test [game];.XCI");
    log = snx_path_join(root, "events");
    folder = snx_path_join(repo, "recompiled/Test game");
#ifdef _WIN32
    game = snx_path_join(folder, "Test game.exe");
#else
    game = snx_path_join(folder, "Test game");
#endif
    set_env("SNX_WORKFLOW_GAMES", folder);
    set_env("SNX_WORKFLOW_CHILD", "1");
    set_env("SNX_WORKFLOW_LOG", log);
    set_env("SNX_WORKFLOW_MODE", "exit");
    set_env("SNX_WORKFLOW_HEADLESS", "");
    set_env("SNX_WORKFLOW_FLAGS", "");
    set_env("SNX_WORKFLOW_OUTDIR", "");
    set_env("SNX_WORKFLOW_ARGS", "");
    /* an NRO is recompiled and played, with no extraction, and its game folder keeps the NRO's ExeFS */
    reset(repo, nro);
    run(exe, repo, nro, log, 0, "RG", NULL);
    {
        char* work = snx_format("%s/work/Test game", repo);
        CHECK(snx_is_file(game) && !snx_path_exists(work));
        free(work);
    }
    { /* a game made already is played as it is, from its program or its folder */
        char* slash = snx_format("%s/", folder);
        run(exe, repo, game, log, 0, "G", NULL);
        run(exe, repo, folder, log, 0, "G", NULL);
        run(exe, repo, slash, log, 0, "G", NULL);
        free(slash);
    }
    /* -r recompiles it where it is, and plays nothing */
    set_env("SNX_WORKFLOW_FLAGS", "-r");
    run(exe, repo, game, log, 0, "R", NULL);
    set_env("SNX_WORKFLOW_FLAGS", "");
    /* a recompile that fails is said, and the game is not played */
    reset(repo, nro);
    set_env("SNX_WORKFLOW_FAIL", "R");
    run(exe, repo, nro, log, 7, "R", "Recompilation failed");
    set_env("SNX_WORKFLOW_FAIL", "");
    run(exe, repo, nro, log, 0, "RG", NULL);
    /* Missing/mismatched/malformed ABI stamps are never trusted, nor one another build wrote with the same ABI: an
       older version's ABI alone, another commit's, or this one's cut short or with more after it */
    {
        const char* const stale[] = {
            "3\nabc1234\n",      "broken", "4", "4garbage\n", "4\n", "4\ndef5678\n", "4\nabc1234", "4\nabc1234\n\n",
            "4\nabc1234-dirty\n"};
        char* stamp = snx_format("%s/Test game.abi", folder);
        size_t j;
        for (j = 0; j < sizeof stale / sizeof *stale; ++j) {
            CHECK(!snx_write_file(stamp, stale[j], strlen(stale[j])));
            run(exe, repo, nro, log, 0, "RG", NULL);
            run(exe, repo, game, log, 0, "G", NULL); /* the recompile stamped it as this build */
        }
        CHECK(!snx_remove_tree(stamp));
        run(exe, repo, nro, log, 0, "RG", NULL);
        free(stamp);
    }
    /* a recompile-only --KEY=VALUE is checked in the play path too, before a current stamp skips the recompile: a
       value the recompile refuses stops the run and the game does not start */
    set_env("SNX_WORKFLOW_FLAGS", "--aot_threads=abc");
    run(exe, repo, game, log, 2, "", "aot_threads=abc is not a whole number from 0 to 1024");
    set_env("SNX_WORKFLOW_FLAGS", "");
    /* a game whose runtime another build of this ABI deployed: played as it is, the runtime replaced by this build's
       first; one already this build's is left as it is (a link to it stays the same file) */
    {
        char* kept_runtime = snx_format("%s.kept", game);
        FILE* f = snx_fopen(game, "ab");
        unsigned char *now, *ours;
        size_t a, b;
        CHECK(f && fputs("another build", f) >= 0 && !fclose(f));
        run(exe, repo, game, log, 0, "G", "ran another build's runtime");
        now = snx_read_file(game, &a);
        ours = snx_read_file(exe, &b);
        CHECK(now && ours && a == b && !memcmp(now, ours, a));
        free(now);
        free(ours);
        CHECK(!snx_link_file(game, kept_runtime));
        run(exe, repo, game, log, 0, "G", NULL);
        CHECK(snx_same_file(game, kept_runtime));
        CHECK(!snx_remove_tree(kept_runtime));
        free(kept_runtime);
    }
    /* the game's failure exit status is the driver's, said */
    set_env("SNX_WORKFLOW_FAIL", "G");
    run(exe, repo, game, log, 7, "G", "Game failed (exit status 7)");
    set_env("SNX_WORKFLOW_FAIL", "");
    run(exe, repo, game, log, 0, "G", NULL);
    /* the game's own arguments, after --, reach it as they were given */
    set_env("SNX_WORKFLOW_ARGS", "1");
    run(exe, repo, game, log, 0, "G", NULL);
    set_env("SNX_WORKFLOW_ARGS", "");
    { /* fullscreen and headless, as the menu and the player pass them */
        const char* flags[] = {"--fullscreen", "-f", "--fullscreen=0"};
        size_t j;
        for (j = 0; j < sizeof(flags) / sizeof(flags[0]); ++j) {
            set_env("SNX_WORKFLOW_FLAGS", flags[j]);
            run(exe, repo, game, log, 0, "G", NULL);
        }
        set_env("SNX_WORKFLOW_FLAGS", "");
        set_env("SNX_WORKFLOW_HEADLESS", "--headless");
        run(exe, repo, game, log, 0, "G", NULL);
        set_env("SNX_WORKFLOW_HEADLESS", "");
        set_env("SNX_WORKFLOW_FLAGS", "--resoltion=2");
        run(exe, repo, game, log, 2, "", "unknown option: --resoltion=2");
        set_env("SNX_WORKFLOW_FLAGS", "--touch=2");
        run(exe, repo, game, log, 0, "G", NULL);
        set_env("SNX_WORKFLOW_FLAGS", "");
    }
    { /* --outdir puts the game there, not in the data folder, and is used again there */
        char* outdir = snx_path_join(root, "games elsewhere");
        char* folder2 = snx_path_join(outdir, "Test game");
        char* moved = snx_format("%s/Test game%s", folder2, TEST_PROGRAM_SUFFIX);
        reset(repo, nro);
        set_env("SNX_WORKFLOW_GAMES", folder2);
        set_env("SNX_WORKFLOW_OUTDIR", outdir);
        run(exe, repo, nro, log, 0, "RG", NULL);
        CHECK(snx_is_file(moved) && !snx_is_file(game));
        set_env("SNX_WORKFLOW_OUTDIR", "");
        run(exe, repo, moved, log, 0, "G", NULL);
        set_env("SNX_WORKFLOW_OUTDIR", "none");
        run(exe, repo, nro, log, 2, "", "--outdir requires a directory");
        set_env("SNX_WORKFLOW_OUTDIR", "");
        set_env("SNX_WORKFLOW_GAMES", folder);
        free(moved);
        free(folder2);
        free(outdir);
    }
    /* an update's program made from a dump is refused, played and with -r, said */
    {
        char* addon_folder = snx_path_join(repo, "recompiled/0100000000AB0000");
        char* addon = snx_path_join(addon_folder, "0100000000AB0000_0100000000AB0800" TEST_PROGRAM_SUFFIX);
        reset(repo, nro);
        CHECK(!snx_mkdirs(addon_folder) && !snx_write_file(addon, "synthetic", 9));
        run(exe, repo, addon, log, 3, "", "made from a dump");
        set_env("SNX_WORKFLOW_FLAGS", "-r");
        run(exe, repo, addon, log, 3, "", "made from a dump");
        set_env("SNX_WORKFLOW_FLAGS", "");
        free(addon);
        free(addon_folder);
    }
    /* an encrypted dump is refused, with the line that says what to give instead */
    reset(repo, xci);
    run(exe, repo, xci, log, 3, "", "is an encrypted dump");
#ifndef _WIN32
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        reset(repo, nro);
        set_env("SNX_WORKFLOW_MODE", modes[i]);
        set_env("SNX_WORKFLOW_FAIL", "R");
        run(exe, repo, nro, log, statuses[i], "R", "Recompilation interrupted by signal");
        set_env("SNX_WORKFLOW_FAIL", "");
        run(exe, repo, nro, log, 0, "RG", NULL);
        set_env("SNX_WORKFLOW_FAIL", "G");
        run(exe, repo, game, log, statuses[i], "G", "Game interrupted by signal");
        set_env("SNX_WORKFLOW_FAIL", "");
        run(exe, repo, game, log, 0, "G", NULL);
    }
#endif
    reset(repo, nro);
    set_env("SNX_WORKFLOW_MODE", "missing");
    set_env("SNX_WORKFLOW_FAIL", "R");
    run(exe, repo, nro, log, 4, "R", "recompilation finished without creating");
    set_env("SNX_WORKFLOW_FAIL", "");
    set_env("SNX_WORKFLOW_MODE", "exit");
    run(exe, repo, nro, log, 0, "RG", NULL);
    /* The probe takes a third argument, so use a separate, terminated vector. */
    {
        const char* probe[4];
        probe[0] = exe;
        probe[1] = "--probe";
        probe[2] = "cwd";
        probe[3] = NULL;
        output = NULL;
        rc = snx_run(probe, NULL, NULL, &output);
        CHECK(rc != 0 && strstr(output, "Probe"));
#ifndef _WIN32
        CHECK(strstr(output, "chdir to the child's working directory"));
#endif
        free(output);
        probe[2] = "exec";
        output = NULL;
        rc = snx_run(probe, NULL, NULL, &output);
        CHECK(rc != 0 && strstr(output, "Probe"));
#ifndef _WIN32
        CHECK(strstr(output, "executing child process"));
#endif
        free(output);
    }
#ifndef _WIN32
    /* --setup goes through the setup in the terminal, here a stdin with nothing to read: it stops at the first
       question, writing nothing; --skip alone is refused; --debug runs with no setup, in a home with no setup.ini */
    {
        const char* setup[4];
        char* home = snx_path_join(root, "setup home");
        set_env("XDG_DATA_HOME", home);
        set_env("XDG_STATE_HOME", home);
        set_env("XDG_CACHE_HOME", home);
        CHECK(freopen("/dev/null", "r", stdin) != NULL); /* the children's */
        setup[0] = exe;
        setup[1] = "--setup";
        setup[2] = NULL;
        output = NULL;
        rc = snx_run(setup, NULL, home, &output);
        CHECK(rc == 1 && strstr(output, "Changed in older versions\n") && strstr(output, "no more answers"));
        free(output);
        setup[1] = "--skip";
        setup[2] = nro;
        setup[3] = NULL;
        output = NULL;
        CHECK(snx_run(setup, NULL, home, &output) == 2 && strstr(output, "--skip --skip"));
        free(output);
        setup[1] = "--debug";
        setup[2] = "/nonexistent/game";
        output = NULL;
        CHECK(snx_run(setup, NULL, home, &output) == 3 && strstr(output, "/nonexistent/game is not a file") &&
              !strstr(output, "Changed"));
        free(output);
        free(home);
    }
#endif
    /* --dump-modules: a program that cannot be loaded is the game's files, 3 */
    {
        char* out = snx_path_join(root, "dump out");
        char* missing = snx_path_join(root, "no such program");
        char* dump[4];
        dump[0] = exe;
        dump[1] = "--dump-modules";
        dump[2] = missing;
        dump[3] = out;
        CHECK(snx_driver(4, dump) == 3);
        free(missing);
        free(out);
    }
    /* --dump-modules' code of a failed dump, from exit_codes.h: the program's files 3, a failed write or a failure of
       the host 1 */
    CHECK(dump_exit_code(SNX_DUMP_INPUT) == 3);
    CHECK(dump_exit_code(SNX_DUMP_OUTPUT) == 1);
    CHECK(dump_exit_code(SNX_DUMP_INTERNAL) == 1);
    CHECK(!snx_remove_tree(root));
    free(exe);
    free(root);
    free(repo);
    free(nro);
    free(xci);
    free(log);
    free(folder);
    free(game);
    printf("Driver workflow: %u checks passed\n", checks);
    return 0;
}
