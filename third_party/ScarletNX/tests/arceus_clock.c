/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Exercise the real wrappers with an asset-free guest sequence and host stubs. */
#include "nx/runtime/arceus_clock.c"
#include <assert.h>

static u8 memory[4096];
u8* g_mem = memory;
u64 g_as_size = sizeof memory;
MemLayout g_layout = {.base = 1};
const SnxModule* g_mod;
GuestFn* g_disp;
u64 g_code_lo, g_code_hi;
int g_opt_fps;
void pm_lock(PMutex* m) { (void)m; }
void pm_unlock(PMutex* m) { (void)m; }
void logf_(int level, const char* fmt, ...) { (void)level; (void)fmt; }
u64 plat_ns(void) { return 1000000000; }
void mem_query(u64 address, MemBlock* out) {
    (void)address;
    *out = (MemBlock){.addr = 1, .size = sizeof memory - 1, .perm = PERM_RW};
}
u64 mem_find_free(u64 lo, u64 hi, u64 size, u64 align) {
    (void)lo; (void)hi; (void)size; (void)align;
    assert(0); return 0;
}
int mem_map(u64 address, u64 size, u32 state, u32 perm) {
    (void)address; (void)size; (void)state; (void)perm;
    assert(0); return -1;
}
void jit_code_changed(u64 pc, u64 size) { (void)pc; (void)size; assert(0); }
GuestFn jit_wait(u64 pc) { (void)pc; assert(0); return NULL; }

static unsigned calls, boundaries, calls_this_render;
static int stop_at, change_speed, divert, no_progress;
static const u64 object = 128, entry_pc = 16, return_pc = 32;
static void sequence_original(CPU* c, u64 entry) {
    assert(entry == entry_pc && c->x[0] == object && c->x[30] == return_pc);
    const float before = guest_float(object + 0xe8);
    const float after = before + guest_float(object + 0xf0);
    /* Every integer command-end comparison must be visited, never crossed. */
    assert(after <= floorf(before) + 1);
    /* Extra calls may catch up complete boundaries, never a fractional tail. */
    if (calls_this_render++) assert(after == floorf(before) + 1);
    if (after == floorf(before) + 1) boundaries++;
    if (!no_progress) guest_float_write(object + 0xe8, after);
    if (stop_at && after == stop_at) *GP(object + 0x301) = 0;
    if (change_speed) guest_float_write(object + 0xf0, 0);
    c->x[0] = ++calls;
    c->x[30] = 999; /* The wrapper must restore the caller on re-entry. */
    c->pc = divert ? 64 : return_pc;
}
static void reset(float frame, float speed, u64 ns) {
    memset(memory, 0, sizeof memory);
    memset(sequence_slots, 0, sizeof sequence_slots);
    sequence_next_slot = 0;
    calls = boundaries = 0;
    stop_at = change_speed = divert = no_progress = 0;
    guest_float_write(object + 0xe8, frame);
    guest_float_write(object + 0xec, 10000);
    guest_float_write(object + 0xf0, speed);
    *GP(object + 0x301) = *GP(object + 0x302) = *GP(object + 0x303) = 1;
    step.step = ns;
    battle_sequence_original = sequence_original;
}
static void update(void) {
    CPU cpu = {.x = {[0] = 128, [30] = 32}, .pc = 16};
    calls_this_render = 0;
    battle_sequence_update(&cpu, entry_pc);
    assert(cpu.pc == (divert ? 64 : return_pc));
    assert(cpu.x[0] == calls);
}
static void test_sequence(void) {
    static const unsigned rates[] = {60, 120, 144, 240, 500, 1000};
    for (unsigned r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        const unsigned fps = rates[r];
        for (float speed = .25f; speed <= 4; speed *= 2) {
            reset(0, speed, 1000000000 / fps);
            for (unsigned i = 0; i < fps; i++) update();
            assert(fabsf(guest_float(object + 0xe8) + sequence_slots[0].pending - 30 * speed) < .001f);
            assert(sequence_slots[0].pending < 1);
        }
    }
    reset(.25f, 4, 100000000); /* A clamped stall needs twelve authored frames. */
    update();
    assert(calls == 12 && boundaries == 12);
    assert(guest_float(object + 0xe8) == 12);
    assert(fabsf(sequence_slots[0].pending - .25f) < .00001f);
    assert(guest_float(object + 0xf0) == 4);
    reset(0, 4, 16666667);
    stop_at = 1;
    update();
    assert(calls == 1 && guest_float(object + 0xe8) == 1);
    reset(0, 4, 16666667);
    change_speed = 1;
    update();
    assert(calls == 1 && guest_float(object + 0xf0) == 0);
    reset(0, 4, 16666667);
    divert = 1;
    update();
    assert(calls == 1);
    reset(0, 4, 16666667);
    no_progress = 1;
    update();
    assert(calls == 1);
    reset(0, 4, 16666667);
    sequence_slots[0].object = object;
    sequence_slots[0].pending = 40;
    update();
    assert(calls == 32 && sequence_slots[0].pending >= 9.99f);
    update();
    assert(fabsf(guest_float(object + 0xe8) - 44) < .0001f);
    /* A nearly complete frame must not cause a second, tiny callback this render. */
    reset(.99f, 1, 16666667);
    update();
    assert(calls == 1 && guest_float(object + 0xe8) == 1);
    assert(fabsf(sequence_slots[0].pending - .49f) < .00001f);
    update();
    assert(calls == 2 && guest_float(object + 0xe8) > 1.98f);
    /* Changing frame intervals carry elapsed budget rather than a fixed FPS ratio. */
    const u64 intervals[] = {8333333, 16666667, 11111111, 25000000, 10000000, 100000000,
                             1000000, 4166667, 2777778, 6944444, 1000000, 50000000};
    for (float speed = .25f; speed <= 4; speed *= 2) {
        reset(0, speed, 1);
        u64 elapsed = 0;
        for (unsigned i = 0; i < 120; i++) {
            step.step = intervals[i % (sizeof intervals / sizeof intervals[0])];
            elapsed += step.step;
            update();
            const double expected = speed * (double)elapsed / 33333333.0;
            assert(fabs(guest_float(object + 0xe8) + sequence_slots[0].pending - expected) < .002);
            assert(sequence_slots[0].pending < 1);
        }
    }
}
static void flag(const char* name, const char* value) {
#ifdef _WIN32
    assert(!_putenv_s(name, value));
#else
    assert(!setenv(name, value, 1));
#endif
}
/* A new air steering entry is scaled by step / (1/30). One second of entries sums to 30 authored ones. */
static void test_air_steering(void) {
    const u32 none[3] = {0};
    const float authored[3] = {0.25f, 0.0f, -0.1f};
    const float steps[4][2] = {{1.0f / 60, 1.0f / 60}, {1.0f / 120, 1.0f / 120}, {0.012f, 0.020f}, {0.008f, 0.030f}};
    memset(memory, 0, sizeof memory);
    for (unsigned k = 0; k < 3; k++) guest_float_write(object + 0x6f0 + 4 * k, authored[k]);
    /* No flag, a full 1/30 step and a slower frame leave the entry bit exact. */
    assert(!air_steering_scale(object, 0, none, 1.0f / 60));
    *GP(object + 0x6e0) = 1;
    assert(!air_steering_scale(object, 0, none, NOMINAL_STEP_SECONDS));
    assert(!air_steering_scale(object, 0, none, 0.05f));
    assert(!air_steering_scale(object, 0, none, 0.0f));
    for (unsigned k = 0; k < 3; k++) assert(guest_float(object + 0x6f0 + 4 * k) == authored[k]);
    for (unsigned schedule = 0; schedule < 4; schedule++) {
        double elapsed = 0, sum[3] = {0};
        for (unsigned call = 0; elapsed < 1.0; call++) {
            const float seconds = steps[schedule][call & 1];
            for (unsigned k = 0; k < 3; k++) guest_float_write(object + 0x6f0 + 4 * k, authored[k]);
            assert(air_steering_scale(object, 0, none, seconds));
            for (unsigned k = 0; k < 3; k++) sum[k] += guest_float(object + 0x6f0 + 4 * k);
            elapsed += seconds;
        }
        for (unsigned k = 0; k < 3; k++) assert(fabs(sum[k] - authored[k] * elapsed * 30) < 1e-4);
    }
    /* An entry still queued and not rewritten by this call is not scaled again. */
    u32 previous[3];
    for (unsigned k = 0; k < 3; k++) previous[k] = gr32(object + 0x6f0 + 4 * k);
    assert(!air_steering_scale(object, 1, previous, 1.0f / 60));
    for (unsigned k = 0; k < 3; k++) assert(gr32(object + 0x6f0 + 4 * k) == previous[k]);
    /* A rewritten entry on a queue that was already set is scaled. */
    guest_float_write(object + 0x6f0, authored[0]);
    assert(air_steering_scale(object, 1, previous, 1.0f / 60));
    assert(fabsf(guest_float(object + 0x6f0) - authored[0] / 2) < 1e-7f);
    /* A value that is not finite is left alone. */
    guest_float_write(object + 0x6f8, NAN);
    guest_float_write(object + 0x6f0, authored[0]);
    assert(!air_steering_scale(object, 0, none, 1.0f / 60));
    assert(guest_float(object + 0x6f0) == authored[0]);
}
/* The three requested schedules are replayed through the real cell and wrappers. */
/* The mock stands for the patched 1050E00 body: every context gains V9.S[1] frames. */
static unsigned ai_frames, ai_passes, ai_turns;
static void ai_tick_mock(CPU* c, u64 entry) {
    (void)entry;
    assert(c->v[9].s[0] == 0 && c->v[9].s[1] <= 1);
    ai_frames += c->v[9].s[1];
    gw32(object + 0x88, gr32(object + 0x88) + c->v[9].s[1]);
    ai_passes++;
    c->pc = c->x[30];
}
static void ai_turn_mock(CPU* c, u64 entry) {
    (void)entry;
    ai_turns++;
    gw32(c->x[8], 1);
    c->pc = c->x[30];
}
/* The mock stands for 1727DF4: one slerp step toward the target with the game's fraction. */
static float turn_angle, turn_passed;
static void turn_mock(CPU* c, u64 entry) {
    (void)entry;
    turn_passed = c->v[0].f[0];
    const float t = guest_float(c->x[0] + 0x268) * NOMINAL_STEP_SECONDS / turn_passed;
    turn_angle += (1 - turn_angle) * (t < 1 ? t : 1);
    c->pc = c->x[30];
}
static u64 audit_step(unsigned schedule, unsigned i) {
    if (!schedule) return 16666667;
    if (schedule == 1) return i & 1 ? 20000000 : 12000000;
    return i % 31 == 30 ? 100000000 : 8000000 + (i * 1103515245u + 12345u) % 32000001;
}
static double audit_cursor_distance;
static void audit_map_original(CPU* c, u64 entry) {
    (void)entry;
    const MapCursorSite* site = &map_cursor_sites[0];
    audit_cursor_distance += guest_float(c->x[0] + site->speed);
    for (unsigned k = 0; k < 2; k++) {
        u64 at = c->x[0] + site->held + 4 * k;
        if (gr32(at) < 9) gw32(at, gr32(at) + 1);
    }
    c->pc = c->x[30];
}
/* Recover long-frame tick budget on later calls without replaying game callbacks. */
static void test_timer_carry(void) {
    for (unsigned enabled = 0; enabled < 2; enabled++) {
        timer_carry_fix = (int)enabled;
        memset(memory, 0, sizeof memory);
        memset(capture_slots, 0, sizeof capture_slots);
        capture_next_slot = 0;
        memset(map_cursor_object, 0, sizeof map_cursor_object);
        memset(map_cursor_ns, 0, sizeof map_cursor_ns);
        map_cursor_original[0] = audit_map_original;
        guest_float_write(object + map_cursor_sites[0].speed, 1);
        u64 elapsed = 0;
        unsigned captures = 0, holds = 0;
        for (unsigned i = 0; i < 420; i++) {
            const u64 ns = i < 300 ? audit_step(2, i) : 12000000;
            step.step = ns;
            elapsed += ns;
            captures += capture_tick(object, 1);
            /* A fresh held counter makes every retained tick observable. */
            gw32(object + map_cursor_sites[0].held, 0);
            gw32(object + map_cursor_sites[0].held + 4, 0);
            CPU c = {.x = {[0] = 128, [30] = 32}};
            field_map_cursor_update(&c, entry_pc);
            holds += gr32(object + map_cursor_sites[0].held);
        }
        if (enabled) {
            assert(captures == elapsed / CAPTURE_TICK_NS);
            assert(holds == elapsed / 33333333ull);
            assert(capture_slots[0].ns == (int64_t)(elapsed % CAPTURE_TICK_NS));
            assert(map_cursor_ns[0] == elapsed % 33333333ull);
        } else {
            assert(captures < elapsed / CAPTURE_TICK_NS);
            assert(holds < elapsed / 33333333ull);
        }
    }
    /* Repeated slow calls cannot grow an unbounded recovery delay. */
    for (unsigned i = 0; i < 100; i++) {
        step.step = 100000000;
        (void)capture_tick(object, 1);
        CPU c = {.x = {[0] = 128, [30] = 32}};
        field_map_cursor_update(&c, entry_pc);
    }
    assert(capture_slots[0].ns <= (int64_t)TIMER_CARRY_LIMIT_NS);
    assert(map_cursor_ns[0] <= TIMER_CARRY_LIMIT_NS);
    /* The cursor mock ran and moved the cursor. */
    assert(audit_cursor_distance > 0);
    timer_carry_fix = 0;
}
static void test_ai_tick(void) {
    const u64 saved_base = main_base;
    const u64 node = 512, owner = 1024, out = 2048;
    ai_tick_original = ai_tick_mock;
    ai_turn_original[0] = ai_turn_original[1] = ai_turn_mock;
    /* Schedules: 30 FPS, 30 FPS with jitter, 60, 120, 12/20 ms, 8..40 ms with stalls. */
    for (unsigned schedule = 0; schedule < 6; schedule++) {
        memset(memory, 0, sizeof memory);
        memset(ai_tick_slots, 0, sizeof ai_tick_slots);
        ai_frames = ai_passes = ai_turns = 0;
        ai_turn_holds[0] = ai_turn_holds[1] = 0;
        main_base = 3072 - 0x40e7c04;
        gw32(3072, 4);
        gw32(node + 0x130, 30);
        gw32(node + 0x140, 30);
        gw64(node + 0x58, owner);
        gw64(owner + 0x90, object);
        double want = 0;
        for (unsigned i = 0; i < 2000; i++) {
            const u64 ns = schedule == 0 ? 33333333 : schedule == 1 ? 31000000 + i * 7919 % 5000000 :
                           schedule == 2 ? 16666667 : schedule == 3 ? 8333333 : audit_step(schedule - 3, i);
            step.step = ns;
            want += ns >= 30000000 ? 1 : ns / 33333333.0;
            CPU c = {.x = {[0] = 128, [30] = 32}};
            c.v[9].s[0] = 0x11111111;
            c.v[9].s[1] = 0x22222222;
            gw32(object + 0x88, 0);
            ai_tick_update(&c, entry_pc);
            assert(c.pc == return_pc && c.v[9].s[0] == 0x11111111 && c.v[9].s[1] == 0x22222222);
            /* At 30 FPS, even or uneven, every pass ticks as in the game. */
            if (schedule < 2) assert(ai_frames == i + 1);
            if (schedule == 2) assert(ai_frames == (i + 1) / 2);
            assert(fabs(ai_frames - want) < 1.04);
            /* A turn runs on a pass with a tick and is held on one without. */
            for (unsigned kind = 0; kind < 2; kind++) {
                const unsigned before = ai_turns;
                CPU t = {.x = {[0] = node, [8] = out, [30] = 32}};
                gw32(out, 99);
                ai_turn_update(&t, entry_pc, kind);
                assert(t.pc == return_pc);
                assert(ai_turns - before == gr32(object + 0x88));
                assert(gr32(out) == (gr32(object + 0x88) ? 1u : 4u));
            }
        }
        assert(ai_passes == 2000 && ai_turns == 2 * ai_frames);
        assert(ai_turn_holds[0] == 2000 - ai_frames && ai_turn_holds[1] == 2000 - ai_frames);
    }
    /* A zero duration and a missing context both reach the original. */
    gw32(object + 0x88, 0);
    gw32(node + 0x130, 0);
    CPU t = {.x = {[0] = node, [8] = out, [30] = 32}};
    ai_turns = 0;
    ai_turn_update(&t, entry_pc, 0);
    gw64(owner + 0x90, 0);
    ai_turn_update(&t, entry_pc, 1);
    assert(ai_turns == 2);
    /* Two schedulers keep separate tick phases. */
    memset(ai_tick_slots, 0, sizeof ai_tick_slots);
    assert(!ai_tick_count(1, 16666667) && !ai_tick_count(2, 16666667));
    assert(ai_tick_count(1, 16666667) && ai_tick_count(2, 16666667));
    main_base = saved_base;
}
static void test_turn(void) {
    /* Rate and duration pairs: a 1/30 s duration as 1A19104 passes, a looked-up one, a snap. */
    static const float cases[3][2] = {{.2f, 1.0f / 30}, {3, .5f}, {2, 1.0f / 30}};
    memset(memory, 0, sizeof memory);
    frame_step_address = 0xf00;
    turn_original = turn_mock;
    for (unsigned k = 0; k < 3; k++) {
        float reached[4];
        guest_float_write(object + 0x268, cases[k][0]);
        /* 0.8 s at 30, 60 and 120 FPS and at 12/20 ms: 24, 48, 96 and 50 calls. */
        for (unsigned schedule = 0; schedule < 4; schedule++) {
            static const unsigned calls_for[4] = {24, 48, 96, 50};
            turn_angle = 0;
            for (unsigned i = 0; i < calls_for[schedule]; i++) {
                const u64 ns = schedule == 0 ? 33333333 : schedule == 1 ? 16666667 : schedule == 2 ? 8333333 :
                               i & 1 ? 20000000 : 12000000;
                frame_step_write(ns);
                CPU c = {.x = {[0] = 128, [30] = 32}};
                c.v[0].f[0] = cases[k][1];
                turn_update(&c, entry_pc);
                assert(c.pc == return_pc);
                /* 30 FPS and a snap pass the authored duration through, bit for bit. */
                if (!schedule || k == 2) assert(turn_passed == cases[k][1]);
                else assert(turn_passed > cases[k][1]);
            }
            reached[schedule] = turn_angle;
            assert(fabsf(reached[schedule] - reached[0]) < 1e-4f);
        }
        assert(k == 2 ? reached[0] == 1 : reached[0] > .9f && reached[0] < 1);
    }
    /* The helper's early out for a tiny duration and a zero rate stay untouched. */
    assert(turn_duration(.2f, 1e-8f, .5f) == 1e-8f && turn_duration(0, .5f, .5f) == .5f);
    frame_step_address = 0;
}
static void test_diagnostics(void) {
    SnxModInfo main_mod = {.name = "main", .base = 4096,
        .build_id = {0xae,0xe8,0xf1,0x50,0xdd,0xa1,0xb5,0xa8,0x38,0x80,0x6e,0x1a,0x5e,0xa6,0x82,0x7a,
                     0xd9,0xf3,0xc5,0x1e}};
    SnxModule module = {.nmods = 1, .mods = &main_mod};
    g_mod = &module;
    const char* flags[] = {"SNX_ARCEUS_CAMERA_TRACE", "SNX_ARCEUS_TIMING_RESEARCH"};
    for (unsigned i = 0; i < 2; i++) {
        flag(flags[i], "1");
        main_base = 0;
        arceus_clock_init();
        assert(main_base == main_mod.base && !clocks_installed);
        main_base = 0;
        main_mod.build_id[0] ^= 1;
        arceus_clock_init();
        assert(main_base == 0);
        main_mod.build_id[0] ^= 1;
        flag(flags[i], "0");
    }
}
/* SNX_ARCEUS_TARGET_FPS=unlocked or 0 reaches the build check like 60 and 120, and any other
   value is refused before it. */
static void test_target(void) {
    SnxModInfo main_mod = {.name = "main", .base = 4096,
        .build_id = {0xae,0xe8,0xf1,0x50,0xdd,0xa1,0xb5,0xa8,0x38,0x80,0x6e,0x1a,0x5e,0xa6,0x82,0x7a,
                     0xd9,0xf3,0xc5,0x1e}};
    SnxModule module = {.nmods = 1, .mods = &main_mod};
    g_mod = &module;
    const char* accepted[] = {"unlocked", "0", "60", "120"};
    for (unsigned i = 0; i < 4; i++) {
        flag("SNX_ARCEUS_TARGET_FPS", accepted[i]);
        main_base = 0;
        arceus_clock_init();
        assert(main_base == main_mod.base);
    }
    const char* refused[] = {"144", "unlimited", "-1"};
    for (unsigned i = 0; i < 3; i++) {
        flag("SNX_ARCEUS_TARGET_FPS", refused[i]);
        main_base = 0;
        arceus_clock_init();
        assert(main_base == 0);
    }
    flag("SNX_ARCEUS_TARGET_FPS", "");
}
/* The climb fix's cell load: ADRP X9 to the frame-step cell's page from the patched word,
   checked against llvm-mc encodings of adrp x9 at +0x3000, -0x5000 and +0x7FF000. */
static void test_climb_adrp(void) {
    frame_step_address = 0x10003000;
    assert(climb_adrp(0x10000ffc, 9) == 0xf0000009);
    frame_step_address = 0x0fffb000;
    assert(climb_adrp(0x10000000, 9) == 0xf0ffffc9);
    frame_step_address = 0x107ff000;
    assert(climb_adrp(0x10000123, 9) == 0xf0003fe9);
    frame_step_address = 0;
}
/* The climb stop: the stub adds one to +D04 in state 2 and finishes at stop_frames, as the
   animation does. Staging holds the count to whole 1/30 s steps. */
static const u64 climber = 128, cell = 3840;
static u32 stop_frames;
static unsigned stop_calls;
static void climb_original(CPU* c, u64 entry) {
    (void)c; (void)entry;
    stop_calls++;
    if (gr32(climber + 0xd00) != 2) return;
    const u32 frame = gr32(climber + 0xd04) + 1;
    gw32(climber + 0xd04, frame);
    if ((int)frame >= (int)stop_frames) gw32(climber + 0xd00, 1);
}
static double climb_stop_seconds(const float* steps, unsigned n) {
    gw32(climber + 0xd00, 2);
    gw32(climber + 0xd04, 0);
    stop_calls = 0;
    double seconds = 0;
    for (unsigned i = 0; gr32(climber + 0xd00) == 2 && i < 10000; i++) {
        guest_float_write(cell, steps[i % n]);
        seconds += steps[i % n];
        CPU cpu = {.x = {[0] = climber}};
        climb_anim_update(&cpu, 0);
    }
    assert(gr32(climber + 0xd00) == 1);
    return seconds;
}
static void test_climb_stop(void) {
    memset(memory, 0, sizeof memory);
    memset(climb_stop_slots, 0, sizeof climb_stop_slots);
    frame_step_address = cell;
    climb_anim_original = climb_original;
    climb_stop_fix = 1;
    stop_frames = 3;
    const float at30 = 1.0f / 30, at120 = 1.0f / 120, at1000 = 1.0f / 1000;
    climb_stop_seconds(&at30, 1);
    assert(stop_calls == 3);
    climb_stop_seconds(&at120, 1);
    assert(stop_calls == 12);
    climb_stop_seconds(&at1000, 1);
    assert(stop_calls >= 99 && stop_calls <= 101);
    /* Mixed steps end once three whole 1/30 s have passed, within one call. */
    const float mixed[] = {1.0f / 144, 1.0f / 60, 1.0f / 240, 1.0f / 90, 1.0f / 30};
    const double seconds = climb_stop_seconds(mixed, 5);
    assert(seconds >= 0.1 - 1e-6 && seconds < 0.1 + 1.0 / 30);
    /* An interrupted stop frees the slot, and the next stop starts from zero. */
    gw32(climber + 0xd00, 2);
    gw32(climber + 0xd04, 0);
    guest_float_write(cell, at120);
    CPU cpu = {.x = {[0] = climber}};
    climb_anim_update(&cpu, 0);
    gw32(climber + 0xd00, 1);
    climb_anim_update(&cpu, 0);
    assert(!climb_stop_slots[0].object);
    climb_stop_seconds(&at120, 1);
    assert(stop_calls == 12);
    climb_stop_fix = 0;
    frame_step_address = 0;
}
/* The wall jump wait: a fake Lua state whose stack holds the setter closure, the context
   userdata, the key and the value, run as the ride setter converts and stores each write. */
static const u64 lua_L = 0x200, lua_ci = 0x300, lua_func = 0x400, lua_closure = 0x600, lua_udata = 0x700,
                 lua_key = 0x800, jumper = 0x900, fake_main = 0x100000;
static void lua_value(u64 at, u64 value, u32 tag) {
    gw64(at, value);
    gw32(at + 8, tag);
}
static void wall_jump_conversion(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = gr64(lua_func + 0x30);
}
static void fake_lua(const char* key) {
    gw64(lua_L + 0x10, lua_func + 0x40);
    gw64(lua_L + 0x20, lua_ci);
    gw64(lua_ci, lua_func);
    lua_value(lua_func, lua_closure, 0x66);
    lua_value(lua_func + 0x10, lua_udata, 0x47);
    lua_value(lua_func + 0x20, lua_key, 0x44);
    gw64(lua_closure + 0x18, fake_main + 0x2335e5c);
    gw64(lua_udata + 0x28, jumper);
    const u32 length = (u32)strlen(key);
    gw32(lua_key + 8, 4 | length << 24);
    memcpy(GP(lua_key + 0x18), key, length);
}
/* One script write of value through the wrapper; returns what the setter stores. */
static int wall_jump_write(long long value, u64 caller) {
    lua_value(lua_func + 0x30, (u64)value, 0x13);
    CPU cpu = {.x = {[0] = lua_L, [1] = 3, [30] = caller}};
    wall_jump_update(&cpu, 0x50ba0);
    gw32(jumper + 0xf4, (u32)cpu.x[0]);
    return (int)cpu.x[0];
}
static unsigned wall_jump_countdown(float step) {
    guest_float_write(cell, step);
    wall_jump_write(15, fake_main + 0x2331b64);
    unsigned updates = 0;
    while ((int)gr32(jumper + 0xf4) > 0 && updates < 1000) {
        wall_jump_write((int)gr32(jumper + 0xf4) - 1, fake_main + 0x2331b64);
        updates++;
    }
    return updates;
}
static void test_wall_jump(void) {
    memset(memory, 0, sizeof memory);
    memset(wall_jump_slots, 0, sizeof wall_jump_slots);
    frame_step_address = cell;
    main_base = fake_main;
    wall_jump_original = wall_jump_conversion;
    fake_lua("climbingJumpWaitCount");
    assert(wall_jump_countdown(1.0f / 30) == 15);
    assert(wall_jump_countdown(1.0f / 120) == 60);
    assert(wall_jump_countdown(1.0f / 1000) >= 495 && wall_jump_countdown(1.0f / 1000) <= 505);
    /* Another caller or another key is never held. */
    guest_float_write(cell, 1.0f / 120);
    gw32(jumper + 0xf4, 10);
    assert(wall_jump_write(9, fake_main + 0x2331b00) == 9);
    fake_lua("climbingJumpWaitCoun_");
    gw32(jumper + 0xf4, 10);
    assert(wall_jump_write(9, fake_main + 0x2331b64) == 9);
    /* A full step keeps its carry: .25 .25 .25 1 .25 from 5 ends at 3 with no fraction left. */
    memset(wall_jump_slots, 0, sizeof wall_jump_slots);
    const float wants[] = {.25f, .25f, .25f, 1, .25f};
    int value = 5;
    for (unsigned i = 0; i < 5; i++)
        if (!wall_jump_hold(jumper, value, value - 1, wants[i])) value--;
    assert(value == 3 && wall_jump_slots[0].progress == 0.0f);
    main_base = 0;
    frame_step_address = 0;
}
/* The ledge throttle through the real wrappers: a stub state script that ends the climb from its
   PreUpdate (so the window's owner comes from the wrapper), then 1/120 s frames. */
static const u64 ledge_state = 0x500, ledge_other = 0x540, ledge_controller = 0x600;
static unsigned ledge_runs[2], ledge_other_runs, ledge_script_step;
static int ledge_end_at, ledge_finish_at;
static void ledge_state_original(CPU* c, u64 entry, unsigned post) {
    (void)entry;
    if (c->x[0] == ledge_other) {
        ledge_other_runs++;
        c->x[0] = 1;
        return;
    }
    ledge_runs[post]++;
    c->x[0] = 1;
    if (post) return;
    ledge_script_step++;
    CPU end = {.x = {[0] = ledge_controller, [1] = 0}};
    if ((int)ledge_script_step == ledge_end_at) ledge_end_start(&end, 0x2553ee0);
    if ((int)ledge_script_step == ledge_finish_at) ledge_end_finish(&end, 0x2554008);
}
static void ledge_pre_original(CPU* c, u64 entry) { ledge_state_original(c, entry, 0); }
static void ledge_post_original(CPU* c, u64 entry) { ledge_state_original(c, entry, 1); }
static void ledge_end_original(CPU* c, u64 entry) { (void)c; (void)entry; }
/* One engine frame: a new frame id, then Pre and Post of object. Returns 1 when Pre ran. */
static int ledge_frame(u64 object, float step) {
    __atomic_add_fetch(&frame_step_frames, 1, __ATOMIC_RELEASE);
    guest_float_write(cell, step);
    const unsigned before = ledge_runs[0];
    for (unsigned post = 0; post < 2; post++) {
        CPU cpu = {.x = {[0] = object, [30] = return_pc}, .pc = 16};
        if (post) ledge_post_update(&cpu, 0x2616ab0);
        else ledge_pre_update(&cpu, 0x26163e0);
        assert(cpu.pc == return_pc || cpu.x[0] == 1);
        if (cpu.x[0] != 1) assert(cpu.x[0] == 7);
    }
    return ledge_runs[0] != before;
}
static void test_ledge(void) {
    memset(memory, 0, sizeof memory);
    ledge_retire(&ledge_window);
    frame_step_address = cell;
    gw32(ledge_state + 0x6c, 7);
    gw32(ledge_other + 0x6c, 7);
    ledge_originals[0] = ledge_pre_original;
    ledge_originals[1] = ledge_post_original;
    ledge_end_originals[0] = ledge_end_original;
    ledge_end_originals[1] = ledge_end_original;
    ledge_end_at = 1;
    ledge_finish_at = -1;
    /* The frame that ends the climb runs. Then 84 frames at 120 run the script 21 times, as 0.7 s at 30. */
    assert(ledge_frame(ledge_state, 1.0f / 120) && ledge_window.open && ledge_window.owner == ledge_state);
    unsigned runs = 0;
    for (unsigned i = 0; i < 84; i++) {
        const unsigned post_before = ledge_runs[1];
        const int ran = ledge_frame(ledge_state, 1.0f / 120);
        assert((ledge_runs[1] != post_before) == ran);
        runs += (unsigned)ran;
        /* An unrelated state is never held back. */
        ledge_frame(ledge_other, 1.0f / 120);
    }
    assert(runs == 21 && ledge_other_runs == 168);
    /* EndClimbingEnd from the script closes the window, and every update runs again. */
    ledge_finish_at = (int)ledge_script_step + 1;
    while (ledge_window.open) ledge_frame(ledge_state, 1.0f / 120);
    for (unsigned i = 0; i < 4; i++) assert(ledge_frame(ledge_state, 1.0f / 120));
    /* At 30 FPS nothing is skipped. */
    ledge_retire(&ledge_window);
    ledge_end_at = (int)ledge_script_step + 1;
    ledge_finish_at = -1;
    for (unsigned i = 0; i < 20; i++) assert(ledge_frame(ledge_state, 1.0f / 30));
    /* An end call outside any state update opens nothing. */
    ledge_retire(&ledge_window);
    CPU end = {.x = {[0] = ledge_controller, [1] = 0}};
    ledge_end_start(&end, 0x2553ee0);
    assert(!ledge_window.open);
    /* The pure rules: a skipped Pre whose Post never came is not applied in a later frame, a
       window expires after 2 s, and another end type of the controller retires it. */
    LedgeWindow w;
    ledge_open(&w, ledge_state, ledge_controller, 1000000000);
    assert(ledge_pre(&w, NULL, ledge_state, 1008333333, .25f, 1));
    assert(!ledge_post(&w, ledge_state, 2));
    assert(ledge_pre(&w, NULL, ledge_state, 1016666666, .25f, 3) && ledge_post(&w, ledge_state, 3));
    assert(!ledge_pre(&w, NULL, ledge_state, 3100000000ull, .25f, 4) && !w.open);
    ledge_open(&w, ledge_state, ledge_controller, 1000000000);
    ledge_end_call(&w, NULL, ledge_controller, 3, ledge_state, 1000000000);
    assert(!w.open);
    frame_step_address = 0;
}
/* Ledge smoothing through the real wrappers: the stub script reads the ride's height and asks
   for one more through the set position wrapper, as the Lua binding at 1FF6B5C does, up to 15. */
static const u64 ride_request = 0x700, ride_direction = 0x710;
static float ride_y, post_read, last_request;
static unsigned ride_sets;
static int smooth_end_at, smooth_finish_at;
static unsigned smooth_step;
static void ride_set_original(CPU* c, u64 entry) {
    (void)entry;
    ride_y = guest_float(c->x[1] + 4);
    ride_sets++;
}
static void ride_script_set(float y, u64 caller) {
    guest_float_write(ride_request + 4, y);
    CPU set = {.x = {[0] = ledge_controller, [1] = ride_request, [2] = ride_direction, [30] = caller}};
    ledge_set_position(&set, fake_main + 0x1983770);
}
static void smooth_original(CPU* c, u64 entry, unsigned post) {
    (void)entry;
    c->x[0] = 1;
    if (post) {
        post_read = ride_y;
        return;
    }
    smooth_step++;
    CPU end = {.x = {[0] = ledge_controller, [1] = 0}};
    if ((int)smooth_step == smooth_end_at) ledge_end_start(&end, 0x2553ee0);
    if (ledge_window.open && ride_y < 15) {
        last_request = ride_y + 1;
        ride_script_set(last_request, fake_main + 0x1ff6b5c);
    }
    if ((int)smooth_step == smooth_finish_at) ledge_end_finish(&end, 0x2554008);
}
static void smooth_pre(CPU* c, u64 entry) { smooth_original(c, entry, 0); }
static void smooth_post(CPU* c, u64 entry) { smooth_original(c, entry, 1); }
static int smooth_frame(float step) {
    __atomic_add_fetch(&frame_step_frames, 1, __ATOMIC_RELEASE);
    guest_float_write(frame_step_address, step);
    const unsigned before = smooth_step;
    CPU pre = {.x = {[0] = ledge_state, [30] = return_pc}};
    ledge_pre_update(&pre, 0x26163e0);
    CPU post = {.x = {[0] = ledge_state, [30] = return_pc}};
    ledge_post_update(&post, 0x2616ab0);
    return smooth_step != before;
}
static void smooth_start(void) {
    memset(memory, 0, sizeof memory);
    ledge_retire(&ledge_window);
    ledge_flush.valid = 0;
    frame_step_address = 0xc00;
    main_base = fake_main;
    gw32(ledge_state + 0x6c, 7);
    guest_float_write(ride_direction + 8, 1);
    ledge_originals[0] = smooth_pre;
    ledge_originals[1] = smooth_post;
    ledge_end_originals[0] = ledge_end_original;
    ledge_end_originals[1] = ledge_end_original;
    ledge_set_position_original = ride_set_original;
    ride_y = 0;
    ride_sets = 0;
    smooth_step = 0;
    smooth_end_at = 1;
    smooth_finish_at = -1;
}
static void test_ledge_smoothing(void) {
    smooth_start();
    float last = 0;
    unsigned moved = 0, frames = 0;
    for (unsigned guard = 0; smooth_step < 30 && guard < 400; guard++) {
        const int ran = smooth_frame(1.0f / 120);
        /* A running Post reads the script's own latest request, never the delayed point. */
        if (ran && smooth_step > 1 && ride_y < 15) assert(post_read == last_request);
        assert(ride_y >= last);
        /* While the ride is on its way up it moves on most frames, not only every fourth. */
        if (last_request < 15 && smooth_step > 2) {
            moved += ride_y != last;
            frames++;
        }
        last = ride_y;
    }
    assert(frames > 40 && moved * 4 >= frames * 3);
    /* The skipped frames are placed through the original setter, not only the script's runs. */
    assert(ride_sets > 2 * smooth_step);
    /* The script finishes inside a run while the ride is a step behind: it settles on 15. */
    smooth_finish_at = (int)smooth_step + 1;
    while (ledge_window.open) smooth_frame(1.0f / 120);
    assert(ride_y == 15 && !ledge_flush.valid);
    /* At 30 FPS the ride stands on each request at once. */
    smooth_start();
    for (unsigned i = 0; i < 10; i++) {
        smooth_frame(1.0f / 30);
        if (i) assert(ride_y == last_request && post_read == last_request);
    }
    /* Movement from anything but the script ends the steering and is kept. */
    smooth_start();
    for (unsigned i = 0; i < 12; i++) smooth_frame(1.0f / 120);
    assert(ledge_window.open);
    ride_script_set(50, fake_main + 0x1234);
    assert(!ledge_window.open && ride_y == 50);
    ledge_set_position_original = NULL;
    main_base = 0;
    frame_step_address = 0;
}
int main(void) {
    test_timer_carry();
    test_ai_tick();
    test_turn();
    test_air_steering();
    const char* disabled[] = {"SNX_ARCEUS_TARGET_FPS", "SNX_ARCEUS_FPS60", "SNX_ARCEUS_CLOCK_TRACE",
        "SNX_ARCEUS_DIALOGUE_TRACE", "SNX_ARCEUS_FIXED_RESOLUTION", "SNX_ARCEUS_CAMERA_TRACE",
        "SNX_ARCEUS_TIMING_RESEARCH"};
    for (unsigned i = 0; i < sizeof disabled / sizeof disabled[0]; i++) flag(disabled[i], "");
    test_sequence();
    test_diagnostics();
    test_climb_adrp();
    test_climb_stop();
    test_wall_jump();
    test_ledge();
    test_ledge_smoothing();
    test_target();
    puts("arceus_clock: all checks passed");
    return 0;
}
