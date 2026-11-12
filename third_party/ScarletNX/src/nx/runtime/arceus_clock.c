/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/runtime/arceus_clock.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/options.h"
#include "nx/runtime/arceus_step.h"
#include "nx/runtime/arceus_camera.h"
#include "nx/runtime/guest_jit.h"
#include "nx/memory/mem.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GuestFn originals[3];
static u64 counts[3];
static u64 main_base;
static int experimental, tracing, research_observing;
static u64 ui_clock_research_calls;
static ArceusStep step;
static PMutex step_lock = PMUTEX_INIT;
static GuestFn message_original;
static u64 message_count;
static u64 water_step_address;
static int clocks_installed;
static u64 blink_remainder;
static unsigned blink_ticks;

static void water_step_write(u64 ns) {
    if (!water_step_address) return;
    float seconds = (float)((double)ns / 1000000000.0);
    u32 bits;
    memcpy(&bits, &seconds, sizeof bits);
    gw32(water_step_address, bits);
}

/* Guest cell for world_timers_init: +0 holds min(1/30 s, frame step) and +4 its
   negation. Patched sites load it in place of their authored +/-1/30 constants. */
#define NOMINAL_STEP_SECONDS (1.0f / 30.0f)
static u64 frame_step_address;
static void frame_step_write(u64 ns) {
    if (!frame_step_address) return;
    float seconds = ns ? (float)((double)ns / 1000000000.0) : NOMINAL_STEP_SECONDS;
    if (!(seconds < NOMINAL_STEP_SECONDS)) seconds = NOMINAL_STEP_SECONDS;
    const float negative = -seconds;
    u32 bits;
    memcpy(&bits, &seconds, sizeof bits);
    gw32(frame_step_address, bits);
    memcpy(&bits, &negative, sizeof bits);
    gw32(frame_step_address + 4, bits);
}

static void adjust_duration(CPU* c) {
    static const u64 callers[4] = {0x278150c, 0x2781540, 0x2781574, 0x27815a8};
    unsigned channel = (unsigned)c->x[1];
    u64 value;
    if (!experimental || c->x[1] > 3 || c->x[30] != main_base + callers[channel]) return;
    if (c->x[2] != 33333334) {
        WARN_LIMIT(4, "arceus-clock: unexpected main-loop duration %llu, left unchanged\n", (unsigned long long)c->x[2]);
        return;
    }
    pm_lock(&step_lock);
    if (arceus_step(&step, plat_ns(), c->x[0], channel, &value)) {
        c->x[2] = value;
        if (channel == 0) {
            water_step_write(value);
            frame_step_write(value);
            /* Shared 30-Hz cadence for the isolated integer blink state machine. */
            blink_remainder += value * 30;
            blink_ticks = (unsigned)(blink_remainder / 1000000000);
            blink_remainder %= 1000000000;
        }
    }
    else WARN_LIMIT(4, "arceus-clock: clock update order changed, left unchanged\n");
    pm_unlock(&step_lock);
}

static int readable(u64 address, u64 size) {
    MemBlock block;
    u64 end = address + size;
    if (address < AS_BASE || end < address || end > AS_SIZE) return 0;
    while (address < end) {
        mem_query(address, &block);
        if (!(block.perm & PERM_R) || block.addr + block.size <= address) return 0;
        address = block.addr + block.size;
    }
    return 1;
}

static void trace_call(CPU* c, u64 entry, unsigned kind) {
    u64 object = c->x[0], channel = c->x[1], input = c->x[2], caller = c->x[30];
    u64 count = __atomic_add_fetch(&counts[kind], 1, __ATOMIC_RELAXED);
    if (kind == 2) adjust_duration(c);
    originals[kind](c, entry);
    /* At this exact call site X19 is the UI animation owner and X21 is its
       signed clock-mode index; both are callee-saved by the clock getter.
       Observe the real denominator, without altering animation progress. */
    if (research_observing && kind == 0 && caller == main_base + 0x2b4344c && c->pc == caller) {
        const u64 ui_count = __atomic_add_fetch(&ui_clock_research_calls, 1, __ATOMIC_RELAXED);
        if ((ui_count <= 8 || ui_count % 600 == 0) && readable(c->x[0], 16)) {
            const u64 mode = c->x[21], table = main_base + 0x39848e0;
            const u64 nominal = mode < 5 && readable(table + mode * 8, 8) ? gr64(table + mode * 8) : 0;
            LOG("arceus-ui-clock: count=%llu ns=%llu object=%llx channel=%llu mode=%lld duration_ns=%llu frame_ns=%llu\n",
                (unsigned long long)ui_count, (unsigned long long)plat_ns(),
                (unsigned long long)c->x[19], (unsigned long long)channel, (long long)mode,
                (unsigned long long)gr64(c->x[0] + 8), (unsigned long long)(nominal ? nominal - 1 : 0));
        }
    }
    if (tracing && (count <= 40 || count % (kind == 0 ? 60000 : 600) == 0)) {
        u64 record = kind == 0 ? c->x[0] : object + 8 + channel * 16;
        u64 duration = readable(record, 16) ? gr64(record + 8) : 0;
        LOG("arceus-clock: kind=%u count=%llu ns=%llu object=%llx channel=%llu input=%llu duration=%llu caller=main+%llx\n",
            kind, (unsigned long long)count, (unsigned long long)plat_ns(),
            (unsigned long long)object, (unsigned long long)channel, (unsigned long long)input,
            (unsigned long long)duration, (unsigned long long)(caller - main_base));
    }
}
static void read_clock(CPU* c, u64 entry) { trace_call(c, entry, 0); }
static void set_mode(CPU* c, u64 entry) { trace_call(c, entry, 1); }
static void set_duration(CPU* c, u64 entry) { trace_call(c, entry, 2); }

/* Candidate MessageView update, identified through the MessageView string references
   and its vtable. State meanings are not established: observe, never alter them. */
static void message_view_probe(CPU* c, u64 entry) {
    u64 object = c->x[0], caller = c->x[30], begin = plat_ns();
    u64 count = __atomic_add_fetch(&message_count, 1, __ATOMIC_RELAXED);
    u32 before = readable(object, 0xf8) ? gr32(object + 0xf0) : ~0u;
    message_original(c, entry);
    u32 after = readable(object, 0xf8) ? gr32(object + 0xf0) : ~0u;
    if (count <= 10 || before != after || count % 60 == 0)
        LOG("arceus-message: count=%llu ns=%llu object=%llx state_before=%u state_after=%u caller=main+%llx\n",
            (unsigned long long)count, (unsigned long long)begin,
            (unsigned long long)object, before, after, (unsigned long long)(caller - main_base));
}

/* The default probe only observes virtual calls. The optional experiment changes the four verified
   main-loop duration writes. Build and instruction guards precede both hooks and the 60 FPS pacing override. */
void arceus_clock_init(void) {
    static const u8 id[32] = {0xae,0xe8,0xf1,0x50,0xdd,0xa1,0xb5,0xa8,0x38,0x80,0x6e,0x1a,0x5e,0xa6,0x82,0x7a,
                              0xd9,0xf3,0xc5,0x1e,0,0,0,0,0,0,0,0,0,0,0,0};
    static const u64 offsets[3] = {0x278303c, 0x2783048, 0x277bef4};
    static const u32 words[3] = {0x8b21d008, 0xf0008fc8, 0x8b21d008};
    static const GuestFn wrappers[3] = {read_clock, set_mode, set_duration};
    const char* enabled = getenv("SNX_ARCEUS_CLOCK_TRACE");
    const char* fps60 = getenv("SNX_ARCEUS_FPS60");
    const char* target = getenv("SNX_ARCEUS_TARGET_FPS");
    int target_fps = fps60 && !strcmp(fps60, "1") ? 60 : 0, unlocked = 0;
    if (target && *target) {
        if (!strcmp(target, "60")) target_fps = 60;
        else if (!strcmp(target, "120")) target_fps = 120;
        /* No frame limit: every fix follows the measured step, whatever the rate. */
        else if (!strcmp(target, "unlocked") || !strcmp(target, "0")) unlocked = 1;
        else {
            WARN("arceus-clock: refused unsupported target FPS (use 60, 120 or unlocked)\n");
            return;
        }
    }
    const char* message_flag = getenv("SNX_ARCEUS_DIALOGUE_TRACE");
    int message_trace = message_flag && !strcmp(message_flag, "1");
    const SnxModInfo* mod = NULL;
    tracing = enabled && !strcmp(enabled, "1");
    experimental = target_fps != 0 || unlocked;
    const char *fixed = getenv("SNX_ARCEUS_FIXED_RESOLUTION");
    const char *camera = getenv("SNX_ARCEUS_CAMERA_TRACE");
    const char *research = getenv("SNX_ARCEUS_TIMING_RESEARCH");
    if (!tracing && !experimental && !message_trace && !(fixed && !strcmp(fixed, "1")) &&
        !(camera && !strcmp(camera, "1")) && !(research && !strcmp(research, "1"))) return;
    for (u32 i = 0; g_mod && i < g_mod->nmods; i++)
        if (!strcmp(g_mod->mods[i].name, "main")) mod = &g_mod->mods[i];
    if (!mod || memcmp(mod->build_id, id, sizeof id)) {
        WARN("arceus-clock: refused unsupported main build\n");
        return;
    }
    main_base = mod->base;
    if (message_trace) {
        u64 pc = main_base + 0x120ae38;
        if (pc >= g_code_lo && pc < g_code_hi && readable(pc, 4) && gr32(pc) == 0xa9ba6ffc &&
            g_disp[(pc - g_code_lo) / 4]) {
            message_original = g_disp[(pc - g_code_lo) / 4];
            g_disp[(pc - g_code_lo) / 4] = message_view_probe;
            LOG("arceus-message: read-only candidate MessageView probe installed\n");
        } else WARN("arceus-message: refused missing or changed MessageView entry\n");
    }
    if (!tracing && !experimental) return;
    for (unsigned i = 0; i < 3; i++) {
        u64 pc = mod->base + offsets[i];
        if (pc < g_code_lo || pc >= g_code_hi || !readable(pc, 4) || gr32(pc) != words[i] ||
            !g_disp[(pc - g_code_lo) / 4]) {
            WARN("arceus-clock: refused missing or changed clock entry %u\n", i);
            return;
        }
        originals[i] = g_disp[(pc - g_code_lo) / 4];
    }
    main_base = mod->base;
    for (unsigned i = 0; i < 3; i++)
        g_disp[(main_base + offsets[i] - g_code_lo) / 4] = wrappers[i];
    if (experimental) {
        /* Unlocked starts from a 1/120 s step until the first measured one. 0 lifts the limit. */
        step.initial_ns = unlocked ? ARCEUS_UNLOCKED_INITIAL_STEP_NS
                                   : (UINT64_C(1000000000) + target_fps / 2) / target_fps;
        __atomic_store_n(&g_opt_fps, unlocked ? 0 : target_fps, __ATOMIC_RELAXED);
        clocks_installed = 1;
    }
    if (unlocked)
        LOG("arceus-clock: Arceus 1.1.1 EXPERIMENT: elapsed-time clocks, target_fps=unlocked, tracing=%d\n", tracing);
    else
        LOG("arceus-clock: Arceus 1.1.1 %s, target_fps=%d, tracing=%d\n",
            experimental ? "EXPERIMENT: elapsed-time clocks" : "read-only probe", target_fps, tracing);
}

/* WaterCurrentTime's verified update adds 1/30 second per rendered frame. Replace
   only its two constant-building instructions with a load of the shared elapsed
   seconds. Mark the containing AOT function stale so direct/inlined calls also
   execute the changed instructions through the runtime's existing JIT path. */
static void water_fix_init(void) {
    u64 pc = main_base + 0x29faf8c;
    if (!readable(pc, 8) || gr32(pc) != 0x52911128 || gr32(pc + 4) != 0x72a7a108 ||
        !g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) {
        WARN("arceus-water: refused instruction or AOT invalidation guard\n");
        return;
    }
    int covered = 0;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] <= pc && g_mod->fn_lo[i] + g_mod->fn_len[i] >= pc + 8) covered = 1;
    if (!covered) {
        WARN("arceus-water: refused patch without AOT function coverage\n");
        return;
    }
    u64 address = mem_find_free(main_base + 0x5000000, main_base + 0x40000000, 4096, 4096);
    if (!address || mem_map(address, 4096, MS_STATIC, PERM_RW)) {
        WARN("arceus-water: could not allocate elapsed-time cell\n");
        return;
    }
    water_step_address = address;
    water_step_write(step.initial_ns);
    /* ADRP X8, cell; LDR W8,[X8,#0]. The cell is page aligned and within ADRP range. */
    u64 pages = ((address >> 12) - (pc >> 12)) & 0x1fffff;
    u32 adrp = 0x90000008 | (u32)((pages & 3) << 29) | (u32)((pages >> 2) << 5);
    gw32(pc, adrp);
    gw32(pc + 4, 0xb9400108);
    jit_code_changed(pc, 8);
    LOG("arceus-water: elapsed-time increment installed, cell=%llx\n", (unsigned long long)address);
}

static void effects_fix_init(void) {
    u64 pc = main_base + 0x2b90040;
    /* The original converts ns/16666666.0 to an integer and back to float.
       At high FPS that drops the entire update. The downstream effect-group
       calculation accepts float progress, so preserve the fractional value. */
    if (!readable(pc, 16) || gr32(pc) != 0x1e780008 || gr32(pc + 4) != 0xbd402280 ||
        gr32(pc + 8) != 0x1e220101 || gr32(pc + 12) != 0x1e210808 ||
        !g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) {
        WARN("arceus-effects: refused instruction or AOT invalidation guard\n");
        return;
    }
    int covered = 0;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] <= pc && g_mod->fn_lo[i] + g_mod->fn_len[i] >= pc + 16) covered = 1;
    if (!covered) {
        WARN("arceus-effects: refused patch without AOT function coverage\n");
        return;
    }
    gw32(pc, 0x1e624001); /* FCVT S1,D0 instead of FCVTZS W8,D0 */
    gw32(pc + 8, 0xd503201f); /* NOP instead of SCVTF S1,W8; W8 is overwritten before its next use */
    jit_code_changed(pc, 12);
    LOG("arceus-effects: fractional effect-group progress installed\n");
}


/* EventCameraMove keeps a nominal per-30-Hz-update increment at +4b4.
   Its update only reads that field and never destroys the object. Preserve the
   nominal rate between calls; scale only the value seen by this update. */
static GuestFn event_camera_original;
static u64 event_camera_count;
static void event_camera_update(CPU* c, u64 entry) {
    u64 object = c->x[0];
    MemBlock block;
    if (!readable(object, 0x4c0) || gr32(object + 0x3e4) != 1) {
        event_camera_original(c, entry);
        return;
    }
    mem_query(object + 0x4b4, &block);
    if (!(block.perm & PERM_W) || block.addr + block.size < object + 0x4b8) {
        event_camera_original(c, entry);
        return;
    }
    u32 nominal_bits = gr32(object + 0x4b4), adjusted_bits;
    float nominal, adjusted;
    memcpy(&nominal, &nominal_bits, sizeof nominal);
    pm_lock(&step_lock);
    u64 ns = step.step ? step.step : step.initial_ns;
    pm_unlock(&step_lock);
    adjusted = (float)((double)nominal * (double)ns / 33333334.0);
    memcpy(&adjusted_bits, &adjusted, sizeof adjusted_bits);
    gw32(object + 0x4b4, adjusted_bits);
    event_camera_original(c, entry);
    gw32(object + 0x4b4, nominal_bits);
    u64 count = __atomic_add_fetch(&event_camera_count, 1, __ATOMIC_RELAXED);
    if (count <= 8 || count % 120 == 0)
        LOG("arceus-event-camera: count=%llu step_ns=%llu nominal=%g adjusted=%g\n",
            (unsigned long long)count, (unsigned long long)ns, (double)nominal, (double)adjusted);
}

static void event_camera_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_EVENT_CAMERA_FIX");
    if (!enabled || strcmp(enabled, "1")) return;
    u64 pc = main_base + 0x19b2140;
    if (pc < g_code_lo || pc + 0x198 > g_code_hi || !readable(pc, 0x198) ||
        gr32(pc) != 0xd101c3ff || gr32(pc + 0x34) != 0xbd44b400 ||
        gr32(pc + 0x44) != 0x1e212800 || gr32(pc + 0x194) != 0x17ffffb7 ||
        !g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) {
        WARN("arceus-event-camera: refused instruction or AOT invalidation guard\n");
        return;
    }
    int covered = 0;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] >= 0x198) covered = 1;
    if (!covered) {
        WARN("arceus-event-camera: refused missing AOT function coverage\n");
        return;
    }
    /* Equivalent B -> CBZ XZR at the final back edge prevents a hash-matched
       original AOT variant being selected after invalidation. Both instructions
       always branch to pc+0x70. Invalidate direct/inlined AOT callers, compile
       this equivalent body, then install the wrapper before guest threads run.
       Compilation failure leaves normal JIT dispatch without the correction. */
    gw32(pc + 0x194, 0xb4fff6ff);
    jit_code_changed(pc, 0x198);
    event_camera_original = jit_wait(pc);
    if (!event_camera_original) {
        WARN("arceus-event-camera: original body did not compile; correction not installed\n");
        return;
    }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], event_camera_update, __ATOMIC_RELEASE);
    LOG("arceus-event-camera: experimental elapsed-time increment installed\n");
}


static GuestFn blink_original[3];
static u64 blink_calls[3], blink_updates[3];
static void blink_update(CPU* c, u64 entry, unsigned kind) {
    pm_lock(&step_lock);
    unsigned ticks = blink_ticks;
    pm_unlock(&step_lock);
    u64 object = c->x[0], return_pc = c->x[30];
    u64 count = __atomic_add_fetch(&blink_calls[kind], 1, __ATOMIC_RELAXED);
    if (!ticks) c->pc = return_pc;
    for (unsigned i = 0; i < ticks; i++) {
        c->x[0] = object;
        c->x[30] = return_pc;
        c->pc = entry;
        blink_original[kind](c, entry);
        __atomic_add_fetch(&blink_updates[kind], 1, __ATOMIC_RELAXED);
        if (c->pc != return_pc) break;
    }
    if (count <= 4 || count % 12000 == 0)
        LOG("arceus-blink: kind=%u calls=%llu updates=%llu ticks=%u\n", kind,
            (unsigned long long)count,
            (unsigned long long)__atomic_load_n(&blink_updates[kind], __ATOMIC_RELAXED), ticks);
}
static void blink_primary(CPU* c, u64 entry) { blink_update(c, entry, 0); }
static void blink_secondary(CPU* c, u64 entry) { blink_update(c, entry, 1); }

static void blink_inlined(CPU* c, u64 entry) { blink_update(c, entry, 2); }

/* Three entries implement the same blink countdown, with the second
   accepting the base subobject at +48. Only this small state machine is paced;
   the skeletal/face animation system continues to update every rendered frame. */
static void blink_fix_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_BLINK_FIX");
    if (!enabled || strcmp(enabled, "1")) return;
    const u64 offsets[3] = {0x194f110, 0x194f20c, 0x1aaf0b4};
    const u32 sizes[3] = {0xfc, 0x100, 0x2a0};
    const u32 prologues[3] = {0xf81e0ff3, 0xa9be4ff4, 0xa9ba6ffc};
    const u64 decrements[3] = {0x194f150, 0x194f250, 0x1aaf12c};
    const u32 tails[3] = {0x17ffffd5, 0x17ffffd5, 0x17ffffe7};
    const u32 replacements[3] = {0xb4fffabf, 0xb4fffabf, 0xb4fffcff};
    const GuestFn wrappers[3] = {blink_primary, blink_secondary, blink_inlined};
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (unsigned kind = 0; kind < 3; kind++) {
        u64 pc = main_base + offsets[kind];
        u64 tail = pc + sizes[kind] - 4;
        int covered = 0;
        for (u32 i = 0; i < g_mod->nfns; i++)
            if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == sizes[kind]) covered = 1;
        if (!covered || pc < g_code_lo || tail >= g_code_hi || !readable(pc, sizes[kind]) ||
            gr32(pc) != prologues[kind] || gr32(tail) != tails[kind] ||
            gr32(main_base + decrements[kind]) != 0x71000508) {
            WARN("arceus-blink: refused instruction/function guard for kind=%u\n", kind);
            continue;
        }
        /* Equivalent CBZ XZR in place of the final B, avoiding original AOT variants. */
        gw32(tail, replacements[kind]);
        jit_code_changed(pc, sizes[kind]);
        blink_original[kind] = jit_wait(pc);
        if (!blink_original[kind]) {
            WARN("arceus-blink: could not compile kind=%u; correction not installed\n", kind);
            continue;
        }
        __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], wrappers[kind], __ATOMIC_RELEASE);
        LOG("arceus-blink: 30-Hz countdown experiment installed, kind=%u\n", kind);
    }
}


static GuestFn camera_input_original;
static u64 camera_input_calls;
static int camera_trace_only;
static float camera_speed = 1.0f;
static float guest_float(u64 address) {
    u32 bits = gr32(address); float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}
static void guest_float_write(u64 address, float value) {
    u32 bits; memcpy(&bits, &value, sizeof bits); gw32(address, bits);
}
/* Fractional authored updates preserve the original stick response and its
   steady-state value. Scaling only the acceleration coefficients would change
   the equilibrium for partial stick input, which also multiplies the state. */
static void camera_input_update(CPU* c, u64 entry) {
    u64 object = c->x[0], parameters = c->x[1], return_pc = c->x[30];
    if (!readable(object, 0x444)) { camera_input_original(c, entry); return; }
    pm_lock(&step_lock);
    u64 ns = step.step ? step.step : step.initial_ns;
    pm_unlock(&step_lock);
    if (camera_trace_only) ns = 33333334;
    double remaining = (double)ns / 33333334.0;
    float raw_yaw = guest_float(object + 0x38c), raw_pitch = guest_float(object + 0x390);
    float deadzone = readable(parameters, 4) ? guest_float(parameters) : 0.0f;
    int active = (gr32(object + 0x380) & 0xff) != 0;
    do {
        float old_yaw = guest_float(object + 0x434), old_pitch = guest_float(object + 0x438);
        c->x[0] = object; c->x[1] = parameters; c->x[30] = return_pc; c->pc = entry;
        camera_input_original(c, entry);
        if (c->pc != return_pc) return;
        if (remaining < 1.0) {
            float yaw = guest_float(object + 0x434), pitch = guest_float(object + 0x438);
            /* The authored release is a linear subtract followed by a clamp.
               Interpolating the already-clamped result leaves an exponential
               tail. Full-stick saturation has the corresponding upper-end bug. */
            float old[2] = {old_yaw, old_pitch}, next[2] = {yaw, pitch};
            float raw[2] = {raw_yaw, raw_pitch};
            for (unsigned axis = 0; axis < 2; axis++) {
                float value = arceus_camera_fraction(old[axis], next[axis], raw[axis], deadzone, active,
                    guest_float(object + 0x424 + axis * 8), guest_float(object + 0x428 + axis * 8), remaining);
                guest_float_write(object + 0x434 + axis * 4, value);
            }
        }
        remaining -= 1.0;
    } while (remaining > 0.0);
    u64 count = __atomic_add_fetch(&camera_input_calls, 1, __ATOMIC_RELAXED);
    if (count <= 8 || count % 60 == 0)
        LOG("arceus-camera-input: count=%llu step_ns=%llu yaw=%g pitch=%g ns=%llu object=%llx caller=main+%llx raw=%g,%g deadzone=%g accel=%g,%g decel=%g,%g\n",
            (unsigned long long)count, (unsigned long long)ns,
            (double)guest_float(object + 0x434), (double)guest_float(object + 0x438),
            (unsigned long long)plat_ns(), (unsigned long long)object,
            (unsigned long long)(return_pc - main_base), (double)raw_yaw, (double)raw_pitch, (double)deadzone,
            (double)guest_float(object + 0x424), (double)guest_float(object + 0x42c),
            (double)guest_float(object + 0x428), (double)guest_float(object + 0x430));
}
static void camera_input_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_CAMERA_INPUT_FIX");
    if ((!enabled || strcmp(enabled, "1")) && !camera_trace_only) return;
    u64 pc = main_base + 0x193c04c;
    int covered = 0;
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == 0x278) covered = 1;
    if (!covered || pc < g_code_lo || pc + 0x278 > g_code_hi || !readable(pc, 0x278) ||
        gr32(pc) != 0xfc1b0fec || gr32(pc + 0x130) != 0x1e222862 ||
        gr32(pc + 0x274) != 0x17ffffee) {
        WARN("arceus-camera-input: refused instruction/function guard\n"); return;
    }
    gw32(pc + 0x274, 0xb4fffddf); /* Equivalent CBZ XZR, pc+22c. */
    jit_code_changed(pc, 0x278);
    camera_input_original = jit_wait(pc);
    if (!camera_input_original) { WARN("arceus-camera-input: original body did not compile\n"); return; }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], camera_input_update, __ATOMIC_RELEASE);
    LOG("arceus-camera-input: response hook installed, read_only=%d\n", camera_trace_only);
}


/* The field camera's two manual-rotation callers multiply by this settings
   rate once per update. Pace that angular integration as well as the response
   above; otherwise full-stick turning still runs at the render frequency. */
static GuestFn camera_rate_original;
static u64 camera_rate_calls;
static float camera_last_authored = -1.0f;
static void camera_rate_update(CPU* c, u64 entry) {
    u64 return_pc = c->x[30], camera_object = c->x[19];
    camera_rate_original(c, entry);
    if (c->pc != return_pc) return;
    pm_lock(&step_lock);
    u64 ns = step.step ? step.step : step.initial_ns;
    pm_unlock(&step_lock);
    float authored = c->v[0].f[0];
    if (!camera_trace_only) c->v[0].f[0] = (float)(authored * camera_speed * ((double)ns / 33333334.0));
    u64 count = __atomic_add_fetch(&camera_rate_calls, 1, __ATOMIC_RELAXED);
    int changed = authored != camera_last_authored;
    camera_last_authored = authored;
    if (changed || count <= 8 || count % 60 == 0) {
        LOG("arceus-camera-rate: count=%llu step_ns=%llu authored=%g scaled=%g ns=%llu caller=main+%llx speed=%g\n",
            (unsigned long long)count, (unsigned long long)ns,
            (double)authored, (double)c->v[0].f[0], (unsigned long long)plat_ns(),
            (unsigned long long)(return_pc - main_base), (double)camera_speed);
        if (return_pc == main_base + 0x193d270 && readable(camera_object, 0x70c))
            LOG("arceus-camera-field: object=%llx yaw=%g yaw_step=%g pitch_step=%g\n",
                (unsigned long long)camera_object, (double)guest_float(camera_object + 0x360),
                (double)guest_float(camera_object + 0x420), (double)guest_float(camera_object + 0x708));
    }
}
static void camera_rate_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_CAMERA_INPUT_FIX");
    if ((!enabled || strcmp(enabled, "1")) && !camera_trace_only) return;
    const char* speed = getenv("SNX_ARCEUS_CAMERA_SPEED");
    if (speed && *speed) {
        char* end; float value = strtof(speed, &end);
        if (*end || !isfinite(value) || value < 0.25f || value > 4.0f)
            WARN("arceus-camera-rate: invalid camera speed, using 1\n");
        else camera_speed = value;
    }
    u64 pc = main_base + 0x193c2c4;
    int covered = 0;
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == 0xea8) covered = 1;
    if (!covered || pc < g_code_lo || pc + 0xea8 > g_code_hi || !readable(pc, 0xea8) ||
        gr32(pc) != 0xfc1d0fe8 || gr32(pc + 0xea4) != 0x17fffec1) {
        WARN("arceus-camera-rate: refused instruction/function guard\n"); return;
    }
    gw32(pc + 0xea4, 0xb4ffd83f); /* Equivalent CBZ XZR to pc+9a8. */
    jit_code_changed(pc, 0xea8);
    camera_rate_original = jit_wait(pc);
    if (!camera_rate_original) { WARN("arceus-camera-rate: original body did not compile\n"); return; }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], camera_rate_update, __ATOMIC_RELEASE);
    LOG("arceus-camera-rate: rotation hook installed, read_only=%d speed=%g\n", camera_trace_only, (double)camera_speed);
}

/* This collector updates held/pressed/released masks every render. Only its
   autorepeat countdown is authored in 30-Hz ticks. Do not throttle the collector
   itself: that would lose short presses and slow analog movement/camera input. */
static GuestFn pad_repeat_original;
static u64 pad_repeat_calls;
static void pad_repeat_update(CPU* c, u64 entry) {
    u64 object = c->x[0], return_pc = c->x[30];
    pm_lock(&step_lock);
    unsigned ticks = blink_ticks;
    pm_unlock(&step_lock);
    pad_repeat_original(c, entry);
    if (c->pc != return_pc || !readable(object, 0x104)) return;
    u32 counter = gr32(object + 0x18);
    u64 repeat = gr64(object + 0xb8);
    if (!ticks) {
        /* Undo exactly the one decrement executed by the original collector,
           including a countdown it just initialized after a direction change.
           +A8 is the separate fresh-press mask and remains untouched. */
        gw32(object + 0x18, counter + 1);
        gw64(object + 0xb8, 0);
    }
    /* At most one repeat per render even after a stall: no catch-up burst. */
    u64 count = __atomic_add_fetch(&pad_repeat_calls, 1, __ATOMIC_RELAXED);
    if (count <= 8 || count % 600 == 0)
        LOG("arceus-pad-repeat: count=%llu ticks=%u object=%llx initial=%u interval=%u counter=%u pressed=%llx repeat_before=%llx repeat_after=%llx\n",
            (unsigned long long)count, ticks, (unsigned long long)object,
            gr32(object+0x10), gr32(object+0x14), gr32(object+0x18),
            (unsigned long long)gr64(object+0xa8), (unsigned long long)repeat,
            (unsigned long long)gr64(object+0xb8));
}
static void pad_repeat_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_MENU_REPEAT_FIX");
    if (!enabled || strcmp(enabled,"1")) return;
    u64 pc = main_base + 0xc9a9b4;
    int covered = 0;
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i=0; i<g_mod->nfns; i++)
        if (g_mod->fn_lo[i]==pc && g_mod->fn_len[i]==0x8e8) covered=1;
    if (!covered || pc<g_code_lo || pc+0x8e8>g_code_hi || !readable(pc,0x8e8) ||
        gr32(pc)!=0x6db923e9 || gr32(main_base+0xc9ac44)!=0x51000530 ||
        gr32(pc+0x8e4)!=0x17ffffea) {
        WARN("arceus-pad-repeat: refused instruction/function guard\n"); return;
    }
    gw32(pc+0x8e4,0xb4fffd5f); /* Equivalent final B -> CBZ XZR. */
    jit_code_changed(pc,0x8e8);
    pad_repeat_original=jit_wait(pc);
    if (!pad_repeat_original) { WARN("arceus-pad-repeat: compile failed\n"); return; }
    __atomic_store_n(&g_disp[(pc-g_code_lo)/4],pad_repeat_update,__ATOMIC_RELEASE);
    LOG("arceus-pad-repeat: 30-Hz autorepeat countdown installed; fresh presses unchanged\n");
}

static GuestFn field_message_original;
static u64 field_message_calls;
static void field_message_probe(CPU* c, u64 entry) {
    u64 object = c->x[0];
    field_message_original(c, entry);
    u64 count = __atomic_add_fetch(&field_message_calls, 1, __ATOMIC_RELAXED);
    if (count > 8 && count % 300 != 0) return;
    u64 manager = readable(object, 0x68) ? gr64(object + 0x60) : 0;
    u64 root = readable(manager, 0x60) ? gr64(manager + 0x58) : 0;
    /* Resolve the update vtable entries of the actual live message tree. No
       guessed state offsets are modified, and input is never consumed here. */
    u64 queue[16] = {root}; unsigned tail = root ? 1 : 0;
    for (unsigned head = 0; head < tail && head < 16; head++) {
        u64 node = queue[head];
        if (!readable(node, 0xa0)) continue;
        u64 vt = gr64(node), update = readable(vt, 0x88) ? gr64(vt + 0x80) : 0;
        LOG("arceus-field-message: count=%llu node=%llx update=main+%llx\n",
            (unsigned long long)count, (unsigned long long)node, (unsigned long long)(update - main_base));
        for (unsigned i = 0; i < 2; i++) {
            u64 next = gr64(node + 0x90 + i * 8); int seen = !next;
            for (unsigned j = 0; j < tail; j++) if (queue[j] == next) seen = 1;
            if (!seen && tail < 16) queue[tail++] = next;
        }
    }
}
static void field_message_probe_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_DIALOGUE_TRACE");
    if (!enabled || strcmp(enabled, "1")) return;
    u64 pc = main_base + 0x2482818;
    int covered = 0;
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == 0xb8) covered = 1;
    if (!covered || pc < g_code_lo || pc + 0xb8 > g_code_hi || !readable(pc, 0xb8) ||
        gr32(pc) != 0xd10083ff || gr32(pc + 0xb4) != 0x17ffffda) {
        WARN("arceus-field-message: refused instruction/function guard\n"); return;
    }
    gw32(pc + 0xb4, 0xb4fffb5f); /* Equivalent CBZ XZR, back to pc+1c. */
    jit_code_changed(pc, 0xb8);
    field_message_original = jit_wait(pc);
    if (!field_message_original) { WARN("arceus-field-message: could not compile probe body\n"); return; }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], field_message_probe, __ATOMIC_RELEASE);
    LOG("arceus-field-message: read-only live message update probe installed\n");
}

/* Read-only cadence/mode observations. Do not change either physics dt here:
   a fixed-step subsystem may have its own scheduler. */
static GuestFn timing_research_original[2];
static u64 timing_research_calls[2];
static int physics_timestep_active, navigation_timestep_active;
static u32 physics_input_bits; /* Last main-world elapsed input, research probe only. */
static void cadence_census_report(u64 frames);
static void timing_research_update(CPU* c, u64 entry, unsigned kind) {
    u64 object = c->x[0], caller = c->x[30];
    u64 count = __atomic_add_fetch(&timing_research_calls[kind], 1, __ATOMIC_RELAXED);
    if (kind) {
        __atomic_store_n(&physics_input_bits, c->v[0].s[0], __ATOMIC_RELAXED);
        cadence_census_report(count);
    }
    if ((count <= 4 || count % 300 == 0) && readable(object, 0x154)) {
        unsigned mode = gr32(object + (kind ? 0x120 : 0x150));
        mode = kind ? (mode >> 8) & 255 : mode & 255;
        /* The timestep correction leaves the guest's +121 byte alone, so
           variable_mode stays 0; timestep_fix records which dt was selected. */
        LOG("arceus-timing-research: kind=%u count=%llu ns=%llu object=%llx caller=main+%llx incoming_dt=%g variable_mode=%u rate=%g timestep_fix=%d\n",
            kind, (unsigned long long)count, (unsigned long long)plat_ns(),
            (unsigned long long)object, (unsigned long long)(caller-main_base),
            (double)c->v[0].f[0], mode, kind ? (double)guest_float(object+0x124) : 1.0,
            kind ? physics_timestep_active : navigation_timestep_active);
    }
    timing_research_original[kind](c, entry);
}
static void navigation_probe(CPU* c, u64 entry) { timing_research_update(c, entry, 0); }
static void physics_probe(CPU* c, u64 entry) { timing_research_update(c, entry, 1); }

/* These probes preserve the guest's counters. In particular +88 is a script
   context's accumulated frames since its scheduler last reset it, not a global
   frame number. Compare it with the paired seconds at +84 before choosing a
   correction. The ride callbacks are separate copies of the blink countdown;
   their live cadence has not yet been measured. */
static GuestFn engine_clock_original[4];
static u64 engine_clock_calls[4];
static void engine_clock_probe(CPU* c, u64 entry, unsigned kind) {
    const u64 object = c->x[0], caller = c->x[30];
    const u64 count = __atomic_add_fetch(&engine_clock_calls[kind], 1, __ATOMIC_RELAXED);
    const int sample = count <= 8 || count % (kind ? 12000 : 600) == 0;
    const u32 field = kind == 0 ? 0x88 : kind == 1 ? 0x68c : 0x644;
    const int valid = sample && readable(object, field + 4);
    const u32 before = valid ? gr32(object + field) : 0;
    const float seconds = valid && !kind ? guest_float(object + 0x84) : 0.0f;
    const u32 flags = valid && !kind ? gr32(object + 0x80) : 0;
    const u64 now = valid ? plat_ns() : 0;
    engine_clock_original[kind](c, entry);
    if (valid) {
        const u32 after = !kind ? (u32)c->x[0] : readable(object, field + 4) ? gr32(object + field) : ~0u;
        LOG("arceus-engine-clock: kind=%u count=%llu ns=%llu object=%llx caller=main+%llx before=%u after=%u seconds=%g flags=%x\n",
            kind, (unsigned long long)count, (unsigned long long)now,
            (unsigned long long)object, (unsigned long long)(caller-main_base), before, after,
            (double)seconds, flags);
    }
}
static void script_frames_probe(CPU* c, u64 entry) { engine_clock_probe(c, entry, 0); }
static void ride_blink_probe(CPU* c, u64 entry) { engine_clock_probe(c, entry, 1); }
static void ride_blink_subobject_probe(CPU* c, u64 entry) { engine_clock_probe(c, entry, 2); }
static void physics_world_step_probe(CPU* c, u64 entry) {
    const u64 count=__atomic_add_fetch(&engine_clock_calls[3],1,__ATOMIC_RELAXED);
    const u64 object=c->x[0], caller=c->x[30];
    const int sample=(count<=8 || count%300==0) && readable(c->x[1],12);
    const u64 now=sample ? plat_ns() : 0;
    const float step=sample ? guest_float(c->x[1]) : 0.0f;
    const u32 word4=sample ? gr32(c->x[1]+4) : 0, word8=sample ? gr32(c->x[1]+8) : 0;
    engine_clock_original[3](c,entry);
    if (sample) {
        /* Observe the solver storage after task setup. The alternate-world path
           can own different data, so record its presence rather than assuming
           these fields were written on every invocation. Never alter StepInfo. */
        LOG("arceus-physics-step: count=%llu ns=%llu object=%llx caller=main+%llx step_seconds=%g raw_word4=%u raw_word8=%u\n",
            (unsigned long long)count, (unsigned long long)now,
            (unsigned long long)object, (unsigned long long)(caller-main_base),
            (double)step, word4, word8);
        if (readable(object,0x650)) {
            /* StepInfo+0x88 is the world's post-solve serial. Its modular
               progress can distinguish setup frequency from completed steps;
               it is not a body count or a lifetime identity. Observation only. */
            LOG("arceus-physics-solver: count=%llu ns=%llu object=%llx input_seconds=%g stored_seconds=%g inverse_seconds=%g substep_seconds=%g substeps=%u alternate_world=%u postsolve_serial=%u\n",
                (unsigned long long)count, (unsigned long long)now, (unsigned long long)object,
                (double)step, (double)guest_float(object+0x540), (double)guest_float(object+0x548),
                (double)guest_float(object+0x568), gr32(object+0x5a0), gr64(object+0x648)!=0,
                gr32(object+0x5b8));
            /* The single-threaded implementation solves inline before checking
               task-list counts. Empty queues therefore do not establish that
               physics was skipped. Identify the implementation without adding
               a solver hook or treating setup as completed integration. */
            const u64 simulation=gr64(object+0x4a8);
            if (readable(simulation,0x20)) {
                const u64 vt=gr64(simulation);
                const u64 solve=readable(vt,0x20) ? gr64(vt+0x18) : 0;
                const u32 tag=gr32(simulation+0x18);
                const char* path=vt==main_base+0x41b5000 && solve==main_base+0x2fe2604 && tag==0 ? "single" :
                    vt==main_base+0x41b3ac8 && solve==main_base+0x2f9158c && tag==1 ? "multi" : "unknown";
                LOG("arceus-physics-simulation: count=%llu ns=%llu object=%llx simulation=%llx vtable=%llx solve_entry=%llx tag=%u path=%s alternate_world=%u\n",
                    (unsigned long long)count,(unsigned long long)now,(unsigned long long)object,
                    (unsigned long long)simulation,(unsigned long long)vt,(unsigned long long)solve,
                    tag,path,gr64(object+0x648)!=0);
            }
        }
    }
}
static void engine_clock_research_init(void) {
    static const struct { u64 offset; u32 size, head, patch, word, replacement, extra, extra_word; } sites[4] = {
        /* LDR W0,[X0,#0x88] -> LDUR W0,[X0,#0x88], same address and result. */
        {0x1059a78,0x8,0xb9408800,0x0,0xb9408800,0xb8488000,0x4,0xd65f03c0},
        /* Same unconditional branch via CBZ XZR, so no original AOT variant is reused. */
        {0x2518ba8,0xfc,0xf81e0ff3,0xf8,0x17ffffd5,0xb4fffabf,0x40,0x71000508},
        {0x2518ca4,0xfc,0xf81e0ff3,0xf8,0x17ffffd5,0xb4fffabf,0x40,0x71000508},
        /* Havok collision task setup copies the StepInfo at X1, before solving. */
        {0x2f39630,0xed8,0xa9ba6ffc,0xed4,0x17ffffc4,0xb4fff89f,0xd4,0xf94002a8}
    };
    const GuestFn wrappers[4] = {script_frames_probe, ride_blink_probe, ride_blink_subobject_probe, physics_world_step_probe};
    for (unsigned k=0; k<4; k++) {
        const u64 pc=main_base+sites[k].offset;
        int covered=0;
        for (u32 i=0; i<g_mod->nfns; i++)
            if (g_mod->fn_lo[i]==pc && g_mod->fn_len[i]==sites[k].size) covered=1;
        if (!covered || pc<g_code_lo || pc+sites[k].size>g_code_hi || !readable(pc,sites[k].size) ||
            gr32(pc)!=sites[k].head || gr32(pc+sites[k].patch)!=sites[k].word ||
            gr32(pc+sites[k].extra)!=sites[k].extra_word) {
            WARN("arceus-engine-clock: refused instruction/function guard kind=%u\n",k);continue;
        }
        gw32(pc+sites[k].patch,sites[k].replacement);
        jit_code_changed(pc,sites[k].size);
        engine_clock_original[k]=jit_wait(pc);
        if (!engine_clock_original[k]) {
            WARN("arceus-engine-clock: compile failed kind=%u\n",k);continue;
        }
        __atomic_store_n(&g_disp[(pc-g_code_lo)/4],wrappers[k],__ATOMIC_RELEASE);
        LOG("arceus-engine-clock: observation probe installed kind=%u\n",k);
    }
}
/* Further observation-only frame counters. Throwable states share +328 but
   advance it with different authored increments; never scale the whole object
   updater, which also handles lifetime, input and transform work. */
static GuestFn frame_timer_original[5];
static u64 frame_timer_calls[5], frame_timer_changes[5];
static void frame_timer_probe(CPU* c, u64 entry, unsigned kind) {
    const u64 object=c->x[0], caller=c->x[30];
    const u32 field=kind==0 ? 0x414 : kind==1 ? 0x238 : 0x328;
    const int valid=readable(object,field+4);
    const u32 before=valid ? gr32(object+field) : 0;
    const u32 state=valid ? (kind==0 ? *GP(object+0x3f9) :
        kind==1 ? *GP(object+0x220) : gr32(object+0x320)) : 0;
    const u64 count=__atomic_add_fetch(&frame_timer_calls[kind],1,__ATOMIC_RELAXED);
    frame_timer_original[kind](c,entry);
    if (!valid || !readable(object,field+4)) return;
    const u32 after=gr32(object+field);
    if (before==after) return;
    const u64 changed=__atomic_add_fetch(&frame_timer_changes[kind],1,__ATOMIC_RELAXED);
    if (changed>8 && changed%120) return;
    double a=(int)before, b=(int)after;
    if (kind>=2) { float x,y; memcpy(&x,&before,4); memcpy(&y,&after,4); a=x; b=y; }
    LOG("arceus-frame-timer: kind=%u count=%llu changed=%llu ns=%llu object=%llx caller=main+%llx state=%u before=%g after=%g\n",
        kind,(unsigned long long)count,(unsigned long long)changed,(unsigned long long)plat_ns(),
        (unsigned long long)object,(unsigned long long)(caller-main_base),state,a,b);
}
static void recenter_timer_probe(CPU* c,u64 e) { frame_timer_probe(c,e,0); }
static void battle_idle_timer_probe(CPU* c,u64 e) { frame_timer_probe(c,e,1); }
static void throwable_one_timer_probe(CPU* c,u64 e) { frame_timer_probe(c,e,2); }
static void throwable_two_timer_probe(CPU* c,u64 e) { frame_timer_probe(c,e,3); }
static void throwable_three_timer_probe(CPU* c,u64 e) { frame_timer_probe(c,e,4); }
static void frame_timer_research_init(void) {
    static const struct { u64 offset; u32 size,head,patch,word,replacement,extra,extra_word; } sites[5]={
        /* MOV Xd,X0 -> ADD Xd,X0,#0: equivalent, flags unchanged. */
        {0x193633c,0x3c8,0xd10343ff,0x3c,0xaa0003f3,0x91000013,0x110,0x11000529},
        {0x9bad68,0x4dc,0xd10203ff,0xcc,0xaa0003f5,0x91000015,0x2c,0x71000508},
        {0x1d482b0,0xe94,0xd10483ff,0xc4,0xaa0003f4,0x91000014,0x68c,0x1e212800},
        {0x1d49144,0x770,0xd10283ff,0x58,0xaa0003f3,0x91000013,0x704,0x1e212800},
        {0x1d498b4,0x7dc,0xd102c3ff,0xe8,0xaa0003f3,0x91000013,0x654,0x1e212800}
    };
    const GuestFn wrappers[5]={recenter_timer_probe,battle_idle_timer_probe,
        throwable_one_timer_probe,throwable_two_timer_probe,throwable_three_timer_probe};
    for (unsigned k=0;k<5;k++) {
        const u64 pc=main_base+sites[k].offset;
        int covered=0;
        for (u32 i=0;i<g_mod->nfns;i++)
            if (g_mod->fn_lo[i]==pc && g_mod->fn_len[i]==sites[k].size) covered=1;
        if (!covered || pc<g_code_lo || pc+sites[k].size>g_code_hi || !readable(pc,sites[k].size) ||
            gr32(pc)!=sites[k].head || gr32(pc+sites[k].patch)!=sites[k].word ||
            gr32(pc+sites[k].extra)!=sites[k].extra_word) {
            WARN("arceus-frame-timer: refused instruction/function guard kind=%u\n",k);continue;
        }
        gw32(pc+sites[k].patch,sites[k].replacement);
        jit_code_changed(pc,sites[k].size);
        frame_timer_original[k]=jit_wait(pc);
        if (!frame_timer_original[k]) {
            WARN("arceus-frame-timer: compile failed kind=%u\n",k);continue;
        }
        __atomic_store_n(&g_disp[(pc-g_code_lo)/4],wrappers[k],__ATOMIC_RELEASE);
        LOG("arceus-frame-timer: observation probe installed kind=%u\n",k);
    }
}
/* Common fade C21490, mode 0 (linear color). Each active call adds one to frame +538 and the
   delta +550 to color +540, so fades ran 4x fast at 120 FPS. With SNX_ARCEUS_FADE_FIX a call
   adds delta x elapsed x 30, and the frame only advances once a whole 1/30 s has built up,
   by staging +538 one lower. Completion, snaps and callbacks stay the original's. */
static int writable(u64 address, u64 size);
#define FADE_SLOTS 16
static int fade_fix_active;
static PMutex fade_lock = PMUTEX_INIT;
/* generation counts the fade starts seen for the object, so a call that a start interrupted
   leaves the new fade's history and delta alone. */
static struct { u64 object; float progress; u32 frame, duration, state, generation; } fade_slots[FADE_SLOTS];
static u64 fade_calls, fade_staged, fade_full;
typedef struct { u64 object; int slot, staged_delta; u32 delta[4], staged[4], generation; float progress; } FadeStage;
static float fade_word_float(u32 bits) {
    float f;
    memcpy(&f, &bits, 4);
    return f;
}
/* The object's slot or a free one, -1 when all are in use (the call then runs as it is).
   A slot that no longer matches the fade (start, completion, other writer) starts at zero. */
static int fade_slot(u64 object, u32 frame, u32 duration, u32 state, float* progress) {
    int found = -1, free_slot = -1;
    for (int k = 0; k < FADE_SLOTS && found < 0; k++) {
        if (fade_slots[k].object == object) found = k;
        if (!fade_slots[k].object && free_slot < 0) free_slot = k;
    }
    if (found < 0) {
        if (free_slot < 0) return -1;
        found = free_slot;
        fade_slots[found].object = object;
        fade_slots[found].progress = 0.0f;
        fade_slots[found].frame = ~0u;
        fade_slots[found].generation = 0;
    }
    if (fade_slots[found].frame != frame || fade_slots[found].duration != duration ||
        fade_slots[found].state != state)
        fade_slots[found].progress = 0.0f;
    *progress = fade_slots[found].progress;
    return found;
}
static void fade_before(u64 object, FadeStage* st) {
    st->slot = -1;
    st->staged_delta = 0;
    if (!fade_fix_active || !writable(object, 0x570) || gr32(object + 0x500) != 0) return;
    const u32 state = gr32(object + 0x504);
    if (state != 2 && state != 3) return;
    /* The cell holds min(elapsed, 1/30), so want is at most 1. */
    const float want = guest_float(frame_step_address) / NOMINAL_STEP_SECONDS;
    if (!(want > 0.0f && want <= 1.0f)) return;
    const int32_t frame = (int32_t)gr32(object + 0x538), duration = (int32_t)gr32(object + 0x53c);
    /* A fade-in at its end only resets and republishes the target: leave that call alone. */
    if (frame >= duration && state == 2) return;
    pm_lock(&fade_lock);
    float progress = 0.0f;
    st->slot = fade_slot(object, (u32)frame, (u32)duration, state, &progress);
    if (st->slot >= 0) st->generation = fade_slots[st->slot].generation;
    else fade_full++;
    pm_unlock(&fade_lock);
    if (st->slot < 0) return;
    st->object = object;
    st->progress = progress + want;
    const int whole = st->progress >= 1.0f;
    for (unsigned k = 0; k < 4; k++) st->delta[k] = gr32(object + 0x550 + 4 * k);
    if (frame < duration) {
        /* Each call adds its share of the delta, only up to the end on the last frame.
           Only a call that completes a frame advances it. */
        const float share = whole && frame + 1 >= duration ? 1.0f - progress : want;
        for (unsigned k = 0; k < 4; k++) {
            const float scaled = fade_word_float(st->delta[k]) * share;
            memcpy(&st->staged[k], &scaled, 4);
        }
        if (!whole) gw32(object + 0x538, (u32)(frame - 1));
    } else {
        /* Fade-out finishes one frame after its last: hold it with a zero step until then. */
        if (whole) return;
        memset(st->staged, 0, sizeof st->staged);
        gw32(object + 0x538, (u32)(duration - 1));
    }
    for (unsigned k = 0; k < 4; k++) gw32(object + 0x550 + 4 * k, st->staged[k]);
    st->staged_delta = 1;
    __atomic_add_fetch(&fade_staged, 1, __ATOMIC_RELAXED);
}
static void fade_after(const FadeStage* st) {
    if (st->slot < 0) return;
    const u64 object = st->object;
    const u64 calls = __atomic_add_fetch(&fade_calls, 1, __ATOMIC_RELAXED);
    pm_lock(&fade_lock);
    const int current = fade_slots[st->slot].object == object &&
                        fade_slots[st->slot].generation == st->generation;
    pm_unlock(&fade_lock);
    /* A fade start during the call owns the object now: no restore, no history. */
    if (!current || !writable(object, 0x570)) return;
    if (st->staged_delta) {
        /* Restore the authored delta unless the call (or a callback) replaced it. */
        int ours = 1;
        for (unsigned k = 0; k < 4; k++)
            if (gr32(object + 0x550 + 4 * k) != st->staged[k]) ours = 0;
        if (ours)
            for (unsigned k = 0; k < 4; k++) gw32(object + 0x550 + 4 * k, st->delta[k]);
    }
    const u32 frame = gr32(object + 0x538), duration = gr32(object + 0x53c), state = gr32(object + 0x504);
    pm_lock(&fade_lock);
    if (fade_slots[st->slot].object == object && fade_slots[st->slot].generation == st->generation) {
        if (state != 2 && state != 3) {
            fade_slots[st->slot].object = 0; /* Finished: the slot is free again. */
        } else {
            fade_slots[st->slot].progress = st->progress >= 1.0f ? st->progress - 1.0f : st->progress;
            fade_slots[st->slot].frame = frame;
            fade_slots[st->slot].duration = duration;
            fade_slots[st->slot].state = state;
        }
    }
    pm_unlock(&fade_lock);
    if (calls <= 4 || calls % 1200 == 0)
        LOG("arceus-fade: calls=%llu staged=%llu full=%llu object=%llx frame=%d duration=%d state=%u progress=%g\n",
            (unsigned long long)calls, (unsigned long long)__atomic_load_n(&fade_staged, __ATOMIC_RELAXED),
            (unsigned long long)fade_full, (unsigned long long)object, (int)frame, (int)duration, state,
            (double)st->progress);
}
/* Common fades and capture stages have separate integer clocks. Capture's X0
   is its manager; X1 points to the holder of the actual sequence object.
   State transitions can reset/reload these clocks, so log both sides. */
static GuestFn sequence_clock_original[2];
static u64 sequence_clock_calls[2], sequence_clock_changes[2];
static int sequence_clock_logging, capture_fix_active;
/* Capture waiting stages 3, 5, 7, 8, 9, 14 and 16 subtract one from +1DC per call.
   With the fix, a call only counts down once 1/30 s of frame steps has built up for
   that object. Otherwise +1DC is raised by one first so the call leaves it unchanged. */
#define CAPTURE_SLOTS 8
#define CAPTURE_TICK_NS 33333333ll
static struct { u64 object; int64_t ns; } capture_slots[CAPTURE_SLOTS];
static unsigned capture_next_slot;
static int capture_waiting(u32 stage) {
    return stage == 3 || stage == 5 || stage == 7 || stage == 8 || stage == 9 || stage == 14 || stage == 16;
}
/* The director skips its body until every enabled effect handle pair is ready, the same
   checks as 180C558 to 180C614. Their start-up takes a fixed number of frames. */
static int capture_ready(u64 object) {
    static const u32 flags[4] = {0x210, 0x248, 0x280, 0x2b8}, pairs[4] = {0x1e0, 0x218, 0x250, 0x288};
    if (!readable(object, 0x2c0)) return 1;
    for (unsigned k = 0; k < 4; k++) {
        if (!*GP(object + flags[k])) continue;
        for (u32 h = pairs[k]; h <= pairs[k] + 0x10; h += 0x10) {
            const u64 p = gr64(object + h);
            if (!p || !readable(p + 0xc, 4) || !gr32(p + 0xc)) return 0;
        }
    }
    return 1;
}
/* A not-ready call counts as one whole 1/30 s frame, as it takes at 30 FPS, so the
   countdown afterwards waits until elapsed time catches up. The debt is capped at 1 s. */
static int capture_tick(u64 object, int ready) {
    pm_lock(&step_lock);
    const int64_t ns = (int64_t)(step.step ? step.step : step.initial_ns);
    pm_unlock(&step_lock);
    unsigned k = 0;
    while (k < CAPTURE_SLOTS && capture_slots[k].object != object) k++;
    if (k == CAPTURE_SLOTS) {
        k = capture_next_slot++ % CAPTURE_SLOTS;
        capture_slots[k].object = object;
        capture_slots[k].ns = 0;
    }
    capture_slots[k].ns += ready ? ns : ns - CAPTURE_TICK_NS;
    if (capture_slots[k].ns < -1000000000ll) capture_slots[k].ns = -1000000000ll;
    if (!ready || capture_slots[k].ns < CAPTURE_TICK_NS) return 0;
    capture_slots[k].ns -= CAPTURE_TICK_NS;
    if (capture_slots[k].ns >= CAPTURE_TICK_NS) capture_slots[k].ns = 0;
    return 1;
}
static void sequence_clock_probe(CPU* c, u64 entry, unsigned kind) {
    const u64 object = kind ? (readable(c->x[1], 8) ? gr64(c->x[1]) : 0) : c->x[0];
    const u64 caller = c->x[30];
    const u32 extent = kind ? 0x1e0 : 0x540;
    const u32 timer = kind ? 0x1dc : 0x538, duration = kind ? 0x1d8 : 0x53c;
    const u32 state = kind ? 0xf0 : 0x504;
    const int valid = readable(object, extent);
    const u32 before = valid ? gr32(object + timer) : 0;
    const u32 state_before = valid ? gr32(object + state) : 0;
    const u32 duration_before = valid ? gr32(object + duration) : 0;
    const u32 mode_before = valid && !kind ? gr32(object + 0x500) : 0;
    const u64 count = __atomic_add_fetch(&sequence_clock_calls[kind], 1, __ATOMIC_RELAXED);
    const u64 now = valid ? plat_ns() : 0;
    const int hold = kind && capture_fix_active && valid && before && capture_waiting(state_before) &&
                     !capture_tick(object, capture_ready(object));
    if (hold) gw32(object + timer, before + 1);
    FadeStage fade;
    fade.slot = -1;
    if (!kind && valid) fade_before(object, &fade);
    sequence_clock_original[kind](c, entry);
    if (!kind) fade_after(&fade);
    if (hold && readable(object, extent) && gr32(object + state) == state_before &&
        gr32(object + timer) == before + 1)
        gw32(object + timer, before);
    if (!sequence_clock_logging || !valid || c->pc != caller || !readable(object, extent)) return;
    const u32 after = gr32(object + timer), state_after = gr32(object + state);
    const u32 duration_after = gr32(object + duration);
    const u32 mode_after = !kind ? gr32(object + 0x500) : 0;
    const int transition = state_before != state_after || duration_before != duration_after || mode_before != mode_after;
    const int changed = before != after || transition;
    const u64 changes = changed ? __atomic_add_fetch(&sequence_clock_changes[kind], 1, __ATOMIC_RELAXED) : 0;
    if (!(transition || count <= 8 || (changed && (changes <= 8 || changes % 30 == 0)) || count % 600 == 0)) return;
    LOG("arceus-sequence-clock: kind=%u count=%llu ns=%llu object=%llx caller=main+%llx transition=%u state_before=%u state_after=%u mode_before=%u mode_after=%u duration_before=%u duration_after=%u before=%u after=%u\n",
        kind, (unsigned long long)count, (unsigned long long)now,
        (unsigned long long)object, (unsigned long long)(caller-main_base), transition,
        state_before, state_after, mode_before, mode_after, duration_before, duration_after, before, after);
}
static void fade_sequence_probe(CPU* c, u64 e) { sequence_clock_probe(c, e, 0); }
static void capture_sequence_probe(CPU* c, u64 e) { sequence_clock_probe(c, e, 1); }
static int sequence_clock_install(unsigned k) {
    static const struct { u64 offset; u32 size,head,patch,word,replacement,extra,extra_word; } sites[2]={
        /* Equivalent MOV Xd,X0 -> ADD Xd,X0,#0 selects an independent JIT body. */
        {0xc21490,0x300,0xd10143ff,0x3c,0xaa0003f3,0x91000013,0xd0,0x1100054a},
        {0x180c338,0x427c,0x6db82beb,0x30,0xaa0003f6,0x91000016,0x61c,0x71000508}
    };
    const GuestFn wrappers[2]={fade_sequence_probe,capture_sequence_probe};
    if (sequence_clock_original[k]) return 1;
    const u64 pc=main_base+sites[k].offset;
    int covered=0;
    for (u32 i=0;i<g_mod->nfns;i++)
        if (g_mod->fn_lo[i]==pc && g_mod->fn_len[i]==sites[k].size) covered=1;
    if (!covered || pc<g_code_lo || pc+sites[k].size>g_code_hi || !readable(pc,sites[k].size) ||
        gr32(pc)!=sites[k].head || gr32(pc+sites[k].patch)!=sites[k].word ||
        gr32(pc+sites[k].extra)!=sites[k].extra_word) {
        WARN("arceus-sequence-clock: refused instruction/function guard kind=%u\n",k);return 0;
    }
    gw32(pc+sites[k].patch,sites[k].replacement);
    jit_code_changed(pc,sites[k].size);
    sequence_clock_original[k]=jit_wait(pc);
    if (!sequence_clock_original[k]) {
        WARN("arceus-sequence-clock: compile failed kind=%u\n",k);return 0;
    }
    __atomic_store_n(&g_disp[(pc-g_code_lo)/4],wrappers[k],__ATOMIC_RELEASE);
    return 1;
}
static void sequence_clock_research_init(void) {
    sequence_clock_logging = 1;
    for (unsigned k=0;k<2;k++)
        if (sequence_clock_install(k)) LOG("arceus-sequence-clock: observation probe installed kind=%u\n",k);
}
/* Runs after the research probes, so it reuses the capture wrapper when they installed it. */
static void capture_fix_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_CAPTURE_FIX");
    if (!main_base || !enabled || strcmp(enabled, "1")) return;
    if (!clocks_installed) {
        WARN("arceus-capture-fix: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    if (!sequence_clock_install(1)) return;
    capture_fix_active = 1;
    LOG("arceus-capture-fix: EXPERIMENT: capture waiting stages count down at 30 per second of elapsed time\n");
}
/* Battle effect sequences (the .wbin player, update 9DBEAC) add their speed at +F0 to the
   frame at +E8 once per call, so they ran 4 times fast at 120 FPS. The fix passes speed x 30
   x elapsed, cut so the frame lands exactly on each whole frame for the exact command-end
   compares. Speeds outside (0, 4] only update once 1/30 s has built up. */
#define SEQUENCE_SLOTS 64
/* A clamped 100 ms step at speed 4 needs at most 13 boundary updates.
   Allow extra catch-up work, retaining excess budget if the guard is reached. */
#define SEQUENCE_MAX_UPDATES 32
static GuestFn battle_sequence_original;
static PMutex battle_sequence_lock = PMUTEX_INIT;
static struct { u64 object; u64 ns; float pending; } sequence_slots[SEQUENCE_SLOTS];
static unsigned sequence_next_slot;
static u64 battle_sequence_calls, battle_sequence_skips, battle_sequence_partial;
/* Read-only research: playing calls per speed, to see how many sequences still step whole frames. */
#define SEQUENCE_SPEEDS 8
static struct { float speed; u64 calls; } sequence_speeds[SEQUENCE_SPEEDS];
static u64 sequence_speed_other;
/* The largest step not past the next whole frame, nudged so frame + step rounds onto it. */
static float sequence_step(float frame, float want) {
    const float whole = floorf(frame) + 1.0f;
    if (frame + want < whole) return want;
    float step = whole - frame;
    for (unsigned i = 0; i < 4 && frame + step != whole; i++)
        step = nextafterf(step, frame + step < whole ? INFINITY : 0.0f);
    return step;
}
static int battle_sequence_playing(u64 o) {
    return readable(o, 0x308) && *GP(o + 0x302) && *GP(o + 0x301) && *GP(o + 0x303) &&
           guest_float(o + 0xf0) != 0.0f && guest_float(o + 0xe8) < guest_float(o + 0xec);
}
static void battle_sequence_update(CPU* c, u64 entry) {
    const u64 object = c->x[0];
    const u64 count = __atomic_add_fetch(&battle_sequence_calls, 1, __ATOMIC_RELAXED);
    int skip = 0;
    float speed = 0.0f, partial = 0.0f;
    unsigned slot = SEQUENCE_SLOTS;
    if (battle_sequence_playing(object)) {
        pm_lock(&step_lock);
        const u64 ns = step.step ? step.step : step.initial_ns;
        pm_unlock(&step_lock);
        speed = guest_float(object + 0xf0);
        pm_lock(&battle_sequence_lock);
        unsigned s = 0;
        while (s < SEQUENCE_SPEEDS && sequence_speeds[s].calls && sequence_speeds[s].speed != speed) s++;
        if (s == SEQUENCE_SPEEDS) sequence_speed_other++;
        else sequence_speeds[s].speed = speed, sequence_speeds[s].calls++;
        unsigned k = 0;
        while (k < SEQUENCE_SLOTS && sequence_slots[k].object != object) k++;
        if (k == SEQUENCE_SLOTS) {
            k = sequence_next_slot++ % SEQUENCE_SLOTS;
            sequence_slots[k].object = object;
            sequence_slots[k].ns = 0;
            sequence_slots[k].pending = 0.0f;
        }
        if (speed > 0.0f && speed <= 4.0f) {
            float pending = sequence_slots[k].pending + speed * (float)ns / 33333333.0f;
            partial = sequence_step(guest_float(object + 0xe8), pending);
            sequence_slots[k].pending = pending - partial;
            slot = k;
            if (partial <= 0.0f) skip = 1, partial = 0.0f;
        } else {
            sequence_slots[k].ns += ns;
            if (sequence_slots[k].ns < 33333333ull) skip = 1;
            else {
                sequence_slots[k].ns -= 33333333ull;
                if (sequence_slots[k].ns >= 33333333ull) sequence_slots[k].ns = 0;
            }
        }
        pm_unlock(&battle_sequence_lock);
    }
    if (skip) {
        __atomic_add_fetch(&battle_sequence_skips, 1, __ATOMIC_RELAXED);
        c->x[0] = 0;
        c->pc = c->x[30];
    } else if (partial > 0.0f) {
        /* Speed is read only at 9DC3E0 inside the update, so pass the step there and restore it. */
        const u64 return_pc = c->x[30];
        /* The update takes only this in X0. Re-enter with the same argument and
           return address; leave the final call's return registers intact. */
        for (unsigned updates = 0; updates < SEQUENCE_MAX_UPDATES; updates++) {
            const float before = guest_float(object + 0xe8);
            __atomic_add_fetch(&battle_sequence_partial, 1, __ATOMIC_RELAXED);
            guest_float_write(object + 0xf0, partial);
            battle_sequence_original(c, entry);
            if (!readable(object, 0x308) || guest_float(object + 0xf0) != partial) break;
            guest_float_write(object + 0xf0, speed);
            if (c->pc != return_pc || !battle_sequence_playing(object) ||
                !(guest_float(object + 0xe8) > before) || updates + 1 == SEQUENCE_MAX_UPDATES) break;
            pm_lock(&battle_sequence_lock);
            /* A slot evicted by another sequence no longer owns our budget. */
            if (sequence_slots[slot].object == object) {
                const float frame = guest_float(object + 0xe8);
                const float pending = sequence_slots[slot].pending;
                /* Catch up whole boundaries only. A fractional tail waits for
                   the next rendered update, preserving the original cadence of
                   command callbacks rather than immediately updating them again. */
                partial = frame + pending >= floorf(frame) + 1.0f ? sequence_step(frame, pending) : 0.0f;
                sequence_slots[slot].pending = pending - partial;
            } else partial = 0.0f;
            pm_unlock(&battle_sequence_lock);
            if (!(partial > 0.0f)) break;
            c->x[0] = object;
            c->x[30] = return_pc;
            c->pc = entry;
        }
        /* The iteration guard retains any unconsumed budget in the slot. */
    } else battle_sequence_original(c, entry);
    if (count <= 4 || count % 2000 == 0) {
        char speeds[256];
        int n = 0;
        speeds[0] = 0;
        pm_lock(&battle_sequence_lock);
        for (unsigned s = 0; s < SEQUENCE_SPEEDS && sequence_speeds[s].calls && n >= 0 && n < (int)sizeof speeds; s++)
            n += snprintf(speeds + n, sizeof speeds - n, " %g:%llu", sequence_speeds[s].speed,
                          (unsigned long long)sequence_speeds[s].calls);
        const u64 other = sequence_speed_other;
        pm_unlock(&battle_sequence_lock);
        LOG("arceus-battle-sequence: calls=%llu partial=%llu skipped=%llu speeds=[%s ] other=%llu\n",
            (unsigned long long)count,
            (unsigned long long)__atomic_load_n(&battle_sequence_partial, __ATOMIC_RELAXED),
            (unsigned long long)__atomic_load_n(&battle_sequence_skips, __ATOMIC_RELAXED),
            speeds, (unsigned long long)other);
    }
}
static void battle_sequence_fix_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_BATTLE_SEQUENCE_FIX");
    if (!main_base || !enabled || strcmp(enabled, "1")) return;
    if (!clocks_installed) {
        WARN("arceus-battle-sequence: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    const u64 pc = main_base + 0x9dbeac;
    int covered = 0;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == 0x598) covered = 1;
    if (!covered || pc < g_code_lo || pc + 0x598 > g_code_hi || !readable(pc, 0x598) ||
        gr32(pc) != 0x6db923e9 || gr32(pc + 0x2c) != 0xaa0003f3 ||
        gr32(pc + 0x534) != 0xbd40f262 || gr32(pc + 0x538) != 0x1e222821) {
        WARN("arceus-battle-sequence: refused instruction/function guard\n");
        return;
    }
    /* Equivalent MOV X19,X0 -> ADD X19,X0,#0 selects an independent JIT body. */
    gw32(pc + 0x2c, 0x91000013);
    jit_code_changed(pc, 0x598);
    battle_sequence_original = jit_wait(pc);
    if (!battle_sequence_original) {
        WARN("arceus-battle-sequence: compile failed\n");
        return;
    }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], battle_sequence_update, __ATOMIC_RELEASE);
    LOG("arceus-battle-sequence: EXPERIMENT: battle effect sequences advance 30 frames per second of elapsed time\n");
}
/* Map cursors 120F1F4 (field map) and 1245DB8 (world map) add stick x speed to the position
   per call and count held frames in two counters up to 9. The fix scales the speed by
   elapsed / (1/30) for the call and lets the counters rise only once 1/30 s has built up. */
typedef struct { u32 offset, size, head, mov, speed_at, speed_word, speed, held; const char* label; } MapCursorSite;
static const MapCursorSite map_cursor_sites[2] = {
    {0x120f1f4, 0x55c, 0xd101c3ff, 0x54, 0x270, 0xbd4d7262, 0xd70, 0xd78, "field map"},
    {0x1245db8, 0x828, 0xfc1a0fea, 0x5c, 0x518, 0xbd497262, 0x970, 0x978, "world map"},
};
static GuestFn map_cursor_original[2];
static PMutex map_cursor_lock = PMUTEX_INIT;
static u64 map_cursor_object[2], map_cursor_ns[2], map_cursor_calls[2];
static void map_cursor_update(CPU* c, u64 entry, unsigned kind) {
    const MapCursorSite* s = &map_cursor_sites[kind];
    const u64 object = c->x[0];
    const u64 count = __atomic_add_fetch(&map_cursor_calls[kind], 1, __ATOMIC_RELAXED);
    if (!readable(object, s->held + 8)) {
        map_cursor_original[kind](c, entry);
        return;
    }
    pm_lock(&step_lock);
    const u64 ns = step.step ? step.step : step.initial_ns;
    pm_unlock(&step_lock);
    pm_lock(&map_cursor_lock);
    if (map_cursor_object[kind] != object) map_cursor_object[kind] = object, map_cursor_ns[kind] = 0;
    map_cursor_ns[kind] += ns;
    const int tick = map_cursor_ns[kind] >= 33333333ull;
    if (tick) {
        map_cursor_ns[kind] -= 33333333ull;
        if (map_cursor_ns[kind] >= 33333333ull) map_cursor_ns[kind] = 0;
    }
    pm_unlock(&map_cursor_lock);
    const float speed = guest_float(object + s->speed), scaled = speed * (float)ns / 33333333.0f;
    const u32 held_a = gr32(object + s->held), held_b = gr32(object + s->held + 4);
    guest_float_write(object + s->speed, scaled);
    map_cursor_original[kind](c, entry);
    if (!readable(object, s->held + 8)) return;
    if (guest_float(object + s->speed) == scaled) guest_float_write(object + s->speed, speed);
    if (!tick) {
        if (gr32(object + s->held) == held_a + 1) gw32(object + s->held, held_a);
        if (gr32(object + s->held + 4) == held_b + 1) gw32(object + s->held + 4, held_b);
    }
    if (count <= 2 || count % 6000 == 0)
        LOG("arceus-map-cursor: %s calls=%llu speed=%g scaled=%g\n", s->label, (unsigned long long)count,
            (double)speed, (double)scaled);
}
static void field_map_cursor_update(CPU* c, u64 e) { map_cursor_update(c, e, 0); }
static void world_map_cursor_update(CPU* c, u64 e) { map_cursor_update(c, e, 1); }
static void map_cursor_fix_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_MAP_CURSOR_FIX");
    if (!main_base || !enabled || strcmp(enabled, "1")) return;
    if (!clocks_installed) {
        WARN("arceus-map-cursor: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    const GuestFn wrappers[2] = {field_map_cursor_update, world_map_cursor_update};
    unsigned installed = 0;
    for (unsigned k = 0; k < 2; k++) {
        const MapCursorSite* s = &map_cursor_sites[k];
        const u64 pc = main_base + s->offset;
        int covered = 0;
        for (u32 i = 0; i < g_mod->nfns; i++)
            if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == s->size) covered = 1;
        if (!covered || pc < g_code_lo || pc + s->size > g_code_hi || !readable(pc, s->size) ||
            gr32(pc) != s->head || gr32(pc + s->mov) != 0xaa0003f3 || gr32(pc + s->speed_at) != s->speed_word) {
            WARN("arceus-map-cursor: refused %s guard\n", s->label);
            continue;
        }
        /* Equivalent MOV X19,X0 -> ADD X19,X0,#0 selects an independent JIT body. */
        gw32(pc + s->mov, 0x91000013);
        jit_code_changed(pc, s->size);
        map_cursor_original[k] = jit_wait(pc);
        if (!map_cursor_original[k]) {
            WARN("arceus-map-cursor: %s compile failed\n", s->label);
            continue;
        }
        __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], wrappers[k], __ATOMIC_RELEASE);
        installed++;
    }
    LOG("arceus-map-cursor: EXPERIMENT: %u of 2 map cursors follow elapsed time\n", installed);
}
static void timing_research_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_TIMING_RESEARCH");
    if (!main_base || !enabled || strcmp(enabled,"1")) return;
    static const struct { u64 offset; u32 size, head, branch, word, replacement; } sites[2] = {
        {0x2795204,0x928,0xd10283ff,0x924,0x17ffff7b,0xb4ffef7f},
        {0x2af25b4,0x6b8,0xfc190fe8,0x6a8,0x17ffff40,0xb4ffe81f}
    };
    const GuestFn wrappers[2] = {navigation_probe, physics_probe};
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    research_observing = 1;
    engine_clock_research_init();
    frame_timer_research_init();
    sequence_clock_research_init();
    for (unsigned k=0; k<2; k++) {
        u64 pc=main_base+sites[k].offset;
        int covered=0;
        for (u32 i=0; i<g_mod->nfns; i++)
            if (g_mod->fn_lo[i]==pc && g_mod->fn_len[i]==sites[k].size) covered=1;
        if (!covered || pc<g_code_lo || pc+sites[k].size>g_code_hi || !readable(pc,sites[k].size) ||
            gr32(pc)!=sites[k].head || gr32(pc+sites[k].branch)!=sites[k].word) {
            WARN("arceus-timing-research: refused guard kind=%u\n", k); continue;
        }
        gw32(pc+sites[k].branch,sites[k].replacement);
        jit_code_changed(pc,sites[k].size);
        timing_research_original[k]=jit_wait(pc);
        if (!timing_research_original[k]) { WARN("arceus-timing-research: compile failed kind=%u\n",k); continue; }
        __atomic_store_n(&g_disp[(pc-g_code_lo)/4],wrappers[k],__ATOMIC_RELEASE);
        LOG("arceus-timing-research: read-only probe installed kind=%u\n",k);
    }
}

/* Arceus 1.1.1's 3D resolution governor (3DBufferWidthScale/HeightScale).
   The update averages GPU duration against 33.333 ms and chooses the 0.90/0.85,
   0.85/0.80 and 0.80/0.70 scene scales. Its mode setter is separate (CC3888):
   normal mode begins at 1.0/1.0, with explicit half-size/custom/special modes.
   Keep those setters intact and skip only this automatic governor's update.
   The native Vulkan render scale still controls the allocated image size. */
static void fixed_resolution_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_FIXED_RESOLUTION");
    if (!main_base || !enabled || strcmp(enabled, "1")) return;
    const u64 pc = main_base + 0xcc35a4;
    int covered = 0;
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == 0x2c8) covered = 1;
    if (!covered || pc < g_code_lo || pc + 0x2c8 > g_code_hi || !readable(pc, 0x2c8) ||
        gr32(pc) != 0xd10103ff || gr32(pc + 0x20) != 0x51000509 ||
        gr32(pc + 0x24) != 0x7100093f || gr32(pc + 0x28) != 0x54000308 ||
        gr32(pc + 0x94) != 0xd65f03c0) {
        WARN("arceus-resolution: refused governor instruction/function guard\n");
        return;
    }
    /* SUB W9,W8,#1 -> MOV W9,#3. The existing CMP #2 / B.HI takes the
       governor's ordinary early return before counters, timing or scale writes. */
    gw32(pc + 0x20, 0x52800069);
    jit_code_changed(pc, 0x2c8);
    if (!jit_wait(pc)) {
        WARN("arceus-resolution: governor JIT failed\n");
        return;
    }
    LOG("arceus-resolution: automatic 3D resolution governor disabled; explicit scene modes unchanged\n");
}

/* The main rigid-body wrapper runs once per rendered frame and normally selects
   a fixed 1/30 s step, so its world simulates at FPS/30 speed. Replace the
   selection with the smaller of 1/30 and the elapsed input. Above 30 FPS that
   is real time; at 30 FPS or below it keeps the authored step and the original
   slowdown instead of asking the solver for a longer one. A NaN input also
   falls back to 1/30, and the existing 2^-23 gate at 2AF2A20 still skips the
   solve for a zero step. Worlds stepped through other callers are not touched. */
static void physics_timestep_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_PHYSICS_TIMESTEP_FIX");
    if (!main_base || !enabled || strcmp(enabled, "1")) return;
    const u64 pc = main_base + 0x2af25b4;
    int covered = 0;
    if (!clocks_installed) {
        WARN("arceus-physics-timestep: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == 0x6b8) covered = 1;
    if (!covered || pc < g_code_lo || pc + 0x6b8 > g_code_hi || !readable(pc, 0x6b8) ||
        gr32(pc) != 0xfc190fe8 || gr32(pc + 0x38) != 0x39448688 ||
        gr32(pc + 0x48) != 0x1e270100 || gr32(pc + 0x6c) != 0x1e280c08) {
        WARN("arceus-physics-timestep: refused instruction/function guard\n");
        return;
    }
    /* FCSEL S8,S0,S8,EQ -> FMINNM S8,S0,S8. S0 holds 1/30 and S8 the input. */
    gw32(pc + 0x6c, 0x1e287808);
    jit_code_changed(pc, 0x6b8);
    if (!jit_wait(pc)) {
        WARN("arceus-physics-timestep: wrapper JIT failed\n");
        return;
    }
    physics_timestep_active = 1;
    LOG("arceus-physics-timestep: EXPERIMENT: main rigid-body step is now min(1/30, elapsed)\n");
}

/* 2795204 steps AI navigation (hkaiWorld::stepWorld) once per rendered frame, but mode
   +150 stays 0 and selects a fixed 1/30 s, so navigation ran at FPS/30 speed. Use
   min(1/30, elapsed) like the physics step. arceus_step never yields a zero step and
   FMINNM maps NaN to 1/30. A nonzero mode, never observed, would also be capped. */
static void navigation_timestep_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_NAVIGATION_TIMESTEP_FIX");
    if (!main_base || !enabled || strcmp(enabled, "1")) return;
    const u64 pc = main_base + 0x2795204, caller = main_base + 0x279a6d0;
    int covered = 0;
    if (!clocks_installed) {
        WARN("arceus-navigation-timestep: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == 0x928) covered = 1;
    /* Prologue, the +150 mode load, the FCSEL, and the caller's tail branch. */
    if (!covered || pc < g_code_lo || pc + 0x928 > g_code_hi || !readable(pc, 0x928) ||
        !readable(caller, 0x50) || gr32(pc) != 0xd10283ff || gr32(pc + 0x24) != 0x39454008 ||
        gr32(pc + 0x40) != 0x1e200c28 || gr32(caller) != 0xf81e0ff3 || gr32(caller + 0x4c) != 0x17ffeaba) {
        WARN("arceus-navigation-timestep: refused instruction/function guard\n");
        return;
    }
    /* FCSEL S8,S1,S0,EQ -> FMINNM S8,S1,S0. S1 holds 1/30 and S0 the input. */
    gw32(pc + 0x40, 0x1e207828);
    jit_code_changed(pc, 0x928);
    if (!jit_wait(pc)) {
        WARN("arceus-navigation-timestep: JIT failed\n");
        return;
    }
    navigation_timestep_active = 1;
    LOG("arceus-navigation-timestep: EXPERIMENT: AI navigation step is now min(1/30, elapsed)\n");
}

/* Owners that the cadence census measured once per rendered frame, each adding a fixed
   +/-1/30 s per call: the day clock feed (in-game minutes), event hold timers and
   weather blends. Each builds the constant with MOV/MOVK W8, which becomes ADRP X8 and
   LDR W8 from the frame-step cell, as for the water clock. */
typedef struct { u32 owner, size, head, site; u8 negative; const char* label; } FrameStepSite;
static const FrameStepSite frame_step_sites[] = {
    {0x1d5d59c,0x528,0xd10203ff,0x1d5d684,0,"world time"},
    {0x167f988,0x650,0xd103c3ff,0x167ff0c,0,"event hold +190"},
    {0x168300c,0x51c,0xd10383ff,0x16832c0,0,"event timer +1A0"},
    {0x168300c,0x51c,0xd10383ff,0x16832e4,0,"event timer +198"},
    {0x1683528,0x35c,0xd10103ff,0x16836d0,0,"event timer +19C"},
    {0x1b47d10,0x1a70,0xd10343ff,0x1b47f5c,1,"weather fade +24C"},
    {0x1b47d10,0x1a70,0xd10343ff,0x1b47f94,1,"weather fade +250"},
    {0x1b47d10,0x1a70,0xd10343ff,0x1b47fcc,1,"weather fade +254"},
    {0x1b47d10,0x1a70,0xd10343ff,0x1b48a48,0,"weather rise +254"},
    {0x1b47d10,0x1a70,0xd10343ff,0x1b48a94,0,"weather rise +24C"},
    {0x1b47d10,0x1a70,0xd10343ff,0x1b49004,0,"weather rise +24C"},
    {0x1b47d10,0x1a70,0xd10343ff,0x1b493f4,0,"weather rise +250"},
};
#define FRAME_STEP_SITES (sizeof frame_step_sites / sizeof frame_step_sites[0])

static int covers_exactly(u64 pc, u64 size) {
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == size) return 1;
    return 0;
}
static int writable(u64 address, u64 size) {
    MemBlock block;
    if (!readable(address, size)) return 0;
    mem_query(address, &block);
    return (block.perm & PERM_W) && block.addr + block.size >= address + size;
}
static float frame_step_ratio(void) {
    pm_lock(&step_lock);
    const u64 ns = step.step ? step.step : step.initial_ns;
    pm_unlock(&step_lock);
    float r = (float)((double)ns / 33333334.0);
    if (!(r <= 1.0f)) r = 1.0f;
    /* The floor only keeps the ratio above zero. It binds above 30000 FPS, not at unlocked rates. */
    return r < ARCEUS_MIN_STEP_RATIO ? ARCEUS_MIN_STEP_RATIO : r;
}

/* 1E1A25C eases cave lighting through record x0+5E640: target +0, active byte +4,
   progress +8 and increment +C, which a change sets to 1/120 and then adds once per
   frame. Scale the increment by step/(1/30) for each call while a change is active,
   and restore the authored value if the call left ours in place. */
#define CAVE_LIGHT_RECORD 0x5e640
static GuestFn cave_light_original;
static void cave_light_update(CPU* c, u64 entry) {
    const u64 record = c->x[0] + CAVE_LIGHT_RECORD;
    if (!writable(record, 16) || !(gr32(record + 4) & 0xff)) {
        cave_light_original(c, entry);
        return;
    }
    const u32 authored = gr32(record + 0xc);
    float increment;
    memcpy(&increment, &authored, sizeof increment);
    increment *= frame_step_ratio();
    u32 seen;
    memcpy(&seen, &increment, sizeof seen);
    gw32(record + 0xc, seen);
    cave_light_original(c, entry);
    if (gr32(record + 0xc) == seen) gw32(record + 0xc, authored);
}

static unsigned frame_step_sites_init(const FrameStepSite* sites, unsigned n, const char* tag) {
    unsigned installed = 0;
    for (unsigned k = 0; k < n; k++) {
        const u64 owner = main_base + sites[k].owner, pc = main_base + sites[k].site;
        const u32 movk = sites[k].negative ? 0x72b7a108 : 0x72a7a108;
        if (!covers_exactly(owner, sites[k].size) || !readable(owner, sites[k].size) ||
            gr32(owner) != sites[k].head || gr32(pc) != 0x52911128 || gr32(pc + 4) != movk) {
            WARN("%s: refused guard %s at main+%x\n", tag, sites[k].label, sites[k].site);
            continue;
        }
        /* ADRP X8, cell; LDR W8, [X8, #0 or #4]. The next instruction moves W8 to a float. */
        const u64 pages = ((frame_step_address >> 12) - (pc >> 12)) & 0x1fffff;
        gw32(pc, 0x90000008 | (u32)((pages & 3) << 29) | (u32)((pages >> 2) << 5));
        gw32(pc + 4, sites[k].negative ? 0xb9400508 : 0xb9400108);
        jit_code_changed(pc, 8);
        installed++;
    }
    return installed;
}

/* One page holds the frame-step cell for every fix that loads it. The first user maps it. */
static int frame_step_cell_init(const char* tag) {
    if (frame_step_address) return 1;
    const u64 address = mem_find_free(main_base + 0x5000000, main_base + 0x40000000, 4096, 4096);
    if (!address || mem_map(address, 4096, MS_STATIC, PERM_RW)) {
        WARN("%s: could not allocate the frame-step cell\n", tag);
        return 0;
    }
    pm_lock(&step_lock);
    frame_step_address = address;
    frame_step_write(step.step ? step.step : step.initial_ns);
    pm_unlock(&step_lock);
    return 1;
}

static int cave_light_init(void) {
    const u64 pc = main_base + 0x1e1a25c;
    /* Prologue, the record offset build, the 1/120 build, the progress load and add, and the B at +11C. */
    if (!covers_exactly(pc, 0x17dc) || pc < g_code_lo || pc + 0x17dc > g_code_hi || !readable(pc, 0x17dc) ||
        gr32(pc) != 0xd105c3ff || gr32(pc + 0xc4) != 0x529cc808 || gr32(pc + 0xc8) != 0x72a000a8 ||
        gr32(pc + 0x101c) != 0x5291112a || gr32(pc + 0x1020) != 0x72a7810a ||
        gr32(pc + 0x1050) != 0x2d410520 || gr32(pc + 0x1054) != 0x1e202820 || gr32(pc + 0x11c) != 0x14000005) {
        WARN("arceus-world-timers: refused cave light guard\n");
        return 0;
    }
    gw32(pc + 0x11c, 0xb40000bf); /* Equivalent B -> CBZ XZR to pc+0x130. */
    jit_code_changed(pc, 0x17dc);
    cave_light_original = jit_wait(pc);
    if (!cave_light_original) {
        WARN("arceus-world-timers: cave light body did not compile\n");
        return 0;
    }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], cave_light_update, __ATOMIC_RELEASE);
    return 1;
}

static void world_timers_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_WORLD_TIMERS_FIX");
    if (!main_base || !enabled || strcmp(enabled, "1")) return;
    if (!clocks_installed) {
        WARN("arceus-world-timers: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    if (!frame_step_cell_init("arceus-world-timers")) return;
    const unsigned sites = frame_step_sites_init(frame_step_sites, FRAME_STEP_SITES, "arceus-world-timers");
    const int cave = cave_light_init();
    LOG("arceus-world-timers: EXPERIMENT: %u of %u fixed-step sites and cave light=%d use min(1/30, elapsed), cell=%llx\n",
        sites, (unsigned)FRAME_STEP_SITES, cave, (unsigned long long)frame_step_address);
}

/* 198D7E4 airborne branch: the velocity gains -g x factor x rate x 1/30 per call, and the
   census and gravity probe saw one call per rendered frame, so falls sped up with FPS.
   The -1/30 is built with MOV/MOVK W8 and moved to S1 at +2B4. Load the negated
   frame step instead, as the world timers do. */
static const FrameStepSite gravity_sites[] = {
    {0x198d7e4,0x998,0xd10503ff,0x198da8c,1,"airborne gravity"},
};
/* Character movement steps that add accel x 1/30 (or subtract x/30) once per call, which
   runs once per rendered frame. Gravity alone was fixed, so airborne sideways motion stayed
   4 times fast at 120 FPS and jumps looked floaty. Each pair is a MOV/MOVK W8 of +-1/30. */
static const FrameStepSite movement_sites[] = {
    {0x198d7e4,0x998,0xd10503ff,0x198e020,0,"shared airborne force"},
    {0x19843a0,0xb18,0xd102c3ff,0x1984a70,1,"shared vertical"},
    {0x1a08560,0x4bb0,0xd103c3ff,0x1a0bee4,0,"player velocity accel"},
    {0x1a08560,0x4bb0,0xd103c3ff,0x1a0baf0,0,"player speed accel"},
    {0x1a08560,0x4bb0,0xd103c3ff,0x1a0b3ac,1,"player countdown +750"},
    {0x1a0e0ec,0x118c,0xa9ba6ffc,0x1a0eea0,0,"player airborne force"},
    {0x1a14f80,0xc54,0xd10283ff,0x1a158c8,1,"player vertical"},
    {0x1a173b0,0x22a4,0xfc180fea,0x1a17b10,0,"player state accel"},
    {0x1a173b0,0x22a4,0xfc180fea,0x1a17f3c,0,"player state force"},
};
#define MOVEMENT_SITES (sizeof movement_sites / sizeof movement_sites[0])
static void movement_fix_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_MOVEMENT_FIX");
    if (!main_base || !enabled || strcmp(enabled, "1")) return;
    if (!clocks_installed) {
        WARN("arceus-movement-fix: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    if (!frame_step_cell_init("arceus-movement-fix")) return;
    /* Optional hex mask over movement_sites (bit 0 is the first) to test sites one by one. */
    const char* mask_text = getenv("SNX_ARCEUS_MOVEMENT_SITES");
    const unsigned long mask = mask_text && *mask_text ? strtoul(mask_text, NULL, 16) : ~0ul;
    FrameStepSite chosen[MOVEMENT_SITES];
    unsigned n = 0;
    for (unsigned k = 0; k < MOVEMENT_SITES; k++)
        if (mask >> k & 1) chosen[n++] = movement_sites[k];
    const unsigned sites = frame_step_sites_init(chosen, n, "arceus-movement-fix");
    LOG("arceus-movement-fix: EXPERIMENT: %u of %u character movement sites (mask %lx) use min(1/30, elapsed), cell=%llx\n",
        sites, (unsigned)MOVEMENT_SITES, mask, (unsigned long long)frame_step_address);
}
static void gravity_fix_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_GRAVITY_FIX");
    if (!main_base || !enabled || strcmp(enabled, "1")) return;
    if (!clocks_installed) {
        WARN("arceus-gravity-fix: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    /* FMOV S1, W8 right after the pair, so only the constant changes. */
    const u64 next = main_base + gravity_sites[0].site + 0xc;
    if (!readable(next, 4) || gr32(next) != 0x1e270101) {
        WARN("arceus-gravity-fix: refused guard at main+%x\n", gravity_sites[0].site + 0xc);
        return;
    }
    if (!frame_step_cell_init("arceus-gravity-fix")) return;
    const unsigned sites = frame_step_sites_init(gravity_sites, 1, "arceus-gravity-fix");
    LOG("arceus-gravity-fix: EXPERIMENT: %u of 1 airborne gravity sites use min(1/30, elapsed), cell=%llx\n",
        sites, (unsigned long long)frame_step_address);
}

/* C21D00 (fade in) and C227BC (fade out) start a fade: its fractional progress starts over. */
static GuestFn fade_start_original[2];
static void fade_start_update(CPU* c, u64 entry, unsigned kind) {
    const u64 object = c->x[0];
    fade_start_original[kind](c, entry);
    pm_lock(&fade_lock);
    for (int k = 0; k < FADE_SLOTS; k++)
        if (fade_slots[k].object == object) {
            fade_slots[k].frame = ~0u;
            fade_slots[k].progress = 0.0f;
            fade_slots[k].generation++;
        }
    pm_unlock(&fade_lock);
}
static void fade_in_start(CPU* c, u64 e) { fade_start_update(c, e, 0); }
static void fade_out_start(CPU* c, u64 e) { fade_start_update(c, e, 1); }
static void fade_fix_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_FADE_FIX");
    if (!main_base || !enabled || strcmp(enabled, "1")) return;
    if (!clocks_installed) {
        WARN("arceus-fade: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    /* The mode 0 step words: LDR W10 +538, ADD W10 #1, LDR Q1 +550, FADD, STR W10 +538. */
    const u64 update = main_base + 0xc21490;
    if (!readable(update, 0x300) || gr32(update + 0x44) != 0xb9453a6a || gr32(update + 0xd0) != 0x1100054a ||
        gr32(update + 0xd8) != 0x3dc15661 || gr32(update + 0xdc) != 0x4e21d400 || gr32(update + 0xe4) != 0xb9053a6a) {
        WARN("arceus-fade: refused update guard\n");
        return;
    }
    static const struct { u32 offset, size, head, mov; const char* label; } starts[2] = {
        {0xc21d00, 0xa50, 0xd10203ff, 0x2c, "fade in C21D00"},
        {0xc227bc, 0x888, 0xd101c3ff, 0x20, "fade out C227BC"},
    };
    const GuestFn wrappers[2] = {fade_in_start, fade_out_start};
    for (unsigned k = 0; k < 2; k++) {
        const u64 pc = main_base + starts[k].offset;
        if (!covers_exactly(pc, starts[k].size) || pc < g_code_lo || pc + starts[k].size > g_code_hi ||
            !readable(pc, starts[k].size) || gr32(pc) != starts[k].head || gr32(pc + starts[k].mov) != 0xaa0003f3) {
            WARN("arceus-fade: refused %s guard\n", starts[k].label);
            return;
        }
    }
    if (!frame_step_cell_init("arceus-fade") || !sequence_clock_install(0)) return;
    for (unsigned k = 0; k < 2; k++) {
        const u64 pc = main_base + starts[k].offset;
        /* Equivalent MOV X19,X0 -> ADD X19,X0,#0 selects an independent JIT body. */
        gw32(pc + starts[k].mov, 0x91000013);
        jit_code_changed(pc, starts[k].size);
        fade_start_original[k] = jit_wait(pc);
        if (!fade_start_original[k]) {
            WARN("arceus-fade: %s compile failed\n", starts[k].label);
            return;
        }
        __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], wrappers[k], __ATOMIC_RELEASE);
    }
    fade_fix_active = 1;
    LOG("arceus-fade: EXPERIMENT: common fades (mode 0) follow elapsed time\n");
}
/* 1A7191C is a thrown-object flight update, a virtual shared by three classes
   (vtables 41375C0, 413C5D0, 4141950). In flight (+5F8 == 1) each call adds S/30
   to elapsed +618, ends the planned flight when +618/+61C exceeds 1, then
   applies drag +5A0 and lift +5A4 to the queued velocity. S is a nominal-1
   ratio, so all three assume one call per 1/30 s. With the elapsed-time physics
   step, scale them for this call only by r = step/(1/30): +618 and +61C are
   divided by r so the guest's own end test sees (old + r*S/30)/T, then +618 is
   multiplied back and +61C restored. Lift is additive per call and scales by r;
   drag uses 1-(1-d)^r, exact for S = 1. Each field is restored only if the call
   left our value in place. Without the timestep fix the wrapper only logs. */
static GuestFn ball_flight_original;
static int ball_flight_correct;
static u64 ball_flight_calls, ball_flight_logs;
static u32 float_bits(float value) { u32 bits; memcpy(&bits, &value, sizeof bits); return bits; }
static void ball_flight_update(CPU* c, u64 entry) {
    const u64 object = c->x[0];
    MemBlock block;
    if (!readable(object, 0x620)) { ball_flight_original(c, entry); return; }
    mem_query(object + 0x5a0, &block);
    if (!(block.perm & PERM_W) || block.addr + block.size < object + 0x620) {
        ball_flight_original(c, entry);
        return;
    }
    const u32 state = gr32(object + 0x5f8);
    pm_lock(&step_lock);
    const u64 ns = step.step ? step.step : step.initial_ns;
    pm_unlock(&step_lock);
    float r = (float)((double)ns / 33333334.0);
    if (!(r <= 1.0f)) r = 1.0f;
    if (r < 0.0625f) r = 0.0625f;
    const int correct = ball_flight_correct && state == 1 && r < 1.0f;
    const u32 drag_bits = gr32(object + 0x5a0), lift_bits = gr32(object + 0x5a4);
    const u32 planned_bits = gr32(object + 0x61c);
    const float elapsed0 = guest_float(object + 0x618), planned = guest_float(object + 0x61c);
    const float drag = guest_float(object + 0x5a0), lift = guest_float(object + 0x5a4);
    float elapsed_seen = elapsed0, planned_seen = planned, drag_seen = drag, lift_seen = lift;
    if (correct) {
        elapsed_seen = elapsed0 / r;
        planned_seen = planned / r;
        drag_seen = drag >= 0.0f && drag < 1.0f ? 1.0f - powf(1.0f - drag, r) : drag * r;
        lift_seen = lift * r;
        guest_float_write(object + 0x618, elapsed_seen);
        guest_float_write(object + 0x61c, planned_seen);
        guest_float_write(object + 0x5a0, drag_seen);
        guest_float_write(object + 0x5a4, lift_seen);
    }
    const u64 now = plat_ns();
    ball_flight_original(c, entry);
    const u32 state_after = gr32(object + 0x5f8);
    float elapsed1 = guest_float(object + 0x618);
    /* Valid only while the call stayed in flight: the guest added S/30. */
    const float implied_s = 30.0f * (elapsed1 - elapsed_seen);
    if (correct) {
        if (gr32(object + 0x5a0) == float_bits(drag_seen)) gw32(object + 0x5a0, drag_bits);
        if (gr32(object + 0x5a4) == float_bits(lift_seen)) gw32(object + 0x5a4, lift_bits);
        if (gr32(object + 0x61c) == float_bits(planned_seen)) {
            gw32(object + 0x61c, planned_bits);
            elapsed1 *= r;
            guest_float_write(object + 0x618, elapsed1);
        }
    }
    const u64 count = __atomic_add_fetch(&ball_flight_calls, 1, __ATOMIC_RELAXED);
    const int transition = state != state_after;
    if (state != 1 && !transition && count % 600) return;
    /* Every launch and end, the first 600 in-flight calls, then 1 in 30. */
    const u64 logs = __atomic_add_fetch(&ball_flight_logs, 1, __ATOMIC_RELAXED);
    if (!transition && logs > 600 && count % 30) return;
    u32 input_bits = __atomic_load_n(&physics_input_bits, __ATOMIC_RELAXED);
    float input; memcpy(&input, &input_bits, sizeof input);
    LOG("arceus-ball-flight: count=%llu ns=%llu object=%llx state=%u->%u step_ns=%llu r=%g corrected=%d physics_input=%g elapsed=%g->%g planned=%g implied_s=%g drag=%g->%g lift=%g->%g\n",
        (unsigned long long)count, (unsigned long long)now, (unsigned long long)object, state, state_after,
        (unsigned long long)ns, (double)r, correct, (double)input, (double)elapsed0, (double)elapsed1,
        (double)guest_float(object + 0x61c), (double)implied_s, (double)drag, (double)drag_seen,
        (double)lift, (double)lift_seen);
}

static void ball_flight_init(void) {
    const char* research = getenv("SNX_ARCEUS_TIMING_RESEARCH");
    const int observe = research && !strcmp(research, "1");
    if (!main_base || (!physics_timestep_active && !observe)) return;
    const u64 pc = main_base + 0x1a7191c;
    int covered = 0;
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] >= 0x60c) covered = 1;
    /* Prologue, the +618 load/store, the +61C load, the drag and lift loads,
       the drag multiply and the B at +FC that is rewritten below. */
    if (!covered || pc < g_code_lo || pc + 0x60c > g_code_hi || !readable(pc, 0x60c) ||
        gr32(pc) != 0xd10203ff || gr32(pc + 0x13c) != 0xbd461e60 ||
        gr32(pc + 0x160) != 0xbd461a62 || gr32(pc + 0x178) != 0xbd061a60 ||
        gr32(pc + 0x274) != 0xbd45a268 || gr32(pc + 0x278) != 0xbd45a669 ||
        gr32(pc + 0x2c8) != 0x1e280841 || gr32(pc + 0xfc) != 0x17ffffda) {
        WARN("arceus-ball-flight: refused instruction/function guard\n");
        return;
    }
    /* Equivalent B -> CBZ XZR, both to pc+0x64, so a hash-matched original AOT
       body is not selected after invalidation (same scheme as the event camera). */
    gw32(pc + 0xfc, 0xb4fffb5f);
    jit_code_changed(pc, 0x60c);
    ball_flight_original = jit_wait(pc);
    if (!ball_flight_original) {
        WARN("arceus-ball-flight: original body did not compile; wrapper not installed\n");
        return;
    }
    ball_flight_correct = physics_timestep_active;
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], ball_flight_update, __ATOMIC_RELEASE);
    LOG("arceus-ball-flight: wrapper installed correct=%d\n", ball_flight_correct);
}

/* Read-only cadence census for the owners of the audited fixed 1/30 (and 1/60,
   1/120) sites. A fixed step is only a high-FPS bug if its owner runs once per
   rendered frame, which the static audit could not establish. Every 5 s this
   logs, per owner, total calls/s and the calls/s of one sampled x0 object
   (many objects at 30 Hz and one object per frame both give large totals),
   beside main-world physics calls/s as the frame reference. Labels are the
   audit's provisional names. Owners already wrapped by other fixes are left out. */
static const struct { u32 offset, size, head, patch, word, replacement; const char* label; } census_sites[] = {
    {0xafb898,0x3fc,0xd10283ff,0x168,0x17ffffe6,0xb4fffcdf,"script char motion"},
    {0x13b32fc,0x2e4,0xd10203ff,0xf0,0x17fffff4,0xb4fffe9f,"credits timer"},
    {0x1458300,0x1154,0xd10343ff,0x1d8,0x14000069,0xb4000d3f,"movie skip fade"},
    {0x167f988,0x650,0xd103c3ff,0x440,0x17fffefa,0xb4ffdf5f,"event coord 167F988"},
    {0x168300c,0x51c,0xd10383ff,0x1fc,0x14000033,0xb400067f,"event coord 168300C"},
    {0x1683528,0x35c,0xd10103ff,0x16c,0x14000032,0xb400065f,"event coord 1683528"},
    {0x1717594,0x2d4,0xd100c3ff,0x2d0,0x17fffffc,0xb4ffff9f,"orientation helper"},
    {0x1727df4,0x3ec,0xd10143ff,0x36c,0x17ffffe8,0xb4fffd1f,"orientation helper 2"},
    {0x1982408,0xdd4,0xd10403ff,0x178,0x14000055,0xb4000abf,"shared char move"},
    {0x19843a0,0xb18,0xd102c3ff,0xfc,0x14000002,0xb400005f,"shared char vertical"},
    {0x198d7e4,0x998,0xd10503ff,0xb0,0x14000114,0xb400229f,"shared char accel"},
    {0x198eb04,0x2f8,0xd10503ff,0x204,0x17ffffd9,0xb4fffb3f,"char collision predict"},
    {0x1a08560,0x4bb0,0xd103c3ff,0x3a0,0x14000007,0xb40000ff,"player controller"},
    {0x1a0d9dc,0x11c,0xd10203ff,0xfc,0x17ffffd2,0xb4fffa5f,"player move helper"},
    {0x1a0e0ec,0x118c,0xa9ba6ffc,0x510,0x14000007,0xb40000ff,"player move 1A0E0EC"},
    {0x1a0f278,0x334,0xd10203ff,0xd4,0x17ffffd7,0xb4fffaff,"player method 1A0F278"},
    {0x1a14f80,0xc54,0xd10283ff,0x140,0x14000002,0xb400005f,"player vertical"},
    {0x1a173b0,0x22a4,0xfc180fea,0x1b0,0x1400000b,0xb400017f,"player motion 1A173B0"},
    {0x1a19654,0x220,0xd10143ff,0xa0,0x1400003a,0xb400075f,"player alt helper"},
    {0x1aab534,0x3c0,0xa9ba6ffc,0x188,0x17ffffd8,0xb4fffb1f,"countdown 1AAB534"},
    {0x1b47d10,0x1a70,0xd10343ff,0x204,0x17ffff97,0xb4fff2ff,"weather blend"},
    {0x1d1b454,0x1f48,0x6db63bef,0x8fc,0x14000002,0xb400005f,"player-state predict"},
    {0x1d5d59c,0x528,0xd10203ff,0x1f8,0x17fffff4,0xb4fffe9f,"world time feed"},
    {0x1e1a25c,0x17dc,0xd105c3ff,0x11c,0x14000005,0xb40000bf,"cave light easing"},
    {0x24ac8c4,0x318,0xfc1d0fe8,0x1b8,0x17ffffab,0xb4fff57f,"world time update"},
    {0x2527e8c,0x4b8,0xd10183ff,0x240,0x14000007,0xb40000ff,"pokemon 2527E8C"},
    {0x252b124,0xc50,0xd10203ff,0x1b4,0x14000007,0xb40000ff,"pokemon 252B124"},
    {0x252d448,0x12b0,0xa9ba6ffc,0x4ac,0x14000007,0xb40000ff,"pokemon 252D448"},
    {0x257abb4,0x3bc,0xd10203ff,0x1a8,0x17ffffac,0xb4fff59f,"pokemon/ride motion"},
    {0x257af70,0x20d0,0xd10383ff,0x158,0x17fffff1,0xb4fffe3f,"pokemon/ride accel"},
    {0x25a8a84,0xa58,0xd10343ff,0x378,0x14000007,0xb40000ff,"pokemon/ride 25A8A84"},
    {0x25abd58,0xcfc,0xd10483ff,0x98,0x140000a0,0xb400141f,"pokemon/ride gravity"},
};
#define CENSUS_N (sizeof census_sites / sizeof census_sites[0])
static struct { u64 pc, calls, probe, probe_calls; GuestFn original; } census[CENSUS_N];
static int census_active;
/* Read-only gravity probe on the shared acceleration owner. Its airborne branch writes
   -g x factor x rate / 30 to the result's vertical term. One object at a time, while that
   term is negative, each call logs the velocity read from x1+120, the x2 position and the
   result at +40 with real time, so a fall shows whether velocity gains it once per frame. */
static int gravity_probe_active;
static u64 gravity_object, gravity_skip, gravity_skip_ns, gravity_start_ns, gravity_calls, gravity_lines;
#define GRAVITY_SEGMENT_NS 4000000000ull
#define GRAVITY_LINE_LIMIT 40000ull
static void gravity_end(u64 object, u64 now, const char* why) {
    LOG("arceus-gravity: end object=%llx calls=%llu seconds=%.4f reason=%s\n", (unsigned long long)object,
        (unsigned long long)gravity_calls, (double)(now - gravity_start_ns) / 1e9, why);
    __atomic_store_n(&gravity_object, 0, __ATOMIC_RELEASE);
}
static void gravity_probe(CPU* c, u64 entry, GuestFn original) {
    const u64 object = c->x[0], in = c->x[1], pose = c->x[2], out = c->x[8];
    float v[3] = {0}, p[3] = {0};
    const int inputs = in && pose && readable(in + 0x120, 12) && readable(pose, 12);
    for (int k = 0; inputs && k < 3; k++) {
        v[k] = guest_float(in + 0x120 + 4 * k);
        p[k] = guest_float(pose + 4 * k);
    }
    original(c, entry);
    if (!inputs || !out || !readable(out + 0x40, 12)) return;
    u64 owner = __atomic_load_n(&gravity_object, __ATOMIC_ACQUIRE);
    if (owner && owner != object) return;
    const float ox = guest_float(out + 0x40), oy = guest_float(out + 0x44), oz = guest_float(out + 0x48);
    const u64 now = plat_ns();
    if (!(oy < 0.0f)) {
        if (owner) gravity_end(object, now, "landed");
        return;
    }
    if (!owner) {
        /* An object that filled a whole segment waits 10 s, so others get a turn. */
        if (object == gravity_skip && now - gravity_skip_ns < 10000000000ull) return;
        if (__atomic_load_n(&gravity_lines, __ATOMIC_RELAXED) >= GRAVITY_LINE_LIMIT) return;
        if (!__atomic_compare_exchange_n(&gravity_object, &owner, object, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
        gravity_start_ns = now;
        gravity_calls = 0;
        const u64 config = readable(object + 0x5c0, 8) ? gr64(object + 0x5c0) : 0;
        LOG("arceus-gravity: start object=%llx g=%g factor=%g max_fall=%g damping=%g\n",
            (unsigned long long)object, (double)guest_float(main_base + 0x413a0f8),
            (double)guest_float(object + 0xbe8),
            config && readable(config + 0x80, 4) ? (double)guest_float(config + 0x80) : 0.0,
            (double)guest_float(object + 0x920));
    }
    gravity_calls++;
    __atomic_add_fetch(&gravity_lines, 1, __ATOMIC_RELAXED);
    LOG("arceus-gravity: t=%.6f call=%llu v_in=%g,%g,%g pos=%g,%g,%g out=%g,%g,%g\n",
        (double)(now - gravity_start_ns) / 1e9, (unsigned long long)gravity_calls, (double)v[0], (double)v[1],
        (double)v[2], (double)p[0], (double)p[1], (double)p[2], (double)ox, (double)oy, (double)oz);
    if (now - gravity_start_ns >= GRAVITY_SEGMENT_NS) {
        gravity_skip = object;
        gravity_skip_ns = now;
        gravity_end(object, now, "segment limit");
    }
}
static void cadence_census_wrapper(CPU* c, u64 entry) {
    for (unsigned i = 0; i < CENSUS_N; i++) {
        if (census[i].pc != entry) continue;
        __atomic_add_fetch(&census[i].calls, 1, __ATOMIC_RELAXED);
        u64 probe = __atomic_load_n(&census[i].probe, __ATOMIC_RELAXED);
        /* On failure the exchange loads the winner into probe. */
        if (!probe && __atomic_compare_exchange_n(&census[i].probe, &probe, c->x[0], 0,
                                                  __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            probe = c->x[0];
        if (probe == c->x[0]) __atomic_add_fetch(&census[i].probe_calls, 1, __ATOMIC_RELAXED);
        if (gravity_probe_active && census_sites[i].offset == 0x198d7e4)
            gravity_probe(c, entry, census[i].original);
        else
            census[i].original(c, entry);
        return;
    }
}
/* Called from the main-world physics probe only, so one thread owns the history. */
static void cadence_census_report(u64 frames) {
    static u64 last_ns, last_frames, last_calls[CENSUS_N], last_probe[CENSUS_N];
    if (!census_active) return;
    const u64 now = plat_ns();
    if (!last_ns) { last_ns = now; last_frames = frames; return; }
    if (now - last_ns < 5000000000ull) return;
    const double seconds = (double)(now - last_ns) / 1e9;
    LOG("arceus-cadence: window=%.2fs frames_per_s=%.1f\n", seconds, (double)(frames - last_frames) / seconds);
    for (unsigned i = 0; i < CENSUS_N; i++) {
        if (!census[i].original) continue;
        const u64 calls = __atomic_load_n(&census[i].calls, __ATOMIC_RELAXED);
        const u64 probe_calls = __atomic_load_n(&census[i].probe_calls, __ATOMIC_RELAXED);
        const u64 probe = __atomic_load_n(&census[i].probe, __ATOMIC_RELAXED);
        if (calls != last_calls[i])
            LOG("arceus-cadence: site=main+%x label=\"%s\" calls_per_s=%.1f object_calls_per_s=%.1f object=%llx\n",
                census_sites[i].offset, census_sites[i].label, (double)(calls - last_calls[i]) / seconds,
                (double)(probe_calls - last_probe[i]) / seconds, (unsigned long long)probe);
        /* The day clock keeps in-game minutes (0..1440) as a double at +70. */
        if (census_sites[i].offset == 0x24ac8c4 && probe && readable(probe + 0x70, 8)) {
            const u64 bits = gr64(probe + 0x70);
            double minutes;
            memcpy(&minutes, &bits, sizeof minutes);
            LOG("arceus-cadence: world_minutes=%.3f\n", minutes);
        }
        /* Resample next window if the sampled object went quiet (destroyed or idle). */
        if (probe_calls == last_probe[i]) __atomic_store_n(&census[i].probe, 0, __ATOMIC_RELAXED);
        last_calls[i] = calls;
        last_probe[i] = probe_calls;
    }
    last_ns = now;
    last_frames = frames;
}
static void cadence_census_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_CADENCE_CENSUS");
    if (!main_base || !enabled || strcmp(enabled, "1") || !research_observing) return;
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    unsigned installed = 0, owned = 0;
    for (unsigned k = 0; k < CENSUS_N; k++) {
        const u64 pc = main_base + census_sites[k].offset, size = census_sites[k].size;
        int covered = 0;
        /* A fix already wraps this owner through the same rewritten branch. */
        if (readable(pc + census_sites[k].patch, 4) && gr32(pc + census_sites[k].patch) == census_sites[k].replacement) {
            owned++;
            continue;
        }
        for (u32 i = 0; i < g_mod->nfns; i++)
            if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == size) covered = 1;
        if (!covered || pc < g_code_lo || pc + size > g_code_hi || !readable(pc, size) ||
            gr32(pc) != census_sites[k].head || gr32(pc + census_sites[k].patch) != census_sites[k].word) {
            WARN("arceus-cadence: refused guard site=main+%x\n", census_sites[k].offset);
            continue;
        }
        /* Equivalent B -> CBZ XZR to the same in-function target. */
        gw32(pc + census_sites[k].patch, census_sites[k].replacement);
        jit_code_changed(pc, size);
        GuestFn original = jit_wait(pc);
        if (!original) {
            WARN("arceus-cadence: compile failed site=main+%x\n", census_sites[k].offset);
            continue;
        }
        census[k].pc = pc;
        census[k].original = original;
        __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], cadence_census_wrapper, __ATOMIC_RELEASE);
        installed++;
    }
    census_active = installed != 0;
    LOG("arceus-cadence: read-only census installed %u of %u owners, %u wrapped by fixes\n",
        installed, (unsigned)CENSUS_N, owned);
    const char* gravity = getenv("SNX_ARCEUS_GRAVITY_PROBE");
    if (!gravity || strcmp(gravity, "1")) return;
    for (unsigned k = 0; k < CENSUS_N; k++)
        if (census_sites[k].offset == 0x198d7e4 && census[k].original) gravity_probe_active = 1;
    LOG("arceus-gravity: read-only probe %s\n", gravity_probe_active ? "installed" : "unavailable");
}

/* Sneasler wall climb: the climb mover 25540E8 moves direction x speed/30 per call, a cast
   distance with no elapsed time, so climbing ran four times fast at 120 FPS. With
   SNX_ARCEUS_CLIMB_STEP=1 its FMOV #30 and FDIV become a frame-step cell load and FMUL, room
   made by storing the +D20 copy with a second STUR instead of an LDP/STP pair through X9/X8. */
static const u32 climb_original_words[12] = {
    0x1e27d000, 0x1e201900, 0x3cd503a1, 0x4f809020, 0xaa1303e0, 0x3c9503a0,
    0x3d8027e0, 0x3dc34a60, 0x3c9403a0, 0xa97423a9, 0xa93323a9, 0x910d83e8,
};
/* ADRP X9 (filled in), LDR S0,[X9], FMUL S0,S8,S0, then the original stores and the +D20 copy. */
static const u32 climb_patched_words[12] = {
    0x90000009, 0xbd400120, 0x1e200900, 0x3cd503a1, 0x4f809020, 0xaa1303e0,
    0x3c9503a0, 0x3d8027e0, 0x3dc34a60, 0x3c9403a0, 0x3c9303a0, 0x910d83e8,
};
/* The climb animation 2549FA0 divides the eased distance |+D50| by speed/30 for its blend and
   playback ratio. At 254A8E0 FCMEQ, MVN, FSQRT, AND, FMOV #30, FDIV, FMOV #1, FDIV become FCMEQ,
   BIC (AND with NOT), FSQRT, ADRP X9, LDR S2, FMUL S2,S8,S2, FMOV #1, FDIV: speed x step. */
static const u32 climb_anim_words[8] = {
    0x4ea0d822, 0x6e205842, 0x6ea1f821, 0x4e221c00, 0x1e27d002, 0x1e221902, 0x1e2e1008, 0x1e22182a,
};
static const u32 climb_anim_patched[8] = {
    0x4ea0d822, 0x4e621c00, 0x6ea1f821, 0x90000009, 0xbd400122, 0x1e220902, 0x1e2e1008, 0x1e22182a,
};
/* The ADRP of a frame-step cell load at pc, for register rd. */
static u32 climb_adrp(u64 pc, u32 rd) {
    const u64 pages = ((frame_step_address >> 12) - (pc >> 12)) & 0x1fffff;
    return 0x90000000 | rd | (u32)((pages & 3) << 29) | (u32)((pages >> 2) << 5);
}
static void climb_step_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_CLIMB_STEP");
    if (!main_base || !enabled || (strcmp(enabled, "1") && strcmp(enabled, "2"))) return;
    if (!clocks_installed) {
        WARN("arceus-climb: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    /* The function head, the speed load before the pair, the BL after the twelve words and the words themselves. */
    const u64 owner = main_base + 0x25540e8, pc = owner + 0x2f4;
    int ok = covers_exactly(owner, 0x2c38) && readable(owner, 0x2c38) && gr32(owner) == 0x6db63bef &&
             gr32(owner + 0x2dc) == 0xbd403188 && (gr32(pc + 0x30) & 0xfc000000) == 0x94000000;
    for (unsigned k = 0; ok && k < 12; k++) ok = gr32(pc + 4 * k) == climb_original_words[k];
    if (!ok) {
        WARN("arceus-climb: refused mover guard\n");
        return;
    }
    if (!frame_step_cell_init("arceus-climb")) return;
    for (unsigned k = 0; k < 12; k++) gw32(pc + 4 * k, k ? climb_patched_words[k] : climb_adrp(pc, 9));
    jit_code_changed(pc, 0x30);
    /* Without the animation ratio fix the ratio would be 1/4 at 120 FPS, a near-still blend. */
    const u64 anim = main_base + 0x2549fa0, at = anim + 0x940;
    int anim_ok = covers_exactly(anim, 0xef4) && readable(anim, 0xef4) && gr32(anim) == 0xd10303ff &&
                  gr32(anim + 0x38) == 0xaa0003f3 && gr32(anim + 0x880) == 0xbd403188;
    for (unsigned k = 0; anim_ok && k < 8; k++) anim_ok = gr32(at + 4 * k) == climb_anim_words[k];
    if (anim_ok) {
        for (unsigned k = 0; k < 8; k++) gw32(at + 4 * k, k == 3 ? climb_adrp(at + 12, 9) : climb_anim_patched[k]);
        jit_code_changed(anim, 0xef4);
    } else {
        WARN("arceus-climb: refused animation guard\n");
    }
    LOG("arceus-climb: EXPERIMENT: Sneasler climb distance uses min(1/30, elapsed), animation ratio %s\n",
        anim_ok ? "on" : "off");
}

void arceus_clock_post_jit(void) {
    fixed_resolution_init();
    /* Before the research probe, which compiles and keeps this wrapper's body. */
    physics_timestep_init();
    navigation_timestep_init();
    /* Before the census, which then skips the owner this wraps. */
    world_timers_init();
    gravity_fix_init();
    movement_fix_init();
    ball_flight_init();
    timing_research_init();
    capture_fix_init();
    battle_sequence_fix_init();
    map_cursor_fix_init();
    fade_fix_init();
    climb_step_init();
    cadence_census_init();
    const char* probe = getenv("SNX_ARCEUS_CAMERA_TRACE");
    if (main_base && !experimental && probe && !strcmp(probe, "1")) {
        camera_trace_only = 1;
        camera_input_init();
        camera_rate_init();
        return;
    }
    const char* enabled = getenv("SNX_ARCEUS_TIMING_FIXES");
    if (!enabled || strcmp(enabled, "1") || !clocks_installed) return;
    water_fix_init();
    effects_fix_init();
    event_camera_init();
    blink_fix_init();
    pad_repeat_init();
    camera_input_init();
    camera_rate_init();
    field_message_probe_init();
}
