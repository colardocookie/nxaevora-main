/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_DISCORD_PRESENCE_H
#define SNX_DISCORD_PRESENCE_H
/* Discord presence for Legends: Arceus (DISCORD-DESIGN.md, D1). The pure parts, tested on any host: the shared
   state and its sequence number, the region table, the activity's text, the JSON writer, the IPC frame and the
   rule of when to send. The game half (arceus_clock.c) writes the state, the host half (discord_presence.c) reads
   it on a thread of its own and never touches game memory. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* DRP_TITLE: the session's start, until a save is loaded */
enum { DRP_FIELD, DRP_WILD, DRP_TRAINER, DRP_BATTLE, DRP_TITLE };

/* Plain values only. seq is odd while the game half writes. */
typedef struct {
    volatile uint32_t seq;
    int32_t region; /* an index into snx_drp_regions, -1 for none known */
    uint32_t activity;
    uint32_t party; /* the party's size, 0 when not read */
} SnxDrpState;
typedef struct {
    int32_t region;
    uint32_t activity, party;
} SnxDrpValues;

static inline void snx_drp_write(SnxDrpState* s, const SnxDrpValues* v) {
    __atomic_store_n(&s->seq, s->seq + 1, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&s->region, v->region, __ATOMIC_RELAXED);
    __atomic_store_n(&s->activity, v->activity, __ATOMIC_RELAXED);
    __atomic_store_n(&s->party, v->party, __ATOMIC_RELAXED);
    __atomic_store_n(&s->seq, s->seq + 1, __ATOMIC_RELEASE);
}
/* 1 with a consistent copy: the same even number before and after. 0 after tries attempts that each saw a write */
static inline int snx_drp_read(const SnxDrpState* s, SnxDrpValues* out, int tries) {
    for (int i = 0; i < tries; i++) {
        const uint32_t before = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
        if (before & 1) continue;
        out->region = __atomic_load_n(&s->region, __ATOMIC_RELAXED);
        out->activity = __atomic_load_n(&s->activity, __ATOMIC_RELAXED);
        out->party = __atomic_load_n(&s->party, __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&s->seq, __ATOMIC_RELAXED) == before) return 1;
    }
    return 0;
}

/* The game's area names, our own text. confirmed: seen in a run as that place (P1b for the Fieldlands), else the
   presence says "Exploring Hisui" until a run confirms it. village: "Visiting" instead of "Exploring". */
typedef struct {
    const char* name;
    const char* text;
    int confirmed, village;
} SnxDrpRegion;
static const SnxDrpRegion snx_drp_regions[] = {
    {"ha_area00", "Jubilife Village", 0, 1},     {"ha_area01", "Obsidian Fieldlands", 1, 0},
    {"ha_area02", "Crimson Mirelands", 0, 0},    {"ha_area03", "Cobalt Coastlands", 0, 0},
    {"ha_area04", "Coronet Highlands", 0, 0},    {"ha_area05", "Alabaster Icelands", 0, 0},
};
#define SNX_DRP_REGIONS ((int)(sizeof snx_drp_regions / sizeof snx_drp_regions[0]))
/* the row for an area name, exactly or with a sub-area suffix (_s...), -1 for any other name */
static inline int snx_drp_region(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < SNX_DRP_REGIONS; i++) {
        const size_t n = strlen(snx_drp_regions[i].name);
        if (!strncmp(name, snx_drp_regions[i].name, n) && (!name[n] || (name[n] == '_' && name[n + 1] == 's')))
            return i;
    }
    return -1;
}

/* The activity's first line, UTF-8 */
static inline const char* snx_drp_details(const SnxDrpValues* v, char* out, size_t size) {
    static const char* const battles[] = {"Battling a wild Pok\xc3\xa9mon", "Battling a trainer", "In battle"};
    if (v->activity >= DRP_WILD && v->activity <= DRP_BATTLE) return battles[v->activity - DRP_WILD];
    if (v->activity == DRP_TITLE) return "On the title screen";
    const int known = v->region >= 0 && v->region < SNX_DRP_REGIONS && snx_drp_regions[v->region].confirmed;
    const SnxDrpRegion* r = known ? &snx_drp_regions[v->region] : NULL;
    if (!r) return "Exploring Hisui";
    snprintf(out, size, "%s %s", r->village ? "Visiting" : "Exploring", r->text);
    return out;
}

/* The activity's second line: the party's size, 1 to 6, else none (NULL) */
static inline const char* snx_drp_state_line(const SnxDrpValues* v, char* out, size_t size) {
    if (v->party < 1 || v->party > 6) return NULL;
    snprintf(out, size, "Party: %u Pok\xc3\xa9mon", v->party);
    return out;
}

/* A battle is over when its effect sequences have not run for 3 s (DRP-P3, research 09:00). since: the battle's
   start or the counter's last move, whichever is later. */
#define SNX_DRP_BATTLE_QUIET_NS 3000000000ull
static inline int snx_drp_in_battle(uint32_t activity) { return activity >= DRP_WILD && activity <= DRP_BATTLE; }
static inline int snx_drp_battle_over(int in_battle, uint64_t now, uint64_t since) {
    return in_battle && now >= since && now - since >= SNX_DRP_BATTLE_QUIET_NS;
}

/* The application id: 1 to 20 decimal digits */
static inline int snx_drp_app_ok(const char* id) {
    if (!id || !id[0] || strlen(id) > 20) return 0;
    for (const char* p = id; *p; p++)
        if (*p < '0' || *p > '9') return 0;
    return 1;
}

/* A JSON writer into a fixed buffer. Overflow marks it failed and nothing past the end is written. */
typedef struct {
    char* at;
    size_t size, used;
    int failed;
} SnxJson;
static inline void snx_json_raw(SnxJson* j, const char* text, size_t n) {
    if (j->failed || j->used + n + 1 > j->size) {
        j->failed = 1;
        return;
    }
    memcpy(j->at + j->used, text, n);
    j->used += n;
    j->at[j->used] = 0;
}
static inline void snx_json_text(SnxJson* j, const char* text) { snx_json_raw(j, text, strlen(text)); }
/* how many bytes of text to keep for at most max bytes, never cutting a UTF-8 sequence */
static inline size_t snx_utf8_cut(const char* text, size_t max) {
    size_t n = strlen(text);
    if (n <= max) return n;
    n = max;
    while (n > 0 && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
    return n;
}
/* a JSON string, quoted and escaped, cut to max bytes of text first (Discord's 128) */
static inline void snx_json_string(SnxJson* j, const char* text, size_t max) {
    static const char hex[] = "0123456789abcdef";
    const size_t n = snx_utf8_cut(text, max);
    snx_json_raw(j, "\"", 1);
    for (size_t i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') {
            const char e[2] = {'\\', (char)c};
            snx_json_raw(j, e, 2);
        } else if (c < 0x20) {
            const char e[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
            snx_json_raw(j, e, 6);
        } else snx_json_raw(j, (const char*)&text[i], 1);
    }
    snx_json_raw(j, "\"", 1);
}

/* The SET_ACTIVITY command for this state, NUL-terminated in out. 0 when it did not fit. */
static inline int snx_drp_activity(char* out, size_t size, const SnxDrpValues* v, unsigned long pid, long long start,
                                   unsigned nonce) {
    char line[160], party[48], number[64];
    SnxJson j = {out, size, 0, 0};
    snx_json_text(&j, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":");
    snprintf(number, sizeof number, "%lu", pid);
    snx_json_text(&j, number);
    snx_json_text(&j, ",\"activity\":{\"details\":");
    snx_json_string(&j, snx_drp_details(v, line, sizeof line), 128);
    const char* state = snx_drp_state_line(v, party, sizeof party);
    if (state) {
        snx_json_text(&j, ",\"state\":");
        snx_json_string(&j, state, 128);
    }
    snprintf(number, sizeof number, ",\"timestamps\":{\"start\":%lld}", start);
    snx_json_text(&j, number);
    snx_json_text(&j, ",\"assets\":{\"large_image\":\"nxaevora\",\"large_text\":");
    snx_json_string(&j, "NXAevora (unofficial)", 128);
    snprintf(number, sizeof number, "}}},\"nonce\":\"%u\"}", nonce);
    snx_json_text(&j, number);
    return !j.failed;
}
static inline int snx_drp_handshake(char* out, size_t size, const char* app) {
    SnxJson j = {out, size, 0, 0};
    snx_json_text(&j, "{\"v\":1,\"client_id\":");
    snx_json_string(&j, app, 32);
    snx_json_text(&j, "}");
    return !j.failed;
}

/* An IPC frame: the opcode and the payload's length, each little-endian u32, then the payload. The frame's size. */
static inline size_t snx_drp_frame(unsigned char* out, size_t size, uint32_t opcode, const char* payload) {
    const size_t n = strlen(payload);
    if (n + 8 > size) return 0;
    for (int i = 0; i < 4; i++) {
        out[i] = (unsigned char)(opcode >> (8 * i));
        out[4 + i] = (unsigned char)((uint32_t)n >> (8 * i));
    }
    memcpy(out + 8, payload, n);
    return n + 8;
}

/* When to send: only a text that differs from the last one sent, and only 4 s or more after the last send. A change
   inside the 4 s waits for their end, and the newest text is the one then sent. */
#define SNX_DRP_GAP_NS 4000000000ull
static inline int snx_drp_due(const char* text, const char* sent, uint64_t now, uint64_t last, int ever) {
    if (ever && !strcmp(text, sent)) return 0;
    return !ever || now - last >= SNX_DRP_GAP_NS;
}
/* The session over a pipe that the caller supplies (Discord's on the host, a stand-in in the tests). Each call of
   snx_drp_step is one wake of the host thread. */
typedef struct {
    int (*open)(void* user, int index);                      /* 1 when pipe index (0 to 9) was opened */
    int (*write)(void* user, const void* data, size_t size); /* 1 when all of it was written */
    int (*read)(void* user, void* data, size_t size);        /* 1 when exactly that much was read */
    void (*close)(void* user);
    void* user;
} SnxDrpPipe;
#define SNX_DRP_RETRY_NS 30000000000ull
typedef struct {
    const SnxDrpPipe* pipe;
    const char* app;
    unsigned long pid;
    long long start;
    int connected, ever;
    uint64_t last_send, last_try;
    unsigned nonce, sends, connects;
    char sent[200]; /* the two lines last sent, "details / state" */
} SnxDrpSession;
/* one reply frame read and dropped, so the pipe never fills. 0 when the pipe failed or Discord closed it */
static inline int snx_drp_reply(SnxDrpSession* d) {
    unsigned char head[8], skip[512];
    if (!d->pipe->read(d->pipe->user, head, 8)) return 0;
    const uint32_t opcode = head[0] | head[1] << 8 | head[2] << 16 | (uint32_t)head[3] << 24;
    uint32_t n = head[4] | head[5] << 8 | head[6] << 16 | (uint32_t)head[7] << 24;
    if (n > 65536) return 0;
    while (n) {
        const uint32_t part = n < sizeof skip ? n : (uint32_t)sizeof skip;
        if (!d->pipe->read(d->pipe->user, skip, part)) return 0;
        n -= part;
    }
    return opcode != 2;
}
static inline void snx_drp_drop(SnxDrpSession* d) {
    if (d->connected) d->pipe->close(d->pipe->user);
    d->connected = 0;
    d->ever = 0; /* a new pipe gets the activity again */
}
static inline int snx_drp_send(SnxDrpSession* d, uint32_t opcode, const char* json) {
    unsigned char frame[1200];
    const size_t n = snx_drp_frame(frame, sizeof frame, opcode, json);
    if (!n || !d->pipe->write(d->pipe->user, frame, n) || !snx_drp_reply(d)) {
        snx_drp_drop(d);
        return 0;
    }
    return 1;
}
/* One wake: connect when due (the pipes 0 to 9, then every 30 s), then send the activity when snx_drp_due says so */
static inline void snx_drp_step(SnxDrpSession* d, const SnxDrpValues* v, uint64_t now) {
    char json[1100], line[160];
    if (!d->connected) {
        if (d->connects && now - d->last_try < SNX_DRP_RETRY_NS) return;
        d->last_try = now;
        d->connects++;
        for (int i = 0; i < 10 && !d->connected; i++) d->connected = d->pipe->open(d->pipe->user, i);
        if (!d->connected || !snx_drp_handshake(json, sizeof json, d->app) || !snx_drp_send(d, 0, json)) {
            snx_drp_drop(d);
            return;
        }
    }
    char party[48], text[200];
    const char* state = snx_drp_state_line(v, party, sizeof party);
    snprintf(text, sizeof text, "%s / %s", snx_drp_details(v, line, sizeof line), state ? state : "-");
    if (!snx_drp_due(text, d->sent, now, d->last_send, d->ever)) return;
    if (!snx_drp_activity(json, sizeof json, v, d->pid, d->start, ++d->nonce) || !snx_drp_send(d, 1, json)) return;
    snprintf(d->sent, sizeof d->sent, "%s", text);
    d->ever = 1;
    d->last_send = now;
    d->sends++;
}

/* discord_presence.c: the state the game half writes, and the host thread's start */
extern SnxDrpState g_drp_state;
int snx_discord_start(const char* app);
#endif
