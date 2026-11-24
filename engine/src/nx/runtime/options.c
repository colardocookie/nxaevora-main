/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The game's .ini: the sections it is written in (the runtime's own settings here, the window's, the on-screen
   controls' and the gamepads' from their modules), the command line's overrides over it, its reload while the game
   runs, and the values the game's menu changes, written back */
#include "nx/runtime/options.h"
#include "common/utilities/common.h"
#include "common/utilities/ini_lines.h"
#include "nx/hle/hle.h"
#include "nx/hle/kernel/kernel.h"
#include "nx/hle/services/services.h"
#include "nx/memory/mem.h"
#include "nx/runtime/ini.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/main.h"
#include "nx/runtime/platform/backend.h"
#include "nx/runtime/platform/gamepad.h"
#include "nx/runtime/platform/hud.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include "nx/runtime/platform/unattended.h"
#include "nx/runtime/platform/vpad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_opt_fps = SNX_FPS_GAME;

/* ---- the runtime's own settings ---- */

/* a language the console doesn't have is English (US), as the clamp to the table's range would not say */
void options_language_set(const IniOpt* o, const char* v) {
    char* end;
    long n = strtol(v, &end, 10);
    (void)o;
    if (end != v && (n < 0 || n >= set_language_count())) g_settings.language = 1;
}

/* "game" in any case, or a number: 0 and below 1 unlimited, 10 to 1000 frames a second (clamped), and the values an
   older file meant the game's pace with (negative ones, and 1, its 60 FPS). Anything else leaves the limit */
void options_fps_set(const IniOpt* o, const char* v) {
    char* end;
    double d;
    int fps;
    (void)o;
    while (*v == ' ' || *v == '\t') v++;
    if ((v[0] == 'g' || v[0] == 'G') && (v[1] == 'a' || v[1] == 'A') && (v[2] == 'm' || v[2] == 'M') &&
        (v[3] == 'e' || v[3] == 'E') && !v[4 + strspn(v + 4, " \t")]) {
        __atomic_store_n(&g_opt_fps, SNX_FPS_GAME, __ATOMIC_RELAXED);
        return;
    }
    d = strtod(v, &end);
    if (end == v || d != d) return;
    if (d < 0 || (d >= 1 && d < 2))
        fps = SNX_FPS_GAME;
    else if (d < 1)
        fps = 0;
    else
        fps = d < 10 ? 10 : d > 1000 ? 1000 : (int)d;
    __atomic_store_n(&g_opt_fps, fps, __ATOMIC_RELAXED);
}
void fps_limit_text(int fps, char* out, size_t n) {
    if (fps == SNX_FPS_GAME)
        snprintf(out, n, "game");
    else
        snprintf(out, n, "%d", fps);
}

int g_opt_ldn = SNX_OPT_LDN_LAN;
/* lan, room or off. Both keys are read once, at start: a value they don't take stops the game there, as a player
   who wrote ldn=rooom would otherwise play alone thinking they were in the room */
void options_ldn_set(const IniOpt* o, const char* v) {
    static const char* const words[] = {"lan", "room", "off"};
    int i;
    (void)o;
    for (i = 0; i < 3; i++)
        if (!strcmp(v, words[i])) {
            g_opt_ldn = i;
            return;
        }
    fatal("settings: ldn=%s is not lan, room or off", v);
}
void options_room_set(const IniOpt* o, const char* v) {
    (void)o;
    if (v[strcspn(v, " \t\r")]) fatal("settings: room=%.64s is not host or host:port", v);
}

/* MB: below 1 no cap but the heap region's own, which also caps a larger one */
void options_heap_max_set(const IniOpt* o, const char* v) {
    char* end;
    double mb = strtod(v, &end);
    (void)o;
    if (end == v || mb != mb) return;
    if (mb < 1)
        g_heap_max = 0;
    else
        g_heap_max = mb * 1048576.0 >= (double)HEAP_SIZE ? HEAP_SIZE : (u64)(mb * 1048576.0);
}

/* key's row in a section, with whether it is one of the runtime's own; NULL for none */
static const IniOpt* find_row(const char* key, int* own) {
    int s, t;
    for (s = 0; s < g_ini_sections_count; s++)
        for (t = 0; t < 3 && g_ini_sections[s].table[t]; t++) {
            const IniOpt* o = ini_find(g_ini_sections[s].table[t], key);
            if (o) {
                if (own) *own = g_ini_sections[s].own;
                return o;
            }
        }
    return NULL;
}

int snx_setting_known(const char* key) { return find_row(key, NULL) || unattended_known(key); }

/* ---- the file's values, the command line's, and the changes made in the game ---- */

#define KEY_MAX 64
#define STORE_MAX 256
#define VALUE_MAX 2048
#define OVERRIDE_MAX 64
#define CHANGE_VALUE_MAX 256 /* a gamepad map's text is up to 165 bytes (pad_map.h) */

/* The file as last read, with the changes made in the game. dirty: the value came from the game and the file does not
   have it yet. g_store_lock guards every entry and flag, and is a leaf: no call that takes a lock runs under it */
typedef struct {
    char key[KEY_MAX];
    char value[VALUE_MAX];
    int dirty;
} Entry;
static Entry g_store[STORE_MAX];
static int g_nstore;
static PMutex g_store_lock = PMUTEX_INIT;
/* the store's copy for a reload's merge or a write: the ini thread makes it, and the two never run at once */
static Entry g_work[STORE_MAX];
/* a key and the text a window set, kept until the game restarts */
typedef struct {
    char key[KEY_MAX];
    char value[CHANGE_VALUE_MAX];
} SettingText;
/* the command line's, set before ini_open and only read after */
typedef struct {
    char key[KEY_MAX];
    const char* value;
} Override;
static Override g_over[OVERRIDE_MAX];
static int g_nover;
static PMutex g_poll_lock = PMUTEX_INIT;

static char g_path[1400];
static int g_started; /* ini_open is done: the startup settings are kept from now on */
static u64 g_hash;    /* the file as this run last read or wrote it */
static u64 g_seen;    /* a changed file seen by the last poll, read once a second poll sees it unchanged */
/* where the last write_file that failed stopped, and the system's reason: the CLI's settings set names the stage */
static const char* g_write_stage;
static char g_write_why[256];
/* 1 when the last parse of the file did not keep every key: write_file then leaves the file alone, so a write cannot
   erase a key the store refused. A missing file is not parsed and an empty one gives 0, so the default file is still
   written */
static int g_write_blocked;

static u64 fnv1a(const unsigned char* p, size_t n) {
    u64 h = 0xCBF29CE484222325ull;
    size_t i;
    for (i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

static const char* override_of(const char* key) {
    int i;
    for (i = 0; i < g_nover; i++)
        if (!strcmp(g_over[i].key, key)) return g_over[i].value;
    return NULL;
}
/* the key's entry, with g_store_lock held by the caller */
static Entry* store_find_locked(const char* key) {
    int i;
    for (i = 0; i < g_nstore; i++)
        if (!strcmp(g_store[i].key, key)) return &g_store[i];
    return NULL;
}
static Entry* store_find(const char* key) {
    Entry* e;
    pm_lock(&g_store_lock);
    e = store_find_locked(key);
    pm_unlock(&g_store_lock);
    return e;
}
/* Why store_put_locked refused a value; the caller says it after it unlocks, since the log takes its own lock */
typedef enum {
    STORE_PUT_DONE = 0,
    STORE_PUT_KEY_LONG,
    STORE_PUT_VALUE_LONG,
    STORE_PUT_FULL
} StorePutResult;
/* a value that doesn't fit is dropped, never cut. g_store_lock held; a stored value is not dirty */
static StorePutResult store_put_locked(const char* key, const char* value) {
    Entry* e = store_find_locked(key);
    size_t n = strlen(value);
    if (strlen(key) >= KEY_MAX) return STORE_PUT_KEY_LONG;
    if (n >= VALUE_MAX) return STORE_PUT_VALUE_LONG;
    if (!e) {
        if (g_nstore == STORE_MAX) return STORE_PUT_FULL;
        e = &g_store[g_nstore++];
        memcpy(e->key, key, strlen(key) + 1);
    }
    memcpy(e->value, value, n + 1);
    e->dirty = 0;
    return STORE_PUT_DONE;
}
/* the one line a refused put says, once for each reason in a run */
static void store_put_warn(StorePutResult why, const char* key) {
    switch (why) {
    case STORE_PUT_KEY_LONG:
        WARN_LIMIT(1, "settings: key %.40s... is too long and is left out\n", key);
        break;
    case STORE_PUT_VALUE_LONG:
        WARN_LIMIT(1, "settings: the value of %s is longer than %d bytes and is left out\n", key, VALUE_MAX - 1);
        break;
    case STORE_PUT_FULL:
        WARN_LIMIT(1, "settings: more than %d keys, %s and those after it are left out\n", STORE_MAX, key);
        break;
    default:
        break;
    }
}
/* a file's value: 0 stored, -1 left out (the warning says why) */
static int store_put(const char* key, const char* value) {
    StorePutResult why;
    pm_lock(&g_store_lock);
    why = store_put_locked(key, value);
    pm_unlock(&g_store_lock);
    if (why != STORE_PUT_DONE) {
        store_put_warn(why, key);
        return -1;
    }
    return 0;
}
/* The text's keys into the store, with the lock held and without a warning: the caller says it after it unlocks.
   0, or -1 when the text held one the store could not keep */
static int store_parse_locked(char* text, StorePutResult* why, char why_key[KEY_MAX]) {
    SnxIni ini;
    char *key, *value;
    int failed = 0;
    g_nstore = 0;
    snx_ini_start(&ini, text);
    while (snx_ini_next(&ini, &key, &value)) {
        StorePutResult r = store_put_locked(key, value);
        if (r == STORE_PUT_DONE) continue;
        if (!failed) {
            size_t n = strlen(key);
            *why = r;
            if (n >= KEY_MAX) n = KEY_MAX - 1;
            memcpy(why_key, key, n);
            why_key[n] = 0;
        }
        failed = 1;
    }
    return failed ? -1 : 0;
}
/* the text's keys into the store: 0, or -1 when it holds one the store could not keep. The CLI's settings commands
   refuse such a file, and the runtime writes no file in such a run, since the write would lose that key */
static int store_parse(char* text) {
    StorePutResult why = STORE_PUT_DONE;
    char why_key[KEY_MAX] = "";
    int failed;
    pm_lock(&g_store_lock);
    failed = store_parse_locked(text, &why, why_key);
    pm_unlock(&g_store_lock);
    if (failed) store_put_warn(why, why_key);
    return failed ? -1 : 0;
}
/* The text's keys into the store, then the changes made in the game on top: an in-game value wins over the file's.
   The store is copied aside first, since the parse overwrites it. A change that the new store cannot hold is left out.
   0, or -1 when the text held a key the store could not keep */
static int store_reparse(char* text) {
    StorePutResult why = STORE_PUT_DONE;
    char why_key[KEY_MAX] = "";
    char lost_key[KEY_MAX] = "";
    int i, n_old, failed, lost = 0;
    pm_lock(&g_store_lock);
    memcpy(g_work, g_store, (size_t)g_nstore * sizeof *g_store);
    n_old = g_nstore;
    failed = store_parse_locked(text, &why, why_key);
    for (i = 0; i < n_old; i++) {
        if (!g_work[i].dirty) continue;
        /* the length checks of ini_set leave a full store as the only refusal of a change */
        if (store_put_locked(g_work[i].key, g_work[i].value) != STORE_PUT_DONE) {
            if (!lost) {
                memcpy(lost_key, g_work[i].key, KEY_MAX);
                lost = 1;
            }
            continue;
        }
        store_find_locked(g_work[i].key)->dirty = 1;
    }
    pm_unlock(&g_store_lock);
    if (failed) store_put_warn(why, why_key);
    if (lost) WARN_LIMIT(1, "settings: the store is full, %s is not saved\n", lost_key);
    return failed ? -1 : 0;
}
/* Only the CLI calls this (snx_settings_value), in one thread with no ini_set, so the pointer stays valid */
static const char* value_of(const IniOpt* o) {
    const char* v = override_of(o->key);
    const Entry* e;
    if (v) return v;
    e = store_find(o->key);
    return e ? e->value : o->def;
}
/* value_of with g_store_lock held by the caller, for a copy of the value under the lock */
static const char* value_of_locked(const IniOpt* o) {
    const char* v = override_of(o->key);
    const Entry* e;
    if (v) return v;
    e = store_find_locked(o->key);
    return e ? e->value : o->def;
}
/* the snapshot's values are in the file: the store's copies that still match lose the flag; one changed stays dirty */
static void store_clear_written(const Entry* snap, int n) {
    int i;
    pm_lock(&g_store_lock);
    for (i = 0; i < n; i++) {
        Entry* e = store_find_locked(snap[i].key);
        if (e && !strcmp(e->value, snap[i].value)) e->dirty = 0;
    }
    pm_unlock(&g_store_lock);
}
/* 1 when an in-game change waits for the file */
static int store_has_dirty(void) {
    int i, found = 0;
    pm_lock(&g_store_lock);
    for (i = 0; i < g_nstore && !found; i++) found = g_store[i].dirty;
    pm_unlock(&g_store_lock);
    return found;
}

void ini_override(const char* k, const char* v) {
    size_t n = strlen(k);
    int i;
    if (n >= KEY_MAX) return;
    for (i = 0; i < g_nover && strcmp(g_over[i].key, k); i++) continue;
    if (i == OVERRIDE_MAX) {
        WARN_LIMIT(1, "settings: more than %d settings on the command line, --%s and those after it do nothing\n",
                   OVERRIDE_MAX, k);
        return;
    }
    if (i == g_nover) memcpy(g_over[g_nover++].key, k, n + 1);
    g_over[i].value = v;
}

void ini_set(const char* k, const char* v) {
    size_t kn = strlen(k), vn = strlen(v);
    StorePutResult why;
    Entry* e;
    if (override_of(k)) return; /* this run's value, not the file's */
    if (kn >= KEY_MAX || vn >= CHANGE_VALUE_MAX) {
        WARN_LIMIT(1, "settings: %s's value is too long to save\n", k);
        return;
    }
    pm_lock(&g_store_lock);
    why = store_put_locked(k, v);
    if (why == STORE_PUT_DONE) {
        e = store_find_locked(k);
        if (e) e->dirty = 1;
    }
    pm_unlock(&g_store_lock);
    /* the length checks above leave a full store as the only refusal */
    if (why != STORE_PUT_DONE) WARN_LIMIT(1, "settings: the store is full, %s is not saved\n", k);
}

/* ---- applying ---- */

static void apply(const char* key, const char* value) {
    int own = 0;
    const IniOpt* o;
    if (ini_deprecated(key)) return; /* kept in the file, never read */
    o = find_row(key, &own);
    if (o && g_started && (o->flags & INI_STARTUP)) return;
    if (o && own)
        ini_read(o, value);
    else if (o || unattended_known(key))
        backend_ini(key, value);
}
int options_overridden(const char* key) { return override_of(key) != NULL; }

/* The startup settings that a window changed in this run: saved to the file, but the running game keeps its value. The
   window shows the saved one and that the game must start again. A leaf lock, as g_store_lock's */
enum { RESTART_MAX = 32 };
static SettingText g_restart[RESTART_MAX];
static int g_nrestart;
static PMutex g_restart_lock = PMUTEX_INIT;

static void restart_note(const char* key, const char* value) {
    pm_lock(&g_restart_lock);
    int i = 0;
    while (i < g_nrestart && strcmp(g_restart[i].key, key)) i++;
    if (i < RESTART_MAX && strlen(key) < sizeof g_restart[i].key && strlen(value) < sizeof g_restart[i].value) {
        if (i == g_nrestart) g_nrestart++;
        snprintf(g_restart[i].key, sizeof g_restart[i].key, "%s", key);
        snprintf(g_restart[i].value, sizeof g_restart[i].value, "%s", value);
    }
    pm_unlock(&g_restart_lock);
}

int options_restart_value(const char* key, char* out, size_t n) {
    int found = 0;
    pm_lock(&g_restart_lock);
    for (int i = 0; i < g_nrestart && !found; i++)
        if (!strcmp(g_restart[i].key, key)) {
            snprintf(out, n, "%s", g_restart[i].value);
            found = 1;
        }
    pm_unlock(&g_restart_lock);
    return found;
}

int options_change(const char* key, const char* value) {
    const IniOpt* o = find_row(key, NULL);
    char why[128];
    if (!o) return OPTIONS_UNKNOWN;
    if (override_of(key)) return OPTIONS_OVERRIDDEN;
    if (ini_check(o, value, why, sizeof why)) {
        WARN_LIMIT(1, "settings: %s=%s is not taken: %s\n", key, value, why);
        return OPTIONS_REJECTED;
    }
    apply(key, value);
    if (g_started && (o->flags & INI_STARTUP)) restart_note(key, value);
    ini_set(key, value);
    return OPTIONS_OK;
}

/* the row's value copied out of the store or the command line, which the caller frees; NULL for none, or out of
   memory */
static char* row_value_copy(const IniOpt* o) {
    const char* v;
    char* copy;
    size_t n;
    pm_lock(&g_store_lock);
    v = value_of_locked(o);
    n = v ? strlen(v) + 1 : 0;
    copy = n ? malloc(n) : NULL;
    if (copy) memcpy(copy, v, n);
    pm_unlock(&g_store_lock);
    if (n && !copy) WARN_LIMIT(1, "settings: out of memory, %s is not applied\n", o->key);
    return copy;
}
/* every setting: the command line's value, else the file's, else its default; then the keys of no section the
   unattended options take */
static void apply_all(void) {
    int s, t, i;
    for (s = 0; s < g_ini_sections_count; s++)
        for (t = 0; t < 3 && g_ini_sections[s].table[t]; t++) {
            const IniOpt* o;
            for (o = g_ini_sections[s].table[t]; o->key; o++) {
                char* v = row_value_copy(o);
                if (v) {
                    apply(o->key, v);
                    free(v);
                }
            }
        }
    for (i = 0;; i++) {
        char key[KEY_MAX], value[VALUE_MAX];
        int have;
        pm_lock(&g_store_lock);
        have = i < g_nstore;
        if (have) {
            memcpy(key, g_store[i].key, sizeof key);
            memcpy(value, g_store[i].value, sizeof value);
        }
        pm_unlock(&g_store_lock);
        if (!have) break;
        if (!find_row(key, NULL) && !override_of(key)) apply(key, value);
    }
    for (i = 0; i < g_nover; i++)
        if (!find_row(g_over[i].key, NULL)) apply(g_over[i].key, g_over[i].value);
}

/* ---- writing ---- */

static void put_help(FILE* f, const char* help) {
    const char* nl;
    fputs("; ", f);
    while ((nl = strchr(help, '\n')) != NULL) {
        fprintf(f, "%.*s\n;   ", (int)(nl - help), help);
        help = nl + 1;
    }
    fprintf(f, "%s\n", help);
}
static void put_header(FILE* f) {
    const char* title = g_mod && g_mod->title && g_mod->title[0] ? g_mod->title : g_exename;
    int s, t, col = 0;
    fprintf(f, "; ==================================================================================\n");
    fprintf(f, ";  %s: ScarletNX settings\n", title);
    fprintf(f, ";  Most settings apply while the game runs. These apply when it next starts:\n;   ");
    for (s = 0; s < g_ini_sections_count; s++)
        for (t = 0; t < 3 && g_ini_sections[s].table[t]; t++) {
            const IniOpt* o;
            for (o = g_ini_sections[s].table[t]; o->key; o++) {
                if (!(o->flags & INI_STARTUP)) continue;
                if (col && col + (int)strlen(o->key) + 2 > 70) {
                    fprintf(f, ",\n;   ");
                    col = 0;
                } else if (col) {
                    fprintf(f, ", ");
                    col += 2;
                }
                fputs(o->key, f);
                col += (int)strlen(o->key);
            }
        }
    fprintf(f, "\n;  In the game: F1 help, F2 the game's menu, F3 screenshot, F4 FPS limit off and on, F5 thread\n");
    fprintf(f, ";  dump, F6 showcase on and off, F7 save state, F8 load state, F9 next save state slot");
#if defined(SNX_TRACY)
    fprintf(f, ", F10 a 60-second Tracy capture");
#endif
    fprintf(f, ", F11 or\n;  Alt+Enter fullscreen\n");
    fprintf(f, "; ==================================================================================\n");
}
/* the key's entry in a snapshot of the store, without the lock: a snapshot does not change */
static const Entry* snapshot_find(const Entry* snap, int n, const char* key) {
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(snap[i].key, key)) return &snap[i];
    return NULL;
}
static void put_rows(FILE* f, const IniOpt* o, const Entry* snap, int n) {
    for (; o->key; o++) {
        const Entry* e = snapshot_find(snap, n, o->key);
        const char* v = e ? e->value : o->def;
        if (!v) continue; /* no default and never set */
        if (o->help) put_help(f, o->help);
        fprintf(f, "%s=%s\n", o->key, v);
    }
}
/* 1 when put_rows writes a row of the section: one with a default, or set */
static int section_set(const IniSection* section, const Entry* snap, int n) {
    int t;
    const IniOpt* o;
    for (t = 0; t < 3 && section->table[t]; t++)
        for (o = section->table[t]; o->key; o++)
            if (o->def || snapshot_find(snap, n, o->key)) return 1;
    return 0;
}
/* the store into the file: beside it, then moved over it, so the file is always whole. 0, -1 (warned once), or 1
   with nothing written because the last parse of the file was incomplete */
static int write_file(void) {
    char tmp[sizeof g_path + 8];
    FILE* f;
    int s, t, i, n, other = 0, failed;
    unsigned char* text;
    size_t size;
    const Entry* snap = g_work;
    if (g_write_blocked) return 1;
    pm_lock(&g_store_lock);
    memcpy(g_work, g_store, (size_t)g_nstore * sizeof *g_store);
    n = g_nstore;
    pm_unlock(&g_store_lock);
    snprintf(tmp, sizeof tmp, "%s.tmp", g_path);
    f = fopen(tmp, "wb");
    if (!f) {
        snx_os_error(g_write_why, sizeof g_write_why);
        g_write_stage = "write";
        WARN_LIMIT(1, "settings: could not write %s: %s\n", tmp, g_write_why);
        return -1;
    }
    put_header(f);
    for (s = 0; s < g_ini_sections_count; s++) {
        if (!section_set(&g_ini_sections[s], snap, n)) continue; /* [Game] of a game made from no dump */
        fprintf(f, "\n[%s]\n", g_ini_sections[s].name);
        for (t = 0; t < 3 && g_ini_sections[s].table[t]; t++) {
            if (g_ini_sections[s].page[t]) fprintf(f, "; -- %s --\n", g_ini_sections[s].page[t]);
            put_rows(f, g_ini_sections[s].table[t], snap, n);
        }
    }
    for (i = 0; i < n; i++) /* a newer version's keys, typos, the unattended options: as they are */
        if (!find_row(snap[i].key, NULL) && !ini_deprecated(snap[i].key)) {
            if (!other++) fprintf(f, "\n[Other]\n; keys this version does not know, kept as they are\n");
            fprintf(f, "%s=%s\n", snap[i].key, snap[i].value);
        }
    for (i = 0, other = 0; i < n; i++) /* last, so the launcher's Clean Old Settings cuts to the end */
        if (ini_deprecated(snap[i].key)) {
            if (!other++) ini_deprecated_head(f);
            ini_deprecated_key(f, snap[i].key, snap[i].value);
        }
    failed = ferror(f);
    if (fclose(f)) failed = 1;
    if (failed || plat_replace_file(tmp, g_path)) {
        /* the stream's own failure is the write's; the move's is the replace's */
        g_write_stage = failed ? "write" : "replace";
        snx_os_error(g_write_why, sizeof g_write_why); /* before remove's */
        remove(tmp);
        WARN_LIMIT(1, "settings: could not write %s (%s): this change is not saved\n", g_path, g_write_why);
        return -1;
    }
    text = snx_read_file(g_path, &size);
    g_hash = g_seen = text ? fnv1a(text, size) : 0;
    free(text);
    store_clear_written(snap, n);
    return 0;
}

/* ini_read's line about a value clamped or left, in the log */
static void say_setting(const char* line) { WARN("%s\n", line); }

static void say_deprecated(void) {
    char text[STORE_MAX][KEY_MAX];
    const char* keys[STORE_MAX];
    char line[2048];
    int i, count, n = 0;
    pm_lock(&g_store_lock);
    count = g_nstore;
    for (i = 0; i < count; i++) memcpy(text[i], g_store[i].key, KEY_MAX);
    pm_unlock(&g_store_lock);
    for (i = 0; i < count; i++)
        if (ini_deprecated(text[i])) keys[n++] = text[i];
    if (ini_deprecated_note(line, sizeof line, g_path, keys, n)) WARN("%s\n", line);
}

int options_debug(int flags, char* out, size_t n) {
    int count = ini_changed(g_debug_ini, flags, out, n);
    size_t at = strlen(out);
    if (g_opt_gdb && at < n) snprintf(out + at, n - at, "%sgdbstub", count++ ? ", " : "");
    return count;
}
/* the Debug settings in effect at the top of the picture; after a reload that changes them, in the log too */
static void debug_shown(int reloaded) {
    char list[HUD_DEBUG_MAX];
    options_debug(INI_CHANGED_NO_TEXT, list, sizeof list);
    if (!hud_debug_set(list) || !reloaded) return;
    if (options_debug(INI_CHANGED_DEFAULT, list, sizeof list))
        LOG("debug: after the reload, %s\n", list);
    else
        LOG("debug: after the reload, every Debug setting is at its default\n");
}

/* Records whether the last parse kept every key. The change into the blocked state warns once, and the change out of
   it says that writes start again; a blocked write itself makes no line */
static void set_write_blocked(int blocked) {
    if (blocked == g_write_blocked) return;
    g_write_blocked = blocked;
    if (blocked)
        WARN("settings: %s has keys that the store cannot keep; the file is not written in this run, so changes made "
             "in the game are not saved\n", g_path);
    else
        LOG("settings: %s was reloaded whole; the file is written again\n", g_path);
}

void ini_open(const char* path) {
    unsigned char* text;
    size_t size;
    int i;
    char why[256];
    snprintf(g_path, sizeof g_path, "%s", path);
    ini_set_say(say_setting);
    for (i = 0; i < g_nover; i++)
        if (!snx_setting_known(g_over[i].key)) WARN("--%s is not a setting, so it does nothing\n", g_over[i].key);
    text = snx_read_file(g_path, &size);
    if (!text) snx_os_error(why, sizeof why);
    if (!text && !snx_path_missing(g_path))
        fatal("The settings file %s exists but cannot be read (%s)", g_path, why);
    if (text) {
        set_write_blocked(store_reparse((char*)text) != 0);
        g_hash = g_seen = fnv1a(text, size);
        free(text);
    }
    say_deprecated();
    apply_all();
    debug_shown(0);
    write_file();
    g_started = 1;
}

const char* ini_file(void) { return g_path; }

/* ---- the CLI's settings commands (settings_cli.c): the store and the writer above, with no window and no poll ---- */

const IniOpt* snx_setting_row(const char* key) { return find_row(key, NULL); }
const char* snx_settings_value(const IniOpt* o) { return value_of(o); }

int snx_settings_load(const char* path, char* why, size_t n) {
    unsigned char* text;
    size_t size;
    int failed;
    snprintf(g_path, sizeof g_path, "%s", path);
    text = snx_read_file(g_path, &size);
    if (!text) {
        if (snx_path_missing(g_path)) return 0;
        snx_os_error(why, n);
        return -1;
    }
    failed = store_parse((char*)text);
    free(text);
    if (failed) {
        snprintf(why, n, "a key or a value that the store cannot keep (at most %d keys, %d-byte keys, %d-byte values)",
                 STORE_MAX, KEY_MAX - 1, VALUE_MAX - 1);
        return -2;
    }
    return 1;
}

/* 1 for a value that the .ini's reader does not give back as written: it splits the file at a '\n' or a '\r' and trims
   the blanks and tabs of both ends (ini_lines.c), so a line break would become a key and the blanks would be lost.
   Settings set, through snx_settings_save, and settings check both call it */
int snx_settings_value_unreadable(const char* key, const char* value, char* out, size_t n) {
    size_t size = strlen(value);
    if (strpbrk(value, "\r\n") == NULL &&
        !(size && (value[0] == ' ' || value[0] == '\t' || value[size - 1] == ' ' || value[size - 1] == '\t')))
        return 0;
    snprintf(out, n, "%s: a line break or a blank at an end is not kept by the .ini file", key);
    return 1;
}

int snx_settings_save(const char* key, const char* value, char* out, size_t n) {
    if (snx_settings_value_unreadable(key, value, out, n)) return 1;
    if (strlen(key) >= KEY_MAX || strlen(value) >= VALUE_MAX) {
        snprintf(out, n, "%s's value is longer than the %d bytes the file keeps", key, VALUE_MAX - 1);
        return 1;
    }
    if (store_put(key, value)) {
        snprintf(out, n, "%s: the file keeps at most %d keys, so it is not written", key, STORE_MAX);
        return -1;
    }
    g_write_stage = NULL;
    if (!write_file()) return 0;
    snprintf(out, n, "%s %s: %s", g_write_stage ? g_write_stage : "write", g_path, g_write_why);
    return -1;
}

void ini_poll(int final) {
    unsigned char* text;
    size_t size;
    int dirty;
    u64 h;
    if (!g_path[0]) return;
    pm_lock(&g_poll_lock);
    dirty = store_has_dirty();
    text = snx_read_file(g_path, &size);
    if (!text) {
        /* unreadable: left alone. Gone: made again when there is something to save */
        if (!snx_path_exists(g_path) && dirty) write_file();
        pm_unlock(&g_poll_lock);
        return;
    }
    h = fnv1a(text, size);
    if (h != g_hash && (final ? dirty : h == g_seen)) {
        /* changed, and settled (seen the same a second before), or the game's changes are to be saved at exit */
        set_write_blocked(store_reparse((char*)text) != 0);
        apply_all();
        LOG("settings: %s reloaded\n", g_path);
        debug_shown(1);
        write_file();
    } else if (h != g_hash)
        g_seen = h; /* changed: maybe still being saved, read at the next poll if it stays so */
    else if (dirty)
        write_file();
    free(text);
    pm_unlock(&g_poll_lock);
}

/* ---- the listing for the launcher ---- */

void ini_list(const IniSection* sections, int count) {
    size_t n = ini_list_text(sections, count, NULL, 0);
    char* text = malloc(n + 1);
    if (!text) return;
    ini_list_text(sections, count, text, n + 1);
    fputs(text, stdout);
    free(text);
}
