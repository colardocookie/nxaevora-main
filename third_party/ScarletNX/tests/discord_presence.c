/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Host tests of Discord presence's pure parts (runtime/discord_presence.h, DISCORD-DESIGN.md D1): the state's
   sequence, the region table, the text, the JSON writer, the frame, the rate rule and the session over a stand-in
   pipe (reconnect on a failed or closed pipe included). No run of ours talks to Discord. */
#include "nx/runtime/discord_presence.h"
#include <assert.h>
#include <stdio.h>

SnxDrpState g_drp_state;
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

static void test_sequence(void) {
    SnxDrpState s = {0, -1, DRP_FIELD};
    SnxDrpValues v;
    snx_drp_write(&s, 1, DRP_WILD);
    assert(s.seq == 2 && snx_drp_read(&s, &v, 4) && v.region == 1 && v.activity == DRP_WILD);
    s.seq = 3; /* a writer in the middle: no copy is taken */
    v.region = 7;
    assert(!snx_drp_read(&s, &v, 4) && v.region == 7);
}

static void test_regions(void) {
    assert(snx_drp_region("ha_area00") == 0 && snx_drp_region("ha_area01") == 1 && snx_drp_region("ha_area05") == 5);
    assert(snx_drp_region("ha_area00_s03_a") == 0 && snx_drp_region("ha_area02_s01") == 2);
    assert(snx_drp_region("ha_area06") == -1 && snx_drp_region("ha_area11") == -1);
    assert(snx_drp_region("ha_area011") == -1 && snx_drp_region("ha_area01x") == -1);
    assert(snx_drp_region("ha_area01_x") == -1);
    assert(snx_drp_region("ha_test_battle_00") == -1 && snx_drp_region("") == -1 && snx_drp_region(NULL) == -1);
    char line[160];
    SnxDrpValues v = {1, DRP_FIELD};
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "Exploring Obsidian Fieldlands"));
    v.region = 0; /* not confirmed yet */
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "Exploring Hisui"));
    v.region = -1;
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "Exploring Hisui"));
    v.region = 99;
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "Exploring Hisui"));
    v.activity = DRP_WILD;
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "Battling a wild Pok\xc3\xa9mon"));
    v.activity = DRP_TRAINER;
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "Battling a trainer"));
    v.activity = DRP_BATTLE;
    assert(!strcmp(snx_drp_details(&v, line, sizeof line), "In battle"));
    for (int i = 0; i < SNX_DRP_REGIONS; i++) assert(snx_drp_region(snx_drp_regions[i].name) == i);
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
    SnxDrpValues v = {1, DRP_FIELD};
    assert(snx_drp_activity(act, sizeof act, &v, 42, 1700000000, 3));
    assert(strstr(act, "\"details\":\"Exploring Obsidian Fieldlands\"") && strstr(act, "\"pid\":42") &&
           strstr(act, "\"start\":1700000000") && strstr(act, "\"large_image\":\"nxaevora\"") &&
           strstr(act, "\"nonce\":\"3\"") && !strstr(act, "\"state\""));
    assert(snx_drp_handshake(act, sizeof act, "123") && !strcmp(act, "{\"v\":1,\"client_id\":\"123\"}"));
}

static void test_frame(void) {
    unsigned char f[32];
    assert(snx_drp_frame(f, sizeof f, 1, "{}") == 10 && f[0] == 1 && f[4] == 2 && f[8] == '{' && f[9] == '}');
    assert(!snx_drp_frame(f, 9, 1, "{}"));
}

static void test_due(void) {
    const uint64_t s = 1000000000ull;
    assert(snx_drp_due("a", "", 0, 0, 0));
    assert(!snx_drp_due("a", "a", 100 * s, 0, 1));       /* no change: nothing */
    assert(!snx_drp_due("b", "a", 3 * s, 0, 1));         /* a change inside the 4 s waits */
    assert(snx_drp_due("b", "a", 4 * s, 0, 1));          /* and goes at their end */
}

static void test_session(void) {
    Stand st = {0};
    const SnxDrpPipe pipe = {st_open, st_write, st_read, st_close, &st};
    SnxDrpSession d = {&pipe, "123", 7, 1700000000, 0, 0, 0, 0, 0, 0, 0, ""};
    SnxDrpValues v = {1, DRP_FIELD};
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
    v.activity = DRP_WILD;
    snx_drp_step(&d, &v, 33 * s);
    assert(st.frames == 2);
    v.activity = DRP_TRAINER;
    snx_drp_step(&d, &v, 35 * s);
    assert(st.frames == 3 && strstr(st.last, "trainer"));
    /* a failed write drops the pipe, the next try 30 s on sends the activity again on a new pipe */
    st.fail_write = 1;
    v.activity = DRP_FIELD;
    snx_drp_step(&d, &v, 40 * s);
    assert(!d.connected && st.closes == 1);
    st.fail_write = 0;
    snx_drp_step(&d, &v, 50 * s);
    assert(!d.connected);
    snx_drp_step(&d, &v, 71 * s);
    assert(d.connected && strstr(st.last, "Obsidian") && st.last_opcode == 1);
    /* Discord closing the pipe (opcode 2 in a reply) drops it too */
    st.close_next = 1;
    v.activity = DRP_WILD;
    snx_drp_step(&d, &v, 80 * s);
    assert(!d.connected && st.closes == 2);
}

int main(void) {
    test_sequence();
    test_regions();
    test_app();
    test_json();
    test_frame();
    test_due();
    test_session();
    puts("discord_presence: all checks passed");
    return 0;
}
