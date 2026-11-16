/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Host tests of Discord presence's pure parts (runtime/discord_presence.h, DISCORD-DESIGN.md D1 and D2): the state's
   sequence, the region from a pack's name, the text, the species list, a record's decipher and its checks, the JSON
   writer, the frame, the rate rule and the session over a stand-in pipe (reconnect on a failed or closed pipe
   included). No run of ours talks to Discord. */
#include "nx/runtime/discord_presence.h"
#include <assert.h>
#include <stdio.h>

SnxDrpState g_drp_state;
int32_t g_drp_region = -1;
int g_drp_packs;
int snx_discord_start(const char* app) { return snx_drp_app_ok(app); }

/* A stand-in pipe: records what was written, answers each frame with a reply, can fail or close on request */
typedef struct {
    int opens, open_ok, fail_write, close_next, closes;
    unsigned frames, last_opcode;
    char last[1200];
    unsigned char reply[16];
    size_t reply_at, reply_n;
} Stand;
static int st_open(void* u, int i) {
    Stand* s = u;
    (void)i;
    s->opens++;
    return s->open_ok;
}
static int st_write(void* u, const void* data, size_t n) {
    Stand* s = u;
    if (s->fail_write) return 0;
    const unsigned char* b = data;
    s->last_opcode = b[0] | b[1] << 8;
    const unsigned len = b[4] | b[5] << 8;
    assert(len + 8 == n);
    memcpy(s->last, b + 8, len);
    s->last[len] = 0;
    s->frames++;
    /* the reply: opcode 1 (or 2 to close) with a 4 byte payload */
    const unsigned char r[12] = {(unsigned char)(s->close_next ? 2 : 1), 0, 0, 0, 4, 0, 0, 0, '{', '}', ' ', ' '};
    memcpy(s->reply, r, sizeof r);
    s->reply_at = 0;
    s->reply_n = sizeof r;
    return 1;
}
static int st_read(void* u, void* data, size_t n) {
    Stand* s = u;
    if (s->reply_at + n > s->reply_n) return 0;
    memcpy(data, s->reply + s->reply_at, n);
    s->reply_at += n;
    return 1;
}
static void st_close(void* u) { ((Stand*)u)->closes++; }

static SnxDrpMon mon(unsigned species, unsigned level, unsigned alpha) {
    const SnxDrpMon m = {(uint16_t)species, (uint8_t)level, (uint8_t)alpha};
    return m;
}

static void test_sequence(void) {
    SnxDrpState s = {0, {0}};
    SnxDrpValues v, w = {.region = 3, .activity = DRP_BATTLE, .foe = {401, 12, 1}, .active = {493, 100, 0}};
    w.party[5] = mon(491, 100, 0);
    snx_drp_write(&s, &w);
    v.region = 7; /* the region is the host half's own, never in the state */
    assert(s.seq == 2 && snx_drp_read(&s, &v, 4) && v.region == 7 && v.activity == DRP_BATTLE);
    assert(v.foe.species == 401 && v.foe.level == 12 && v.foe.alpha == 1 && v.active.species == 493);
    assert(v.party[5].species == 491 && v.party[5].level == 100 && v.party[0].species == 0);
    s.seq = 3; /* a writer in the middle: no copy is taken */
    v.activity = 9;
    assert(!snx_drp_read(&s, &v, 4) && v.activity == 9);
}

static void test_packs(void) {
    assert(snx_drp_pack_index("ha_area00.gfpak") == 0 && snx_drp_pack_index("ha_area01.gfpak") == 1);
    assert(snx_drp_pack_index("ha_area05.gfpak") == 5 && snx_drp_pack_index("ha_area11.gfpak") == 11);
    assert(snx_drp_pack_index("ha_area001.gfpak") == -1 && snx_drp_pack_index("ha_area0.gfpak") == -1);
    assert(snx_drp_pack_index("ha_area01_s01.gfpak") == -1 && snx_drp_pack_index("ha_area0a.gfpak") == -1);
    assert(snx_drp_pack_index("ha_area00.bnsh") == -1 && snx_drp_pack_index("ha_area00.gfpak.bak") == -1);
    assert(snx_drp_pack_index("ha_area00.gfpa") == -1 && snx_drp_pack_index("xha_area00.gfpak") == -1);
    assert(snx_drp_pack_index("ha_terrain.bnsh") == -1 && snx_drp_pack_index("ha_terrain_zprepass.trsha") == -1);
    assert(snx_drp_pack_index("") == -1 && snx_drp_pack_index(NULL) == -1);
    /* fs.c's call: a region's pack sets it, any other name leaves it */
    int32_t region = -1;
    snx_drp_note_pack(&region, "ha_terrain.bnsh");
    assert(region == -1);
    snx_drp_note_pack(&region, "ha_area02.gfpak");
    assert(region == 2);
    snx_drp_note_pack(&region, "field_weathering_config.bin");
    assert(region == 2);
    snx_drp_note_pack(&region, "ha_area00.gfpak");
    assert(region == 0);
}

static void test_text(void) {
    char line[160];
    SnxDrpValues v = {.region = 1, .activity = DRP_FIELD};
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "Exploring Obsidian Fieldlands"));
    v.region = 0;
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "Visiting Jubilife Village"));
    v.region = 2;
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "Exploring Crimson Mirelands"));
    v.region = 5;
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "Exploring Alabaster Icelands"));
    v.region = 6; /* ha_area06 and 11: not named */
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "Exploring Hisui"));
    v.region = -1; /* no pack read yet */
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "Exploring Hisui"));
    v.activity = DRP_TITLE;
    v.region = 1;
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "On the title screen"));
    /* a battle: the opponent when its copy checked out, else "In battle" */
    v.activity = DRP_BATTLE;
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "In battle"));
    v.foe = mon(401, 5, 0);
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "Battling a wild Kricketot"));
    v.foe.alpha = 1;
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "Battling a wild alpha Kricketot"));
    v.foe = mon(493, 75, 0);
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "Fighting Arceus"));
    v.foe = mon(906, 5, 0); /* out of range: as a failed copy */
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "In battle"));
    assert(snx_drp_in_battle(DRP_BATTLE) && !snx_drp_in_battle(DRP_FIELD) && !snx_drp_in_battle(DRP_TITLE));
    /* the second line, only in a battle */
    assert(!snx_drp_state_line(&v, line, sizeof line));
    v.active = mon(157, 58, 0);
    assert(!strcmp(snx_drp_state_line(&v, line, sizeof line), "Active: Typhlosion Lv. 58"));
    v.active.level = 0;
    assert(!strcmp(snx_drp_state_line(&v, line, sizeof line), "Active: Typhlosion"));
    v.activity = DRP_FIELD;
    assert(!snx_drp_state_line(&v, line, sizeof line));
}

static void test_species(void) {
    assert(!snx_drp_species_name(0) && !snx_drp_species_name(906));
    static const struct { unsigned n; const char* name; } known[] = {
        {1, "Bulbasaur"},   {25, "Pikachu"},     {151, "Mew"},       {152, "Chikorita"}, {251, "Celebi"},
        {252, "Treecko"},   {386, "Deoxys"},     {387, "Turtwig"},   {401, "Kricketot"}, {454, "Toxicroak"},
        {493, "Arceus"},    {494, "Victini"},    {649, "Genesect"},  {650, "Chespin"},   {721, "Volcanion"},
        {722, "Rowlet"},    {809, "Melmetal"},   {810, "Grookey"},   {898, "Calyrex"},   {899, "Wyrdeer"},
        {905, "Enamorus"},  {29, "Nidoran\xe2\x99\x80"}, {32, "Nidoran\xe2\x99\x82"}, {669, "Flab\xc3\xa9" "b\xc3\xa9"},
        {83, "Farfetch'd"}, {122, "Mr. Mime"},   {772, "Type: Null"}, {865, "Sirfetch'd"}, {474, "Porygon-Z"},
    };
    for (size_t i = 0; i < sizeof known / sizeof known[0]; i++)
        assert(!strcmp(snx_drp_species_name(known[i].n), known[i].name));
    /* the bosses: Uxie to Arceus, the forces of nature and Enamorus */
    for (unsigned n = 480; n <= 493; n++) assert(snx_drp_boss(n));
    assert(snx_drp_boss(641) && snx_drp_boss(642) && snx_drp_boss(645) && snx_drp_boss(905));
    assert(!snx_drp_boss(479) && !snx_drp_boss(494) && !snx_drp_boss(643) && !snx_drp_boss(904) && !snx_drp_boss(0));
}

static void test_hover(void) {
    char out[SNX_DRP_HOVER + 1];
    SnxDrpValues v = {.region = -1, .activity = DRP_FIELD};
    assert(!strcmp(snx_drp_hover(&v, out, sizeof out), "NXAevora (unofficial)"));
    const unsigned six[6] = {493, 484, 487, 492, 488, 491};
    for (int i = 0; i < 6; i++) v.party[i] = mon(six[i], 100, 0);
    assert(!strcmp(snx_drp_hover(&v, out, sizeof out),
                   "Arceus Lv. 100, Palkia Lv. 100, Giratina Lv. 100, Shaymin Lv. 100, Cresselia Lv. 100, "
                   "Darkrai Lv. 100"));
    /* a member that failed its checks is left out, a level not known is not shown */
    v.party[1] = mon(0, 0, 0);
    v.party[2].level = 0;
    assert(!strcmp(snx_drp_hover(&v, out, sizeof out),
                   "Arceus Lv. 100, Giratina, Shaymin Lv. 100, Cresselia Lv. 100, Darkrai Lv. 100"));
    /* six of the longest names: whole members within 128 bytes */
    for (int i = 0; i < 6; i++) v.party[i] = mon(740, 100, 0); /* Crabominable, 12 letters */
    const char* h = snx_drp_hover(&v, out, sizeof out);
    assert(strlen(h) <= SNX_DRP_HOVER && strlen(h) == 6 * 20 + 5 * 2 - 22);
    assert(!strcmp(h + strlen(h) - 20, "Crabominable Lv. 100"));
    char big[300]; /* the 128 bytes, not the buffer, make the cut (six would be 130) */
    assert(strlen(snx_drp_hover(&v, big, sizeof big)) == 108);
    /* a small buffer cuts on a whole member too */
    char small[24];
    assert(!strcmp(snx_drp_hover(&v, small, sizeof small), "Crabominable Lv. 100"));
}

/* The block orders against the public description, row by row (positions of A to D) */
static void test_orders(void) {
    static const unsigned rows[][4] = {{0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2}, {1, 0, 2, 3}};
    static const unsigned which[] = {0, 1, 2, 3, 6};
    for (unsigned r = 0; r < 5; r++)
        for (unsigned b = 0; b < 4; b++) assert(snx_pk_position(which[r] << 13, b) == rows[r][b]);
    for (unsigned b = 0; b < 4; b++) {
        assert(snx_pk_position(23u << 13, b) == 3 - b); /* DCBA */
        assert(snx_pk_position(24u << 13, b) == b);      /* row 24 is row 0 again */
        assert(snx_pk_position(31u << 13, b) == snx_pk_position(7u << 13, b));
    }
    /* each row a permutation */
    for (uint32_t row = 0; row < 32; row++) {
        unsigned seen = 0;
        for (unsigned b = 0; b < 4; b++) seen |= 1u << snx_pk_position(row << 13, b);
        assert(seen == 15);
    }
    uint8_t table[128];
    for (uint32_t row = 0; row < 32; row++)
        for (unsigned b = 0; b < 4; b++) table[row * 4 + b] = (uint8_t)snx_pk_position(row << 13, b);
    assert(snx_pk_table_same(table));
    table[50] ^= 1; /* a different stand-in table turns D2 off */
    assert(!snx_pk_table_same(table));
    table[50] ^= 1;
    /* the inverse orders (where each place's block comes from) are a different table */
    uint8_t inverse[128];
    for (uint32_t row = 0; row < 32; row++)
        for (unsigned b = 0; b < 4; b++) inverse[row * 4 + snx_pk_position(row << 13, b)] = (uint8_t)b;
    assert(!snx_pk_table_same(inverse));
}

static void test_crypt(void) {
    /* the stream's first three halves for key 0x12345678, worked out apart from this code: 0b71 84ea d98a */
    uint8_t zero[6] = {0};
    snx_pk_crypt(zero, sizeof zero, 0x12345678u);
    const uint8_t want[6] = {0x71, 0x0b, 0xea, 0x84, 0x8a, 0xd9};
    assert(!memcmp(zero, want, 6));
    snx_pk_crypt(zero, sizeof zero, 0x12345678u); /* its own inverse */
    for (int i = 0; i < 6; i++) assert(zero[i] == 0);
    uint8_t one[4] = {0};
    snx_pk_crypt(one, sizeof one, 0); /* key 0: the first half is 0, the second e97e */
    assert(one[0] == 0 && one[1] == 0 && one[2] == 0x7e && one[3] == 0xe9);
}

/* A record of our own making: species, the alpha bit and the level, its sum set, then enciphered by key */
static void make_record(uint8_t* record, uint8_t* extension, uint32_t key, unsigned species, unsigned alpha,
                        unsigned level) {
    memset(record, 0, SNX_PK_RECORD);
    for (int i = 0; i < 4; i++) record[i] = (uint8_t)(key >> (8 * i));
    for (size_t i = 8; i < SNX_PK_RECORD; i++) record[i] = (uint8_t)(i * 7 + 3); /* other data */
    uint8_t* a = record + 8 + SNX_PK_BLOCK * snx_pk_position(key, 0);
    a[0] = (uint8_t)species;
    a[1] = (uint8_t)(species >> 8);
    a[0xe] = (uint8_t)((a[0xe] & ~0x20) | (alpha ? 0x20 : 0));
    unsigned sum = 0;
    for (size_t i = 8; i < SNX_PK_RECORD; i += 2) sum += snx_pk_u16(record + i);
    record[6] = (uint8_t)sum;
    record[7] = (uint8_t)(sum >> 8);
    memset(extension, 0x5a, SNX_PK_EXTENSION);
    extension[0] = (uint8_t)level;
    snx_pk_crypt(record + 8, SNX_PK_RECORD - 8, key);
    snx_pk_crypt(extension, SNX_PK_EXTENSION, key);
}

static void test_decode(void) {
    uint8_t record[SNX_PK_RECORD], extension[SNX_PK_EXTENSION], copy[SNX_PK_RECORD], more[SNX_PK_EXTENSION];
    const uint32_t key = (18u << 13) | 0x55; /* order DABC: block A at the second place */
    assert(snx_pk_position(key, 0) == 1);
    SnxDrpMon m = {0};
    make_record(record, extension, key, 493, 1, 100);
    memcpy(copy, record, sizeof copy);
    memcpy(more, extension, sizeof more);
    assert(snx_pk_decode(copy, more, 1, &m) == SNX_PK_OK && m.species == 493 && m.level == 100 && m.alpha == 1);
    /* a plain copy (the game's flag at 0) is taken as it is, and not deciphered a second time */
    m = mon(0, 0, 0);
    assert(snx_pk_decode(copy, more, 0, &m) == SNX_PK_OK && m.species == 493 && m.level == 100);
    assert(snx_pk_decode(copy, more, 1, &m) != SNX_PK_OK);
    /* without the extension: no level */
    make_record(record, extension, key, 401, 0, 7);
    memcpy(copy, record, sizeof copy);
    assert(snx_pk_decode(copy, NULL, 1, &m) == SNX_PK_OK && m.species == 401 && m.level == 0 && m.alpha == 0);
    /* a torn copy fails the sum */
    memcpy(copy, record, sizeof copy);
    copy[0x100] ^= 0x10;
    assert(snx_pk_decode(copy, NULL, 1, &m) == SNX_PK_SUM);
    /* the bad checksum flag */
    memcpy(copy, record, sizeof copy);
    copy[4] |= 4;
    assert(snx_pk_decode(copy, NULL, 1, &m) == SNX_PK_FLAGGED);
    /* out of range species and levels are dropped, with the sum right */
    make_record(record, extension, key, 0, 0, 50);
    assert(snx_pk_decode(record, extension, 1, &m) == SNX_PK_SPECIES);
    make_record(record, extension, key, 906, 0, 50);
    assert(snx_pk_decode(record, extension, 1, &m) == SNX_PK_SPECIES);
    make_record(record, extension, key, 905, 0, 0);
    assert(snx_pk_decode(record, extension, 1, &m) == SNX_PK_LEVEL);
    make_record(record, extension, key, 905, 0, 101);
    assert(snx_pk_decode(record, extension, 1, &m) == SNX_PK_LEVEL);
    make_record(record, extension, key, 905, 0, 1);
    assert(snx_pk_decode(record, extension, 1, &m) == SNX_PK_OK && m.species == 905 && m.level == 1);
    /* every order: block A found wherever the key puts it */
    for (uint32_t row = 0; row < 32; row++) {
        const uint32_t k = row << 13 | 0x1234;
        make_record(record, extension, k, 400 + row, row & 1, 1 + row);
        assert(snx_pk_decode(record, extension, 1, &m) == SNX_PK_OK && m.species == 400 + row);
        assert(m.level == 1 + row && m.alpha == (row & 1));
    }
    /* the copy rule: the lock free before and after, the flag the same */
    assert(snx_pk_copy_state(0, 1, 0, 1) == SNX_PK_STEADY && snx_pk_copy_state(0, 0, 0, 0) == SNX_PK_STEADY);
    assert(snx_pk_copy_state(0x1234, 1, 0, 1) == SNX_PK_BUSY && snx_pk_copy_state(0x1234, 1, 0x1234, 0) == SNX_PK_BUSY);
    assert(snx_pk_copy_state(0, 1, 0x1234, 1) == SNX_PK_TORN && snx_pk_copy_state(0, 1, 0, 0) == SNX_PK_TORN);
}

static void test_app(void) {
    assert(snx_drp_app_ok("1234567890123456789") && !snx_drp_app_ok(NULL) && !snx_drp_app_ok(""));
    assert(!snx_drp_app_ok("12a4") && !snx_drp_app_ok(" 123") && !snx_drp_app_ok("123456789012345678901"));
}

static void test_json(void) {
    char out[64];
    SnxJson j = {out, sizeof out, 0, 0};
    snx_json_string(&j, "a\"b\\c\n\x01", 128);
    assert(!j.failed && !strcmp(out, "\"a\\\"b\\\\c\\u000a\\u0001\""));
    /* é is two bytes: a cut at 128 never splits it */
    char longer[200];
    memset(longer, 'x', 127);
    memcpy(longer + 127, "\xc3\xa9", 3);
    assert(snx_utf8_cut(longer, 128) == 127 && snx_utf8_cut(longer, 129) == 129 && snx_utf8_cut("abc", 128) == 3);
    char small[8];
    SnxJson k = {small, sizeof small, 0, 0};
    snx_json_string(&k, "too long for it", 128);
    assert(k.failed && strlen(small) < sizeof small);
    char act[1100];
    SnxDrpValues v = {.region = 1, .activity = DRP_FIELD};
    assert(snx_drp_activity(act, sizeof act, &v, 42, 1700000000, 3));
    assert(strstr(act, "\"details\":\"Exploring Obsidian Fieldlands\"") && strstr(act, "\"pid\":42") &&
           strstr(act, "\"start\":1700000000") && strstr(act, "\"large_image\":\"nxaevora\"") &&
           strstr(act, "\"large_text\":\"NXAevora (unofficial)\"") && strstr(act, "\"nonce\":\"3\"") &&
           !strstr(act, "\"state\""));
    assert(snx_drp_handshake(act, sizeof act, "123") && !strcmp(act, "{\"v\":1,\"client_id\":\"123\"}"));
    /* a battle: the second line, and the party as the hover text */
    v.activity = DRP_BATTLE;
    v.foe = mon(401, 3, 1);
    v.active = mon(493, 100, 0);
    v.party[0] = mon(493, 100, 0);
    v.party[1] = mon(29, 4, 0);
    assert(snx_drp_activity(act, sizeof act, &v, 42, 1700000000, 4));
    assert(strstr(act, "\"details\":\"Battling a wild alpha Kricketot\"") &&
           strstr(act, "\"state\":\"Active: Arceus Lv. 100\"") &&
           strstr(act, "\"large_text\":\"Arceus Lv. 100, Nidoran\xe2\x99\x80 Lv. 4\""));
}

static void test_frame(void) {
    unsigned char f[32];
    assert(snx_drp_frame(f, sizeof f, 1, "{}") == 10 && f[0] == 1 && f[4] == 2 && f[8] == '{' && f[9] == '}');
    assert(!snx_drp_frame(f, 9, 1, "{}"));
}

static void test_due(void) {
    const uint64_t s = 1000000000ull;
    assert(snx_drp_due("a", "", 0, 0, 0));
    assert(!snx_drp_due("a", "a", 100 * s, 0, 1)); /* no change: nothing */
    assert(!snx_drp_due("b", "a", 3 * s, 0, 1));   /* a change inside the 4 s waits */
    assert(snx_drp_due("b", "a", 4 * s, 0, 1));    /* and goes at their end */
}

static void test_battle_over(void) {
    const uint64_t s = 1000000000ull;
    assert(!snx_drp_battle_over(0, 100 * s, 0));     /* not in a battle: nothing to end */
    assert(!snx_drp_battle_over(1, 12 * s, 10 * s)); /* 2 s quiet: still in it */
    assert(snx_drp_battle_over(1, 13 * s, 10 * s));  /* 3 s quiet: over */
    assert(!snx_drp_battle_over(1, 9 * s, 10 * s));  /* a start after now (a race): not over */
}

static void test_session(void) {
    Stand st = {0};
    const SnxDrpPipe pipe = {st_open, st_write, st_read, st_close, &st};
    SnxDrpSession d = {&pipe, "123", 7, 1700000000, 0, 0, 0, 0, 0, 0, 0, ""};
    SnxDrpValues v = {.region = 1, .activity = DRP_FIELD};
    const uint64_t s = 1000000000ull;
    /* no Discord: ten pipes tried, then nothing until 30 s later */
    snx_drp_step(&d, &v, 1 * s);
    assert(st.opens == 10 && !d.connected && !st.frames);
    snx_drp_step(&d, &v, 20 * s);
    assert(st.opens == 10);
    st.open_ok = 1;
    snx_drp_step(&d, &v, 31 * s);
    assert(d.connected && st.frames == 2 && st.last_opcode == 1 && strstr(st.last, "Obsidian") && d.sends == 1);
    /* the same text: nothing; a change at once waits for 4 s, and the newest is sent */
    snx_drp_step(&d, &v, 32 * s);
    assert(st.frames == 2);
    v.activity = DRP_BATTLE;
    snx_drp_step(&d, &v, 33 * s);
    assert(st.frames == 2);
    v.foe = mon(401, 5, 0);
    snx_drp_step(&d, &v, 35 * s);
    assert(st.frames == 3 && strstr(st.last, "Battling a wild Kricketot"));
    /* the party changing alone is a change, through the hover text */
    v.party[0] = mon(493, 100, 0);
    snx_drp_step(&d, &v, 39 * s);
    assert(st.frames == 4 && strstr(st.last, "\"large_text\":\"Arceus Lv. 100\""));
    assert(!strcmp(d.sent, "Battling a wild Kricketot / - / Arceus Lv. 100"));
    /* a failed write drops the pipe, the next try 30 s on sends the activity again on a new pipe */
    st.fail_write = 1;
    v.activity = DRP_FIELD;
    snx_drp_step(&d, &v, 44 * s);
    assert(!d.connected && st.closes == 1);
    st.fail_write = 0;
    snx_drp_step(&d, &v, 50 * s);
    assert(!d.connected);
    snx_drp_step(&d, &v, 71 * s);
    assert(d.connected && strstr(st.last, "Obsidian") && st.last_opcode == 1);
    /* Discord closing the pipe (opcode 2 in a reply) drops it too */
    st.close_next = 1;
    v.activity = DRP_BATTLE;
    snx_drp_step(&d, &v, 80 * s);
    assert(!d.connected && st.closes == 2);
}

int main(void) {
    test_sequence();
    test_packs();
    test_text();
    test_species();
    test_hover();
    test_orders();
    test_crypt();
    test_decode();
    test_app();
    test_json();
    test_frame();
    test_due();
    test_battle_over();
    test_session();
    puts("discord_presence: all checks passed");
    return 0;
}
