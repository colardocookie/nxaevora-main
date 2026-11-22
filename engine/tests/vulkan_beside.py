# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""SNX_ARCEUS_PERF_BESIDE=1 (PERF-36, research's BUILD SPEC 2026-11-22 14:08) through the code itself: buffer.c's A,
B and C (vk_buf_fly_pick, vk_buf_fly_write, vk_buf_fly_finish, the wait vk_buf_fly_wait), cmd.c's fly_c,
land_through and vk_land_beside, gpu.c's gmem_ptr_untracked, buffer.c's entry_host and gpu.h's inline waits are
copied out of the source as they are, with vulkan_compare.py's stand-ins for the rest.

- Model: random batches (abutting and overlapping results, composed chains inside a batch and across it, guest
  writes between capture and landing, results that change nothing) land through today's loop and through A, B
  and C from the same start: the same guest bytes, stamps in order, original bytes of every result still pending,
  g_landed, and then the same bytes after the next frame lands as today. Also with B stopped part way by a full
  arena (the rest lands by today's loop in C).
- Threads: during B, a reader through gpu_fly_wait, gmem_ptr_untracked and entry_host on a batch's bytes returns
  only after B with the landed bytes, one on other bytes at once. A compute landing's write over a batch's bytes
  waits for B, then writes. A pointer taken before the batch, read in a later hold, waits. A land_through(must)
  during B waits and runs C once, the lander's C then does nothing. submit()'s landing during B lands nothing.
- Each fallback reason takes today's path with nothing changed. Unset, the inline waits never call the renderer.

Run: python3 tests/vulkan_beside.py [compiler and flags], for example
    python3 tests/vulkan_beside.py clang -std=gnu11 -O2 -fsanitize=address,undefined
"""
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import vulkan_compare as vc  # noqa: E402  its stand-ins, its copying helpers and its batches

SRC = HERE.parent / "src"

EXTRA = r'''
#include <sys/syscall.h>
typedef pthread_cond_t PCond;
static void pc_init(PCond* c) { pthread_cond_init(c, NULL); }
static void pc_broadcast(PCond* c) { pthread_cond_broadcast(c); }
static int pc_wait(PCond* c, PMutex* m, long long timeout) {
    (void)timeout;
    return pthread_cond_wait(c, m);
}
static unsigned plat_tid(void) { return (unsigned)syscall(SYS_gettid); }
static int g_own_any, g_gpu_fly_hold, g_gfx_fly;
static int image_due, rt_hit; /* the fallbacks' stand-ins */
static int vk_gfx_image_due(u64 sub) {
    (void)sub;
    return image_due;
}
static int vk_rt_pages_hit(u64 a, u64 n) {
    (void)a, (void)n;
    return rt_hit;
}
static unsigned slow_calls;
void vk_buf_fly_wait(const void* host, u64 n);
static void gpu_fly_wait_slow(const void* host, u64 n) {
    slow_calls++;
    vk_buf_fly_wait(host, n);
}
/* gpu.c's gmem_ptr_untracked over the stand-ins' memory */
#define GP(a) (guest + (a))
static u64 gmmu_run(u64 g, u64 n, u64* cpu) {
    *cpu = g;
    return g + n <= GUEST ? n : 0;
}
static void own_acquire(u64 a, u64 n) { (void)a, (void)n; }
/* buffer.c's entry_host over a cache entry with only the fields it reads */
typedef struct {
    u32 host_gen;
    u64 host_cpu;
} Vb;
static u32 g_vb_map_gen = 1;
static u8* gmem_host(u64 g, u64 n) { return g + n <= GUEST ? guest + g : NULL; }
/* a landing inside A's gmem_ptr, as a compute result's hook can make (its flush, a vk_finish): reviewer F1 */
static void (*ptr_hook)(void);
static u8* gmem_ptr_hooked(u64 a, u64 n) {
    u8* p = gmem_ptr(a, n);
    void (*h)(void) = ptr_hook;
    ptr_hook = NULL;
    if (h) h();
    return p;
}
#define gmem_ptr gmem_ptr_hooked
/* cmd.c's state */
static u64 g_landed, g_gval;
static unsigned image_frames;
static void vk_gfx_image_frame_done(u64 f) {
    (void)f;
    image_frames++;
}
'''

TESTS = r'''
static void strip_initial(void) { /* a GPU-owned baseline sends a batch down today's path: the model has none */
    for (GfxOut* o = g_out; o; o = o->next) o->initial = NULL;
}
static u64 stamps[1 << 16], stamps0[1 << 16];
static u32 nstamps(u64* out) { /* the write record's stamps, in order (render target calls are today's alone) */
    u32 n = 0;
    for (u32 i = 0; i < ncalls; i++)
        if (calls[i][0] == 2) out[n++] = calls[i][1] << 20 ^ calls[i][2];
    return n;
}
static u8 guest_mid[GUEST], guest_end[GUEST];
static u64 orgs_mid[1 << 14];
static u32 norgs_mid, nst0;
/* today's loop through frame FRAMES - 1, the state then, and frame FRAMES after it */
static void today(u32 seed, int n) {
    build(seed, n);
    strip_initial();
    ncalls = norgs = 0;
    for (u64 f = 1; f < FRAMES; f++) vk_buf_frame_done(f);
    note_orgs();
    memcpy(guest_mid, guest, GUEST);
    memcpy(orgs_mid, orgs, sizeof orgs[0] * norgs);
    norgs_mid = norgs;
    nst0 = nstamps(stamps0);
    vk_buf_frame_done(FRAMES);
    memcpy(guest_end, guest, GUEST);
    free_owned();
}
static void same_as_today(void) {
    norgs = 0;
    note_orgs();
    assert(!memcmp(guest, guest_mid, GUEST));
    assert(norgs == norgs_mid && !memcmp(orgs, orgs_mid, sizeof orgs[0] * norgs));
    assert(nstamps(stamps) == nst0 && !memcmp(stamps, stamps0, sizeof stamps[0] * nst0));
    assert(g_landed == FRAMES - 1 && !g_gfx_fly && !t_depth && !result_depth && !g_out_landing);
    for (GfxOut* o = g_out; o; o = o->next) assert(!o->fly && o->frame == FRAMES);
    vk_buf_frame_done(FRAMES);
    assert(!memcmp(guest, guest_end, GUEST) && !g_out);
    free_owned();
}
static int any_before_last(void) {
    for (GfxOut* o = g_out; o; o = o->next)
        if (o->frame < FRAMES) return 1;
    return 0;
}
static void check_model(void) {
    u32 beside = 0, partial = 0;
    g_perf_landing = 3, g_landing_masked = 1, g_perf_compare = 2;
    for (u32 seed = 1; seed <= 400; seed++) {
        const int n = 2 + (int)(seed % 40);
        today(seed, n);
        build(seed, n);
        strip_initial();
        ncalls = 0, g_landed = 0, g_gval = FRAMES;
        const int some = any_before_last();
        const unsigned images = image_frames;
        vk_lock();
        const int took = vk_land_beside(FRAMES - 1);
        if (!took) land_through(FRAMES - 1, 1);
        vk_unlock();
        assert(took == some && image_frames - images == FRAMES - 1); /* C's per frame, as today's */
        beside += took;
        same_as_today();
        /* B stopped by a full arena at a random run: the rest lands by today's loop in C */
        today(seed, n);
        build(seed, n);
        strip_initial();
        ncalls = 0, g_landed = 0, g_gval = FRAMES;
        vk_lock();
        if (vk_buf_fly_pick(1, FRAMES - 1)) {
            vk_unlock();
            free(g_fly.runs);
            g_fly.capruns = 1 + seed % 9;
            g_fly.runs = malloc(sizeof *g_fly.runs * g_fly.capruns);
            alloc_fails = 1;
            vk_buf_fly_write();
            alloc_fails = 0;
            partial += g_fly.partial;
            vk_lock();
            fly_c();
        } else
            land_through(FRAMES - 1, 1);
        vk_unlock();
        same_as_today();
    }
    assert(beside > 350 && partial > 100);
    printf("vulkan_beside: model, %u batches beside the same as today's loop, %u stopped part way the same\n", beside,
           partial);
}
/* four results over the same 64 bytes, B stopped at the third: C must not rebase the fourth (in the batch, not reached)
   again from the first, after the second rebased it in B. The first two change every byte, the third half of them */
static void check_crafted(void) {
    u8 want[64];
    for (int beside = 0; beside < 2; beside++) {
        build(3, 0);
        crowd = 1;
        u32 s = 11;
        GfxOut* r[4];
        for (int i = 0; i < 4; i++) {
            while ((r[i] = make(&s, 1 + (u64)i / 2))->size < 64) free(r[i]); /* room for 64 bytes in org and ptr */
            r[i]->addr = 0x5000, r[i]->size = 64, r[i]->initial = NULL, r[i]->composed = i > 0;
            memcpy(r[i]->org, guest + 0x5000, 64);
            for (u32 k = 0; k < 64; k++) r[i]->ptr[k] = r[i]->org[k] ^ (i == 2 && k >= 32 ? 0 : 1 + i);
            walk_add(r[i]);
            r[i]->in_out = 1;
            if (g_out_tail)
                g_out_tail->next = r[i];
            else
                g_out = r[i];
            g_out_tail = r[i];
        }
        crowd = 0, g_gfx_any = 1, g_landed = 0, g_gval = FRAMES;
        vk_lock();
        if (!beside)
            land_through(FRAMES - 1, 1);
        else {
            assert(vk_buf_fly_pick(1, FRAMES - 1));
            free(g_fly.runs);
            g_fly.capruns = 2, g_fly.runs = malloc(sizeof *g_fly.runs * 2); /* the third's run does not fit */
            alloc_fails = 1;
            vk_buf_fly_write();
            alloc_fails = 0;
            assert(g_fly.partial && g_fly.reached == 2);
            fly_c();
        }
        vk_unlock();
        if (!beside)
            memcpy(want, guest + 0x5000, 64);
        else
            assert(!memcmp(want, guest + 0x5000, 64));
        free_owned();
    }
}
/* a thread that waits as an access under vk_lock does, then reads or writes the bytes */
typedef struct {
    int how; /* 0 gpu_fly_wait, 1 gmem_ptr_untracked, 2 entry_host, 3 a compute landing's write, 4 land_through */
    u64 addr;
    u32 n;
    u8 got[64];
    volatile int done;
} Access;
static void* access_thread(void* p) {
    Access* a = p;
    vk_lock();
    if (a->how == 0) {
        gpu_fly_wait(guest + a->addr, a->n);
        memcpy(a->got, guest + a->addr, a->n);
    } else if (a->how == 1)
        memcpy(a->got, gmem_ptr_untracked(a->addr, a->n), a->n);
    else if (a->how == 2) {
        Vb e = {0, 0};
        memcpy(a->got, entry_host(&e, a->addr, a->n), a->n);
    } else if (a->how == 3) {
        gpu_fly_wait(guest + a->addr, a->n);
        memset(guest + a->addr, 0xC5, a->n);
    } else
        land_through(FRAMES - 1, 1);
    a->done = 1;
    vk_unlock();
    return NULL;
}
static GfxOut* changing(void) { /* a batch result B changes, whose first 64 bytes it writes */
    for (GfxOut* o = g_out; o; o = o->next)
        if (o->frame < FRAMES && o->size >= 64 && memcmp(o->ptr, o->org, 64)) return o;
    return NULL;
}
static void check_threads(void) {
    u32 waited = 0;
    for (u32 seed = 1; seed <= 60; seed++)
        for (int how = 0; how <= 4; how++) {
            build(seed, 30);
            strip_initial();
            GfxOut* o = changing();
            if (!o) {
                free_owned();
                continue;
            }
            g_landed = 0, g_gval = FRAMES;
            const u64 at = o->addr; /* o is freed by C */
            Access far = {0, GUEST - 64, 64, {0}, 0}, near = {how, at, 64, {0}, 0};
            vk_lock();
            assert(vk_buf_fly_pick(1, FRAMES - 1));
            const unsigned skipped = (unsigned)g_perf_bs.skipped;
            land_through(FRAMES - 1, 0); /* submit()'s landing during B: nothing */
            assert(g_landed == 0 && g_gfx_fly);
            vk_unlock();
            pthread_t tf, tn;
            pthread_create(&tf, NULL, access_thread, &far);
            pthread_join(tf, NULL); /* other bytes: at once, before B */
            assert(far.done);
            pthread_create(&tn, NULL, access_thread, &near);
            usleep(20000);
            assert(!near.done); /* the batch's bytes: it waits for B */
            vk_buf_fly_write();
            pthread_join(tn, NULL);
            waited++;
            if (how <= 2) assert(!memcmp(near.got, guest + at, 64)); /* B's bytes, read after it */
            const u64 c0 = g_perf_bs.joins;
            vk_lock();
            if (how == 4) /* the join ran C once: the lander's does nothing */
                assert(g_landed == FRAMES - 1 && !g_gfx_fly && !vk_buf_fly_finish());
            fly_c();
            vk_unlock();
            assert(g_landed == FRAMES - 1 && !g_gfx_fly && (how != 4 || c0 == g_perf_bs.joins));
            if (how == 3) /* the compute landing wrote after B */
                for (u32 k = 0; k < 64; k++) assert(guest[at + k] == 0xC5);
            (void)skipped;
            vk_buf_frame_done(FRAMES);
            free_owned();
        }
    /* a pointer taken before the batch and read in a later hold waits as well */
    build(5, 30);
    strip_initial();
    GfxOut* o = changing();
    assert(o);
    g_landed = 0, g_gval = FRAMES;
    vk_lock();
    u8* p = gmem_ptr_untracked(o->addr, 64);
    vk_unlock();
    vk_lock();
    assert(vk_buf_fly_pick(1, FRAMES - 1));
    vk_unlock();
    Access late = {0, (u64)(p - guest), 64, {0}, 0};
    pthread_t tl;
    pthread_create(&tl, NULL, access_thread, &late);
    usleep(20000);
    assert(!late.done);
    vk_buf_fly_write();
    pthread_join(tl, NULL);
    vk_lock();
    fly_c();
    vk_unlock();
    vk_buf_frame_done(FRAMES);
    free_owned();
    printf("vulkan_beside: threads, %u accesses waited for B and saw its bytes\n", waited);
    assert(waited > 200);
}
static void land_first(void) { vk_buf_frame_done(1); }
/* each reason: pick says 0, nothing in flight, nothing changed, the reason counted */
static void check_fallbacks(void) {
    g_perf_probes = 1;
    for (int why = FLYR_HOLD; why < FLYR_N; why++) {
        if (why == FLYR_ARENA || why == FLYR_NESTED) continue; /* not made to fail here, and nested below */
        build(7, why == FLYR_EMPTY ? 0 : 30);
        if (why != FLYR_INITIAL) strip_initial();
        else
            g_out->initial = (u8*)1, g_out->frame = 1;
        g_gpu_fly_hold = why == FLYR_HOLD, g_own_any = why == FLYR_OWN, image_due = why == FLYR_IMAGE;
        rt_hit = why == FLYR_TARGET, direct = why != FLYR_DST;
        const u64 before = g_perf_bs.today[why];
        memcpy(guest_mid, guest, GUEST);
        vk_lock();
        assert(!vk_buf_fly_pick(1, FRAMES - 1));
        vk_unlock();
        assert(g_perf_bs.today[why] == before + 1 && !g_gfx_fly && !memcmp(guest, guest_mid, GUEST));
        for (GfxOut* o = g_out; o; o = o->next) assert(!o->fly);
        g_gpu_fly_hold = g_own_any = image_due = rt_hit = 0, direct = 1;
        if (why == FLYR_INITIAL) g_out->initial = NULL;
        for (u64 f = 1; f <= FRAMES; f++) vk_buf_frame_done(f);
        free_owned();
    }
    /* a frame landed inside A's gmem_ptr frees results A holds: today's path, none of them touched (ASan) */
    for (u32 seed = 1; seed <= 40; seed++) {
        build(seed, 30);
        strip_initial();
        ptr_hook = land_first;
        const u64 before = g_perf_bs.today[FLYR_NESTED];
        vk_lock();
        assert(!vk_buf_fly_pick(1, FRAMES - 1) && !g_gfx_fly);
        vk_unlock();
        assert(g_perf_bs.today[FLYR_NESTED] == before + 1 && !ptr_hook);
        for (GfxOut* o = g_out; o; o = o->next) assert(!o->fly);
        for (u64 f = 2; f <= FRAMES; f++) vk_buf_frame_done(f);
        free_owned();
    }
    g_perf_probes = 0;
    /* unset: the inline waits read one int and never call the renderer */
    slow_calls = 0;
    gpu_fly_wait(guest, 64);
    gpu_fly_wait_all(GPU_FLY_MUST);
    assert(!slow_calls);
    printf("vulkan_beside: each fallback took today's path, the waits made no call unset\n");
}
int main(void) {
    check_model();
    check_crafted();
    check_threads();
    check_fallbacks();
    printf("vulkan_beside: all checks passed\n");
    return 0;
}
'''


def main():
    buffer = (SRC / "nx/gpu/vk/buffer.c").read_text()
    writeback = (SRC / "nx/gpu/vk/writeback.c").read_text()
    cmd = (SRC / "nx/gpu/vk/cmd.c").read_text()
    gpu_c = (SRC / "nx/gpu/gpu.c").read_text()
    gpu_h = (SRC / "nx/gpu/gpu.h").read_text()
    f, b = vc.function, vc.between
    body = vc.HEAD + EXTRA + "#define malloc test_malloc\n#define realloc test_realloc\n"
    body += b(buffer, "typedef struct GfxOut {", "} GfxOut;") + "} GfxOut;\n"
    body += "static GfxOut *g_prep, *g_prep_tail, *g_out, *g_out_tail;\n"
    body += f(buffer, "static u64 out_block(")
    body += b(buffer, "/* SNX_ARCEUS_PERF_COMPARE=2 (PERF-28 3): the results not landed yet", "/* e has landed")
    body += f(buffer, "void vk_buf_writable_abort(void) {")
    body += b(buffer, "static ChgRun g_chg_first[64];", "/* whether e's w bytes")
    body += b(buffer, "typedef void (*SpanFn)", "/* the run of R's page")
    body += f(buffer, "static int page_cmp(const void* a, const void* b) {")
    body += f(writeback, "static int in_guest(")
    body += b(writeback, "typedef struct {\n    u64 addr;", "static void landing_put(")
    for sig in ("static void landing_put(", "static void landing_note(", "static int write_changed(",
                "void vk_write_changed(", "int vk_write_changed_found("):
        body += f(writeback, sig)
    body += b(buffer, "typedef struct {\n    const GfxOut* e;", "/* the changed bytes of e's")
    for sig in ("static void out_span_page(", "static void out_spans(", "static int pages_meet(",
                "static int word_changed("):
        body += f(buffer, sig)
    body += b(buffer, "/* SNX_ARCEUS_PERF_COMPARE=2 (PERF-28 1b): the result whose", "/* e's changed runs into")
    for sig in ("static u32 out_changed_runs(", "static void rebase_bytes(", "static void rebase_span(",
                "static void out_rebase(const GfxOut* e, GfxOut* o) {"):
        body += f(buffer, sig)
    body += b(buffer, "static GfxOut** g_wcand;", "/* the composed results recorded after e")
    for sig in ("static void out_rebase_later(", "static void cap_step("):
        body += f(buffer, sig)
    body += "static void land_count(GfxOut* d) { (void)d; }\nstatic void land_copy(void) {}\n"
    body += "static void land_list(GfxOut* done);\nstatic u64 g_frames_done;\n" + f(buffer, "void vk_buf_frame_done(")
    body += f(buffer, "static void land_list(GfxOut* done) {")
    body += "enum {" + b(gpu_h, " GPU_FLY_RANGE,", "static inline void gpu_fly_wait(").split("};")[0] + "};\n"
    body += f(gpu_h, "static inline void gpu_fly_wait(") + f(gpu_h, "static inline void gpu_fly_wait_all(")
    body += b(buffer, "typedef struct {\n    const u8 *lo, *hi;\n} FlyRange;", "/* At the frame's submit")
    body += f(gpu_c, "u8* gmem_ptr_untracked(")
    body += f(buffer, "static const u8* entry_host(")
    for sig in ("static void fly_c(void) {", "static void land_through(u64 value, int must) {",
                "int vk_land_beside(u64 value) {"):
        body += f(cmd, sig)
    body += "#undef malloc\n#undef realloc\n" + b(vc.TESTS, "/* a batch: results", "/* every pending result's")
    body += b(vc.TESTS, "/* every pending result's original bytes", "/* lands frames 1 to FRAMES */") + TESTS
    cc = sys.argv[1:] or ["cc", "-std=gnu11", "-O2"]
    with tempfile.TemporaryDirectory() as d:
        c = pathlib.Path(d) / "beside.c"
        c.write_text(body)
        exe = pathlib.Path(d) / "beside"
        subprocess.run([*cc, "-I", str(SRC), "-DSNX_LANDING_PORTABLE", "-Wall", "-Wno-unused-function", "-o",
                        str(exe), str(c), str(SRC / "nx/gpu/vk/landing.c"), str(SRC / "nx/gpu/perf_times.c"),
                        "-lpthread"], check=True)
        return subprocess.run([str(exe)]).returncode


if __name__ == "__main__":
    sys.exit(main())
