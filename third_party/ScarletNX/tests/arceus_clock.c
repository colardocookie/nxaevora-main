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
int main(void) {
    const char* disabled[] = {"SNX_ARCEUS_TARGET_FPS", "SNX_ARCEUS_FPS60", "SNX_ARCEUS_CLOCK_TRACE",
        "SNX_ARCEUS_DIALOGUE_TRACE", "SNX_ARCEUS_FIXED_RESOLUTION", "SNX_ARCEUS_CAMERA_TRACE",
        "SNX_ARCEUS_TIMING_RESEARCH"};
    for (unsigned i = 0; i < sizeof disabled / sizeof disabled[0]; i++) flag(disabled[i], "");
    test_sequence();
    test_diagnostics();
    test_climb_adrp();
    test_target();
    puts("arceus_clock: all checks passed");
    return 0;
}
