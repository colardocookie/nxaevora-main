/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_DISCORD_PRESENCE_H
#define SNX_DISCORD_PRESENCE_H
/* Discord presence for Legends: Arceus (DISCORD-DESIGN.md, D1 and D2). The pure parts, tested on any host: the
   shared state and its sequence number, the region from a pack's name, the activity's text, a record's decipher,
   the JSON writer, the IPC frame and the rule of when to send. The game half (arceus_clock.c) writes the state,
   the host half (discord_presence.c) reads it on a thread of its own and never touches game memory. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "nx/runtime/discord_species.h"

/* DRP_TITLE: the session's start, until the player's camera first moves in the field */
enum { DRP_FIELD, DRP_BATTLE, DRP_TITLE };

/* A Pokémon as the presence shows it: species 1 to 905, 0 for none, level 1 to 100, 0 when not known */
typedef struct {
    uint16_t species;
    uint8_t level, alpha;
} SnxDrpMon;
/* What the presence shows. The game half writes all but the region, which the host half takes from the file
   service (g_drp_region). foe: a wild opponent that checked out. active: the player's Pokémon in a battle. */
typedef struct {
    int32_t region;
    uint32_t activity;
    SnxDrpMon foe, active, party[6];
} SnxDrpValues;
#define SNX_DRP_WORDS 9
/* Plain words only. seq is odd while the game half writes. */
typedef struct {
    volatile uint32_t seq;
    uint32_t words[SNX_DRP_WORDS];
} SnxDrpState;
static inline uint32_t snx_drp_mon_word(SnxDrpMon m) {
    return m.species | (uint32_t)m.level << 16 | (uint32_t)(m.alpha ? 1 : 0) << 24;
}
static inline SnxDrpMon snx_drp_word_mon(uint32_t w) {
    const SnxDrpMon m = {(uint16_t)w, (uint8_t)(w >> 16), (uint8_t)(w >> 24)};
    return m;
}

static inline void snx_drp_write(SnxDrpState* s, const SnxDrpValues* v) {
    uint32_t w[SNX_DRP_WORDS] = {v->activity, snx_drp_mon_word(v->foe), snx_drp_mon_word(v->active)};
    for (int i = 0; i < 6; i++) w[3 + i] = snx_drp_mon_word(v->party[i]);
    __atomic_store_n(&s->seq, s->seq + 1, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    for (int i = 0; i < SNX_DRP_WORDS; i++) __atomic_store_n(&s->words[i], w[i], __ATOMIC_RELAXED);
    __atomic_store_n(&s->seq, s->seq + 1, __ATOMIC_RELEASE);
}
/* 1 with a consistent copy: the same even number before and after. 0 after tries attempts that each saw a write.
   The region is left as it was. */
static inline int snx_drp_read(const SnxDrpState* s, SnxDrpValues* out, int tries) {
    for (int i = 0; i < tries; i++) {
        const uint32_t before = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
        if (before & 1) continue;
        uint32_t w[SNX_DRP_WORDS];
        for (int k = 0; k < SNX_DRP_WORDS; k++) w[k] = __atomic_load_n(&s->words[k], __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&s->seq, __ATOMIC_RELAXED) != before) continue;
        out->activity = w[0];
        out->foe = snx_drp_word_mon(w[1]);
        out->active = snx_drp_word_mon(w[2]);
        for (int k = 0; k < 6; k++) out->party[k] = snx_drp_word_mon(w[3 + k]);
        return 1;
    }
    return 0;
}

/* The region (research 18:25): the pack the game reads on entering one, ha_areaNN.gfpak, by the file service's
   leaf name. The index NN, or -1 for any other name. */
static inline int snx_drp_pack_index(const char* leaf) {
    if (!leaf || strncmp(leaf, "ha_area", 7)) return -1;
    const char a = leaf[7], b = a ? leaf[8] : 0;
    if (a < '0' || a > '9' || b < '0' || b > '9' || strcmp(leaf + 9, ".gfpak")) return -1;
    return (a - '0') * 10 + (b - '0');
}
/* fs.c's call, once per opened file the game reads, only when the presence is on (g_drp_packs) */
static inline void snx_drp_note_pack(int32_t* region, const char* leaf) {
    const int index = snx_drp_pack_index(leaf);
    if (index >= 0) __atomic_store_n(region, index, __ATOMIC_RELAXED);
}
/* Our own text for the packs. 00, 01 and 02 are confirmed by runs, 03 to 05 by the game's order until a look. */
static const char* const snx_drp_regions[] = {"Visiting Jubilife Village", "Exploring Obsidian Fieldlands",
                                              "Exploring Crimson Mirelands", "Exploring Cobalt Coastlands",
                                              "Exploring Coronet Highlands", "Exploring Alabaster Icelands"};
#define SNX_DRP_REGIONS ((int)(sizeof snx_drp_regions / sizeof snx_drp_regions[0]))

/* The activity's first line, UTF-8 */
static inline const char* snx_drp_details(const SnxDrpValues* v, char* out, size_t size) {
    if (v->activity == DRP_TITLE) return "On the title screen";
    if (v->activity == DRP_BATTLE) {
        const char* name = snx_drp_species_name(v->foe.species);
        if (!name) return "In battle";
        if (snx_drp_boss(v->foe.species)) snprintf(out, size, "Fighting %s", name);
        else snprintf(out, size, "Battling a wild %s%s", v->foe.alpha ? "alpha " : "", name);
        return out;
    }
    return v->region >= 0 && v->region < SNX_DRP_REGIONS ? snx_drp_regions[v->region] : "Exploring Hisui";
}

/* The activity's second line: the player's Pokémon in a battle, else none (NULL) */
static inline const char* snx_drp_state_line(const SnxDrpValues* v, char* out, size_t size) {
    const char* name = snx_drp_species_name(v->active.species);
    if (v->activity != DRP_BATTLE || !name) return NULL;
    if (v->active.level) snprintf(out, size, "Active: %s Lv. %u", name, v->active.level);
    else snprintf(out, size, "Active: %s", name);
    return out;
}

/* The large image's hover text: the party in order, "<species> Lv. <n>" (the level left out when not known), whole
   members only within Discord's 128 bytes. A member that did not check out is left out. None: our name. */
#define SNX_DRP_HOVER 128
static inline const char* snx_drp_hover(const SnxDrpValues* v, char* out, size_t size) {
    size_t used = 0;
    out[0] = 0;
    for (int i = 0; i < 6; i++) {
        const char* name = snx_drp_species_name(v->party[i].species);
        if (!name) continue;
        char member[48];
        const char* comma = used ? ", " : "";
        if (v->party[i].level) snprintf(member, sizeof member, "%s%s Lv. %u", comma, name, v->party[i].level);
        else snprintf(member, sizeof member, "%s%s", comma, name);
        const size_t n = strlen(member);
        if (used + n > SNX_DRP_HOVER || used + n + 1 > size) break;
        memcpy(out + used, member, n + 1);
        used += n;
    }
    return used ? out : "NXAevora (unofficial)";
}

/* D2 (research 08:35): a Pokémon's record, deciphered from a copy by our own code. 0x168 bytes: a u32 key, a u16 of
   flags (bit 2 a bad checksum), a u16 sum, then four blocks of 0x58. A 16-byte party extension (byte 0 the level).
   Both are enciphered by an LCG seeded with the key, each u16 XORed with the state's high half. */
#define SNX_PK_RECORD 0x168
#define SNX_PK_EXTENSION 16
#define SNX_PK_BLOCK 0x58
/* The 24 orders of the blocks A to D as they lie in the record, from the public description of the format: the
   order's row is (key >> 13) & 31, rows 24 to 31 are rows 0 to 7 again */
static const char snx_pk_orders[24][5] = {"ABCD", "ABDC", "ACBD", "ACDB", "ADBC", "ADCB", "BACD", "BADC",
                                          "BCAD", "BCDA", "BDAC", "BDCA", "CABD", "CADB", "CBAD", "CBDA",
                                          "CDAB", "CDBA", "DABC", "DACB", "DBAC", "DBCA", "DCAB", "DCBA"};
/* where block (0 for A) lies for this key, 0 to 3 */
static inline unsigned snx_pk_position(uint32_t key, unsigned block) {
    const char* order = snx_pk_orders[((key >> 13) & 31) % 24];
    return (unsigned)(strchr(order, 'A' + (int)block) - order);
}
/* 1 when a table of 32 rows of 4 bytes, each the positions of A to D, is ours: checked once against the game's */
static inline int snx_pk_table_same(const uint8_t* table) {
    for (uint32_t row = 0; row < 32; row++)
        for (unsigned block = 0; block < 4; block++)
            if (table[row * 4 + block] != snx_pk_position(row << 13, block)) return 0;
    return 1;
}
/* the cipher, its own inverse */
static inline void snx_pk_crypt(uint8_t* data, size_t size, uint32_t seed) {
    for (size_t i = 0; i + 1 < size; i += 2) {
        seed = seed * 0x41c64e6du + 0x6073u;
        data[i] ^= (uint8_t)(seed >> 16);
        data[i + 1] ^= (uint8_t)(seed >> 24);
    }
}
static inline unsigned snx_pk_u16(const uint8_t* p) { return p[0] | (unsigned)p[1] << 8; }
/* A copy is kept when the record's lock word was free before it and after it and the enciphered flag did not
   change. BUSY: the game holds it, skip this second. TORN: it changed under the copy, drop it. */
enum { SNX_PK_STEADY, SNX_PK_BUSY, SNX_PK_TORN };
static inline int snx_pk_copy_state(uint64_t lock_before, unsigned flag_before, uint64_t lock_after,
                                    unsigned flag_after) {
    if (lock_before) return SNX_PK_BUSY;
    return lock_after || flag_after != flag_before ? SNX_PK_TORN : SNX_PK_STEADY;
}
/* A copy deciphered in place when enciphered says so (a plain copy is not deciphered again), then accepted only
   when the flags say no bad checksum, the sum holds, the species is 1 to 905 and, with an extension, the level 1 to
   100. The alpha flag is bit 5 of block A's u16 at +0xE (IsOybn, 2B6D5B0). */
enum { SNX_PK_OK, SNX_PK_FLAGGED, SNX_PK_SUM, SNX_PK_SPECIES, SNX_PK_LEVEL };
static inline int snx_pk_decode(uint8_t* record, uint8_t* extension, int enciphered, SnxDrpMon* out) {
    const uint32_t key = record[0] | (uint32_t)record[1] << 8 | (uint32_t)record[2] << 16 | (uint32_t)record[3] << 24;
    if (enciphered) {
        snx_pk_crypt(record + 8, SNX_PK_RECORD - 8, key);
        if (extension) snx_pk_crypt(extension, SNX_PK_EXTENSION, key);
    }
    if (snx_pk_u16(record + 4) & 4) return SNX_PK_FLAGGED;
    unsigned sum = 0;
    for (size_t i = 8; i < SNX_PK_RECORD; i += 2) sum += snx_pk_u16(record + i);
    if ((sum & 0xffff) != snx_pk_u16(record + 6)) return SNX_PK_SUM;
    const uint8_t* a = record + 8 + SNX_PK_BLOCK * snx_pk_position(key, 0);
    const unsigned species = snx_pk_u16(a), level = extension ? extension[0] : 0;
    if (species < 1 || species > 905) return SNX_PK_SPECIES;
    if (extension && (level < 1 || level > 100)) return SNX_PK_LEVEL;
    out->species = (uint16_t)species;
    out->level = (uint8_t)level;
    out->alpha = (snx_pk_u16(a + 0xe) >> 5) & 1;
    return SNX_PK_OK;
}

/* A battle is over when its effect sequences have not run for 3 s (DRP-P3, research 09:00). since: the battle's
   start or the counter's last move, whichever is later. */
#define SNX_DRP_BATTLE_QUIET_NS 3000000000ull
static inline int snx_drp_in_battle(uint32_t activity) { return activity == DRP_BATTLE; }
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
    char line[160], party[64], hover[SNX_DRP_HOVER + 1], number[64];
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
    snx_json_string(&j, snx_drp_hover(v, hover, sizeof hover), SNX_DRP_HOVER);
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
    char sent[400]; /* what was last sent, "details / state / hover text" */
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
    char party[64], hover[SNX_DRP_HOVER + 1], text[400];
    const char* state = snx_drp_state_line(v, party, sizeof party);
    snprintf(text, sizeof text, "%s / %s / %s", snx_drp_details(v, line, sizeof line), state ? state : "-",
             snx_drp_hover(v, hover, sizeof hover));
    if (!snx_drp_due(text, d->sent, now, d->last_send, d->ever)) return;
    if (!snx_drp_activity(json, sizeof json, v, d->pid, d->start, ++d->nonce) || !snx_drp_send(d, 1, json)) return;
    snprintf(d->sent, sizeof d->sent, "%s", text);
    d->ever = 1;
    d->last_send = now;
    d->sends++;
}

/* discord_presence.c: the state the game half writes, the region fs.c notes when g_drp_packs is set (by the
   host thread's start), and that start */
extern SnxDrpState g_drp_state;
extern int32_t g_drp_region;
extern int g_drp_packs;
int snx_discord_start(const char* app);
#endif
