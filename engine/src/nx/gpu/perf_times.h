/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_PERF_TIMES=1 (research 10:38, log only): where a frame's time goes on native. The frame job's cycle
   (posted, started, ended), the GPU thread's waits on an empty queue and inside the job by site, and the game's
   nvnSyncWait and nvnWindowAcquireTexture. One line every PERF_TIMES_WINDOW presents. With it off, the sites test
   g_perf_times and read no clock. Any thread may add; the line is written on the GPU thread. */
#ifndef SNX_GPU_PERF_TIMES_H
#define SNX_GPU_PERF_TIMES_H
#include <stddef.h>
#include <stdint.h>

#define PERF_TIMES_WINDOW 600u
#define PERF_TIMES_NAMED 8u /* the GPU thread's other waits, by what gpu_wait_note names them */

enum {
    PT_POST_START, /* posted on the game's thread to frame_run entered */
    PT_START_END,  /* frame_run entered to its syncpoint increment queued (frame_end) */
    PT_END_POST,   /* that increment to the next present's post: the game's part that does not overlap the replay */
    PT_PERIOD,     /* post to post */
    PT_IDLE,       /* the GPU thread on an empty queue */
    PT_SHOW,       /* frame_show, the present */
    PT_TL_RECYCLE, /* timeline_wait for a frame slot's previous use (frame_recycle) */
    PT_TL_FINISH,  /* timeline_wait in vk_finish */
    PT_TL_SUBMIT,  /* timeline_wait for a submission's results (vk_wait_submit: releases and landings) */
    PT_REC_SYNC,   /* vk_rec_sync: the recorder catching up */
    PT_REC_FULL,   /* rec_push on a full ring */
    PT_SYNC_WAIT,  /* the game's nvnSyncWait, the whole call */
    PT_ACQUIRE,    /* the game's nvnWindowAcquireTexture, the whole call */
    PT_RELEASE_HOLD, /* the release thread holding vk_lock in release_through (the landings and releases) */
    PT_PASS_END,     /* vk_render_end ending an open pass (PERF-11, research 20:41) */
    PT_PASS_BEGIN,   /* a game pass begun: from the end of the pass before to its vkCmdBeginRendering recorded */
    PT_REL_LOCK_ALL, /* the release thread waiting to take vk_lock, the whole frame (PERF-14, research 22:35) */
    /* PERF-18 (research 04:14), the GPU thread's CPU time in buffer.c */
    PT_SRC_WALK,     /* gfx_gpu_src past pend_hit: its walks over the pending results, to before a landing or barrier */
    PT_CLASS_WALK,   /* PERF-17's class walk of a composed input (PERF_TIMES' own cost) */
    PT_COMPOSE_EMPTY, /* gfx_composed when the composition copied no byte: staging, guest bytes, the overlay walk */
    PT_COMPOSE_COPY,  /* the same when it copied */
    PT_COMPOSE_W_EMPTY, /* vk_buf_copy_writable over pending results, after gfx_gpu_src, copying no byte */
    PT_COMPOSE_W_COPY,  /* the same when it copied */
    /* a present job's tail, each from its syncpoint increment queued at frame_end (PERF-10, research 19:46) */
    PT_TAIL_SUBMIT, /* to the recorder thread handing the submission the increment waits for to the driver */
    PT_TAIL_DONE,   /* to the release thread's wait for that submission returning */
    PT_TAIL_INCR,   /* to the increment performed, after the landings before it */
    PT_TAIL_FREE,   /* to the end of the release_through that performed it: vk_lock released */
    PT_TAIL_WAKE,   /* to the game's next nvnSyncWait returning after the increment */
    /* a graphics submission, from the recorder's vkQueueSubmit2 returning to its completion first seen (PERF-12) */
    PT_SUB_DONE,  /* every submission */
    PT_SUB_LAST5, /* the last five a present queued before its frame_end */
    /* a tail's window, from its increment queued to tail_done, by what the release thread did in it (PERF-14) */
    PT_WIN_GPU,   /* blocked in vk_wait_submit or vk_compute_wait */
    PT_WIN_LOCK,  /* waiting to take vk_lock */
    PT_WIN_HOLD,  /* holding vk_lock in release_through: the landings and releases */
    PT_WIN_OTHER, /* the rest: its queue's lock and condition, and between those */
    PT_SITES
};
/* counts a frame, not times */
enum {
    PC_WB_OUTPUTS, /* graphics storage outputs landed (buffer.c, as g_wb_outputs counts them) */
    PC_WB_BYTES,   /* their bytes */
    PC_WC_BYTES,   /* bytes vk_write_changed wrote to guest memory, the ones the shader changed */
    PC_TAIL_SUBMITS, /* graphics submissions from one present's increment to the next, the increment's own included */
    PC_TAIL_AFTER,   /* of those, the ones not complete when the increment was queued */
    PC_PASS_BEGINS,  /* game render passes begun */
    PC_END_OTHER,    /* open passes ended, by the reason the caller gave: none named */
    PC_END_GPU_SRC,  /* gfx_gpu_src's barrier for a draw that reads an earlier result */
    PC_END_COPY,     /* a buffer copy (overlay, owned input, patch, staging) */
    PC_END_IMAGE,    /* an image upload, copy, blit or readback */
    PC_END_TARGET,   /* the next draw's targets or their layouts differ */
    PC_END_QUERY,    /* a samples-passed report (vk_query_report, PERF-19) */
    PC_SRC_BARRIERS, /* gfx_gpu_src's barriers */
    PC_SRC_CLEAN,    /* of those, ones whose first result read no draw bound for writing since the one before */
    PC_SRC_UNKNOWN,  /* of those, ones whose result's last writer was not kept (the table's slot taken) */
    PC_REL_DEPTH,    /* releases queued and not yet performed when a present's increment was queued */
    PC_WIN_OUTPUTS,  /* storage outputs the release thread landed inside a tail's window */
    PC_SRC_WALKS,    /* gfx_gpu_src calls past pend_hit (PT_SRC_WALK's) */
    PC_SRC_RESULTS,  /* the pending results those walked, the first walk's */
    /* from here on printed as totals, not a frame's mean */
    PC_TAIL_UNGATED, /* increments that waited for no submission: no submit or done time */
    PC_TAIL_PARTIAL, /* tails closed with a time missing (performed by another path, or no wait after it) */
    PC_TAIL_LOST,    /* tails whose slot was needed again before they closed */
    PC_COUNTS
};

typedef struct {
    uint64_t sum, max, count;
} PerfTime;
/* where a graphics submission came from (PERF-12, research 21:46): set by the caller just before vk_submit */
enum {
    SW_OTHER = 0,
    SW_SLICE,    /* vk_frame_check: the recording time a submission holds */
    SW_RELEASE,  /* release.c submit_through: a release's gate */
    SW_FINISH,   /* vk_finish */
    SW_FRAME,    /* vk_frame_next: a frame slot's end */
    SW_PRESENT,  /* the renderer's present */
    SW_IMAGE,    /* render.c image_flush_at */
    SW_COMPUTE,  /* compute.c */
    SW_READBACK, /* owned.c, a landing of owned memory */
    SW_SITES
};
/* the release thread's recent intervals, for a tail's window (its own: written and read on that thread) */
enum { REL_GPU = 0, REL_LOCK, REL_HOLD, REL_STATES };
#define PERF_RELS 512u
typedef struct {
    uint64_t start, end, outputs;
    int state;
} PerfRel;
#define PERF_SUBS 256u /* the submissions kept, by number: one far behind is counted lost */
#define PERF_SUB_LEADS 3u /* the last 1, 2 and 4 ms before a present's frame_end */
typedef struct {
    uint64_t v, queued, handed, seen;
    int state; /* the site << 2, and SUB_SEEN and SUB_LAST5: whoever sets the second adds the last five's time */
    /* PERF-33, with SNX_ARCEUS_PERF_PROBES: the PCY_* kinds of result queued for the release thread in submission
       carry_v (registered while it is recorded, before it is queued here), and when the last of its landings was */
    uint64_t carry_v, landed;
    int carry;
    uint32_t kind_n[5]; /* and how many of each kind (PCY_* by bit), with carry_v (research 08:52) */
} PerfSub;
enum { PCY_OUT = 1, PCY_IMAGE = 2, PCY_COMPUTE = 4, PCY_PAYLOAD = 8, PCY_SAMPLES = 16, PCY_KINDS = 5 };
_Static_assert(PCY_KINDS == sizeof((PerfSub){0}.kind_n) / sizeof(uint32_t), "a count per kind");
/* A tail's times, each written once by the thread that reaches that point, and closed when both its vk_lock release
   and the game's wake are in */
#define PERF_TAILS 4u
typedef struct {
    uint64_t armed, gate, seq, t0, t_submit, t_done, t_incr, t_free, t_wake;
    uint64_t first, last; /* PERF-33: its frame's submissions, after the previous tail's last up to its own */
    int closes;
} PerfTail;
typedef struct {
    PerfTime site[PT_SITES];
    PerfTail tail[PERF_TAILS];
    uint64_t tail_next, tail_handed, tail_mark; /* the next tail's number, the last value handed, the submit count */
    int tail_arm;                               /* the GPU thread's: frame_end armed the next increment */
    PerfSub sub[PERF_SUBS];
    /* atomic: a submission may come from any thread that holds vk_lock (a readback, the front end's refresh), and
       frame_end and the line run on the GPU thread without it (reviewer 21:58) */
    uint64_t sub_seen;                               /* the highest submission whose completion was seen */
    uint64_t sub_last;                               /* the last queued */
    uint64_t sub_site[SW_SITES][1 + PERF_SUB_LEADS]; /* a window's submissions by site: all, and in the last 1/2/4 ms */
    uint64_t sub_lost, sub_unhanded;                 /* never seen; seen before its handing was published */
    uint64_t sub_end;                                /* the GPU thread's: the last frame_end */
    PerfRel rel[PERF_RELS];
    uint64_t rel_next;  /* the release thread's */
    uint64_t wb_total;  /* storage outputs landed so far, never reset: atomic */
    struct {
        const char* what; /* 0 while free: claimed with a compare-exchange */
        PerfTime t;
    } named[PERF_TIMES_NAMED];
    uint64_t count[PC_COUNTS];
    uint64_t overlaps; /* presents posted before the previous frame job ended */
    uint64_t named_lost;
} PerfTimes;

extern int g_perf_times;
/* SNX_ARCEUS_PERF_PROBES=1 with PERF_TIMES (PERF-29 and the index bound's counts): the perf-p29 line and what it
   counts, which costs in the code it times. 0 unless both are set */
extern int g_perf_probes;
extern PerfTimes g_perf;
/* SNX_ARCEUS_PERF_SUBMIT_US, bench only (research 20:51): the GPU thread's recording time a submission holds at most
   (vk/cmd.c vk_frame_check), upstream's 1 ms unless set to 250 to 8000 us. Read once at start */
#define PERF_SUBMIT_EVERY_NS 1000000ull
extern uint64_t g_perf_submit_every_ns;
/* SNX_ARCEUS_PERF_COMPUTE_SUBMIT=0, an experiment (PERF-13, research 22:09): a deferred dispatch's results are not
   submitted at once (vk/compute.c), so they go with the next submission. 1 unless set to 0. Read once at start */
extern int g_perf_compute_submit;

/* why the next vk_render_end ends the pass, a PC_END_* count less PC_END_OTHER: set just before the call */
enum { PE_OTHER = 0, PE_GPU_SRC, PE_COPY, PE_IMAGE, PE_TARGET, PE_QUERY, PE_N };
extern int g_perf_pass_why; /* the GPU thread's */
static inline void perf_pass_why(int why) {
    if (g_perf_times) g_perf_pass_why = why;
}

void perf_time_add(PerfTimes* p, int site, uint64_t ns);
void perf_time_named(PerfTimes* p, const char* what, uint64_t ns);
void perf_count(PerfTimes* p, int counter, uint64_t n);
/* The tail's points. arm and take on the GPU thread: frame_end arms, the next increment it queues takes */
void perf_tail_arm(PerfTimes* p);
int perf_tail_take(PerfTimes* p);
/* the armed increment queued as release seq, waiting for submission gate (0: none), with the graphics submissions
   recorded and done so far */
void perf_tail_queued(PerfTimes* p, uint64_t seq, uint64_t gate, uint64_t submitted, uint64_t done, uint64_t now);
void perf_tail_handed(PerfTimes* p, uint64_t value, uint64_t now);  /* the recorder handed submission value over */
void perf_tail_done(PerfTimes* p, uint64_t value, uint64_t now);    /* the release thread's wait for value returned */
void perf_tail_performed(PerfTimes* p, uint64_t seq, uint64_t now); /* release seq, an increment, performed */
void perf_tail_freed(PerfTimes* p, uint64_t now);                   /* a release_through ended, vk_lock released */
void perf_tail_woke(PerfTimes* p, uint64_t now);                    /* the game's nvnSyncWait returned */
/* the submissions: queued and its site (the GPU thread, in submit), handed over (the recorder), completion seen up to
   value (any thread that read the timeline), and a present's frame_end (the GPU thread) */
extern int g_perf_submit_why; /* an SW_* for the next submit, used once: set and taken by whoever submits */
static inline void perf_submit_why(int why) {
    if (g_perf_times) __atomic_store_n(&g_perf_submit_why, why, __ATOMIC_RELAXED);
}
void perf_sub_queued(PerfTimes* p, uint64_t v, int site, uint64_t now);
void perf_sub_handed(PerfTimes* p, uint64_t v, uint64_t now);
void perf_sub_seen(PerfTimes* p, uint64_t value, uint64_t now);
void perf_sub_frame_end(PerfTimes* p, uint64_t now);
/* PERF-33 (research 05:12, 05:16), log only, with SNX_ARCEUS_PERF_PROBES=1: the frame hand-off. A result of kind
   (PCY_*) queued for the release thread in submission v (release.c vk_release_result, the one place), a guest
   result of v landed (under vk_lock: one v never registered is counted), and a compute batch's results landed */
void perf_sub_carry(PerfTimes* p, uint64_t v, int kind);
void perf_sub_landed(PerfTimes* p, uint64_t v, uint64_t now);
enum { PH_SUBMIT, PH_SUBMIT_LOCK, PH_SUBMIT_ADD, PH_SUBMIT_SCAN, PH_FENCE, PH_PRESENT, PH_PARTS };
#define PH_POS 5u /* where a frame's last carrying submission sits: the last, 1, 2, 3 before it, or 4 and more */
typedef struct { /* all atomic */
    uint64_t frames, subs, carry, kinds[PCY_KINDS], pos[PH_POS + 1]; /* pos[PH_POS]: a frame with none */
    uint64_t pos_noimg[PH_POS + 1]; /* the same with the storage images left out (research 05:40) */
    uint64_t to_land, to_carry_gpu, to_last_gpu, to_incr;           /* ns from the increment's queueing (frame_end) */
    uint64_t land_before, compute_last, compute_after, compute_ns, lost, unregistered;
    uint64_t n[PH_PARTS], ns[PH_PARTS], ops, marked; /* ops walked whole, and read at a mark (PERF_SCAN) */
    /* the storage images' writes (render.c image_record): how many, their draws' places after the last frame_end,
       and the last one's place, frame (its frame's draws), guest address, size and format. The GPU thread's */
    uint64_t img_n, img_at, draws0, frame_draws, img_last_at, img_last_frame, img_gva, img_w, img_h, img_fkey;
    int img_open; /* an image was written in the frame not yet presented */
    /* research 08:52: the frame's last submission's results by kind, how many and in how many frames; each kind's
       last registration's place among the frame's draws (res_at, under vk_lock), summed with the frames it came in;
       and the frame's draws summed over the presents */
    uint64_t last_n[PCY_KINDS], last_has[PCY_KINDS], res_at[PCY_KINDS], res_place[PCY_KINDS], res_frames[PCY_KINDS];
    uint64_t draws_sum, presents;
    uint64_t stamp_off, stamp_pending; /* PERF-34: stamp reads off the renderer's lock, and those a notice met */
    /* research 12:38: the combined notices, their ranges and the distinct 4 KB pages those cover; the release
       thread's waits that ended with work to take, and without (woken while nothing was let go) */
    uint64_t notices, notice_ranges, notice_pages, rwait_work, rwait_idle;
    /* PERF-35 (research 13:57): the landed results, their bytes, and those on pages with no CPU write since the
       original was taken, no other GPU-side write over them since, no composed result there, and all three (by page,
       PLD_*); bytes whose page could not be told; and one bracket each around a landing's write loop and around a
       plain copy of the same bytes */
    uint64_t land_results, land_bytes[5], land_unknown, land_loop_ns, land_copy_ns;
    uint64_t land_steps[4]; /* the masked pass's 64-byte steps: all, none stored, all 64, some (landing.c's count) */
} PerfHandoff;
extern PerfHandoff g_perf_handoff;
void perf_handoff_compute(PerfHandoff* h, uint64_t now);
void perf_handoff_time(PerfHandoff* h, int part, uint64_t ns, uint64_t ops, uint64_t marked);
/* at a present's frame_end, drawn the draws so far; at a storage image's write, drawn and the image */
void perf_handoff_frame_draws(PerfHandoff* h, uint64_t drawn);
void perf_handoff_result(PerfHandoff* h, int kind, uint64_t drawn); /* a registration of kind with drawn draws so far */
static inline void perf_stamp_read(PerfHandoff* h, int pending) {
    __atomic_fetch_add(pending ? &h->stamp_pending : &h->stamp_off, 1, __ATOMIC_RELAXED);
}
void perf_handoff_image(PerfHandoff* h, uint64_t drawn, uint64_t gva, uint32_t w, uint32_t hgt, uint32_t fkey);
size_t perf_handoff_line(PerfHandoff* h, uint32_t frames, char* out, size_t size);
size_t perf_land_line(PerfHandoff* h, uint32_t frames, char* out, size_t size); /* PERF-35's, its own line */
/* the release thread spent [start, end) in state (REL_*), landing outputs storage outputs while holding vk_lock */
void perf_rel_interval(PerfTimes* p, int state, uint64_t start, uint64_t end, uint64_t outputs);
/* Writes the window's line into out and starts the next window. frames: the presents it covers. */
size_t perf_times_line(PerfTimes* p, uint32_t frames, char* out, size_t size);

/* PERF-16 (research 23:05): the buffer copies by call site, the passes they ended by the pass's first target, and
   what the draws on a target did with an earlier draw's result. The GPU thread's: the hooks (vk/render.c) count
   only there, the rest as elsewhere, and the line is written there too */
enum {
    PK_COMPOSE,    /* buffer.c gfx_overlay for a read (gfx_composed): pending results copied over guest bytes */
    PK_COMPOSE_W,  /* gfx_overlay for a writable binding */
    PK_ATLAS,      /* atlas_fill: an aliased draw's pages */
    PK_OWNED_BASE, /* writable_owned: an owned input's baseline */
    PK_PATCH,      /* patch_apply: the dirty runs of an own buffer */
    PK_OWN,        /* make_own_: a buffer of its own filled */
    PK_OWNED_READ, /* buf_get_owned: a range over bytes the GPU holds */
    PK_DMA,        /* dma.c: a copy engine operation */
    PK_SITES
};
/* a binding over pending results (gfx_gpu_src): read in place, composed by a copy (why), or landed first (why) */
enum {
    PS_REUSE,   /* the result's buffer bound itself */
    PS_SEVERAL, /* composed: more than one result over the range */
    PS_PART,    /* composed: one result, covering the range only in part */
    PS_ALIGN,   /* composed: one result, at an offset the binding does not accept */
    PS_SUBMIT,  /* composed: a writable binding of a result from an earlier submission */
    PS_OLD,     /* landed: a result older than the frame before, or of the frame before with its bytes changed */
    PS_CHANGED, /* landed: the guest wrote the result's original bytes since */
    PS_N
};
enum { PB_INPUT, PB_READ, PB_WRITE, PB_KINDS }; /* buf_get, vk_buf_copy, vk_buf_copy_writable */
/* PERF-17 (research 02:42): what a composed input binding (PB_INPUT) is, by its pending results that share a byte */
enum {
    PI_NONE,      /* none shares a byte: page neighbours only */
    PI_ONE,       /* the newest one covers the range at its address (level 1 binds it) */
    PI_ONE_EXACT, /* not one, but one at the vertex stream's exact length (level 2 binds it) */
    PI_RUN,       /* several at one address each, tiling the range in one buffer with the guest's spacing */
    PI_SEVERAL,   /* several, anything else */
    PI_PART,      /* one, not covering the range */
    PI_CLASSES
};
/* the binding being looked up, set by its caller (vk/render.c, vk/compute.c): the GPU thread's */
enum { PBK_OTHER, PBK_VERTEX, PBK_INDEX, PBK_CONST, PBK_STORAGE, PBK_XFB, PBK_DISPATCH, PBK_KINDS };
/* a writable result handed out, by producer (research 02:48): new from vk_buf_copy_writable by its caller or kind, an
   atlas page (draw_output_page), rewritten in place, or a repeated binding of a prepared one */
enum { PR_ORDINARY, PR_OWNED, PR_COMPOSED, PR_XFB, PR_DISPATCH, PR_ATLAS, PR_INPLACE, PR_REPEAT, PR_N };
extern int g_perf_bind;
extern uint64_t g_perf_exact; /* a vertex stream's exact length when it is looked up at a longer one, else 0 */
static inline void perf_bind(int kind, uint64_t exact) {
    if (g_perf_times) g_perf_bind = kind, g_perf_exact = exact;
}
enum {
    PX_LAST_PENDING, /* vertex streams whose last vertex was unknown: the indices share a page with a pending result */
    PX_EXACT,        /* vertex streams looked up at their exact length (level 2) */
    PX_BARRIER,      /* the reuse path's barriers recorded */
    PX_SKIPPED,      /* and skipped (level 3) */
    PX_WIDE,         /* classified ranges of more than 16 pages: a result on another address meets them by page */
    PX_OVERLAY_SKIPPED, /* writable compositions whose gfx_overlay was skipped: no result shares a byte (level 4) */
    PX_OVERLAY_ENTERED, /* and those that entered it */
    PX_QUERY_REPORTS, /* samples-passed reports queued (vk_query_report, PERF-19) */
    PX_QUERY_KEPT,    /* of those, left the pass open (SNX_ARCEUS_PERF_QUERY=1) */
    PX_QUERY_GUARD,   /* vk_query_room's copies: half the query ring not copied (a report, reset, enable, draw) */
    PX_N
};
/* PERF-20 (research 05:10): vk_buf_copy_writable by path, and its steps, each a time */
enum { PW_PLAIN, PW_COMPOSED_EMPTY, PW_COMPOSED_BYTES, PW_OWNED, PW_PATHS };
enum {
    PWS_SETUP,    /* gmem_need, range_owned and the owned baseline */
    PWS_RECORD,   /* gfx_out_new */
    PWS_ORIGINAL, /* the original bytes: read_untracked, or guest_bytes and their copy */
    PWS_MAP,      /* out_map */
    PWS_STAGE,    /* vk_stage_output */
    PWS_COPY,     /* into staging: memcpy, or writable_owned's GPU copies */
    PWS_OVERLAY,  /* gfx_overlay */
    PWS_TAIL,     /* the rest: the record filled and linked */
    PWS_N
};
/* a transform feedback binding: whether the draw provably writes all of it (captured vertices times the stride, no
   geometry or tessellation stage), and why not when it can't be told */
enum { PXF_WHOLE, PXF_SHORT, PXF_STAGE, PXF_RESUME, PXF_TOPOLOGY, PXF_N };
enum { PXP_POINTS, PXP_LINES, PXP_LINE_STRIP, PXP_TRIANGLES, PXP_TRIANGLE_STRIP, PXP_TRIANGLE_FAN, PXP_OTHER };
/* the vertices a capture writes for count vertices of a primitive kind (PXP_*), times instances: ~0 when unknown */
uint64_t perf_xfb_vertices(int prim, uint64_t count, uint64_t instances);
/* PERF-22 (research 06:40): buf_get's slow path, past its checks, by what it did, why, its bytes and its time */
enum {
    PO_STAGED,     /* copied into this frame's staging ring */
    PO_OWN_MADE,   /* a buffer of its own made and filled (make_own) */
    PO_OWN_SAME,   /* its own buffer kept: hashed, unchanged */
    PO_OWNED_READ, /* bytes the GPU holds, copied by the GPU (buf_get_owned) */
    PO_PATCH,      /* its own buffer patched: the dirty runs copied (patch_own, patch_dirty) */
    PO_N
};
enum { POW_NEW, POW_NOBUF, POW_SUBMIT, POW_DIRTY, POW_WRITTEN, POW_EPOCH, POW_N }; /* buf_get's ST_* reasons */
enum { POT_HASH, POT_CREATE, POT_REST, POT_N }; /* guest bytes, hash and copy; make_own; the rest */
#define PERF_OWN_USES 6u /* draws using an own buffer before it is dropped: 0, 1, 2-3, 4-15, 16-63, 64 or more */
/* the graphics results landed lately, by guest page, for buf_get's slow path: under vk_lock, both sides. PERF-25 keeps
   for each the frame whose staging holds its buffer (made), the submission open when it landed, the GPU-side write
   generation and the write tracking's clock after it landed, its landing's order (seq) and whether its bytes were
   counted as kept alive (kept, in its first page's slot) */
typedef struct {
    uint64_t page, start, end, frame;
    uint64_t made, submit, gen, seq;
    uint32_t wclock, kept;
} PerfLanded;
#define PERF_LANDED 8192u
#define PERF_LANDED_PAGES 16u /* a result's first pages are entered: a range further into a bigger one is missed */
extern PerfLanded g_perf_landed[PERF_LANDED];
void perf_landed_add(PerfLanded* t, uint64_t start, uint64_t size, uint64_t frame, uint64_t made, uint64_t submit,
                     uint64_t gen, uint32_t wclock);
/* whether [gva, gva + len) lies inside one result landed in frame or the one before */
int perf_landed_inside(const PerfLanded* t, uint64_t gva, uint64_t len, uint64_t frame);
/* PERF-25: the result or two adjoining results [gva, gva + len) lies inside, landed in frame or the one before, into
   r: PLK_WHOLE (its whole range), PLK_PART, PLK_TWO (r[0] holds gva, r[1] the end), else PLK_NONE */
enum { PLK_NONE, PLK_WHOLE, PLK_PART, PLK_TWO };
int perf_landed_find(const PerfLanded* t, uint64_t gva, uint64_t len, uint64_t frame, PerfLanded r[2]);
/* whether a result that landed after seq shares a byte with [gva, gva + len), by the table (its first 16 pages) */
int perf_landed_later(const PerfLanded* t, uint64_t seq, uint64_t gva, uint64_t len, uint64_t frame);
/* r's bytes the first time it is asked for after its buffer was freed, else 0 */
uint64_t perf_landed_keep(PerfLanded* t, const PerfLanded* r);

/* PERF-23 (research 19:40): GPU-side writes by GPU page, kept to the byte, in two records: g_perf_stamps as stamped
   (what gpu_mem_dirty_since reads: a landed result's whole binding), g_perf_real as the bytes written (a result's
   changed runs). Each page holds the newest generation of up to PERF_STAMP_RUNS runs of its bytes (two runs merged
   into one when a write makes more), and apart from that the span of every byte ever written on it, in 16 byte steps */
enum {
    PSK_RESULT,  /* a landed result's binding, as stamped (writeback.c) */
    PSK_GPU,     /* another GPU-side write */
    PSK_PUBLISH, /* an ownership publication in CPU addresses (gpu_mem_written_cpu from vk_own_publish) */
    PSK_LANDED,  /* a run of a result's bytes that changed, as landed: g_perf_real only */
    PSK_N
};
#define PERF_STAMP_RUNS 6u
#define PERF_STAMP_SLOTS (1u << 17) /* pages kept: the one written longest ago in its probe makes room */
#define PERF_STAMP_WIDE 4096u       /* a write over more pages than this is kept as one wide run */
#define PERF_STAMP_WIDES 256u
typedef struct {
    uint64_t gen;
    uint16_t lo, hi; /* bytes [lo, hi) of the page */
    uint8_t kinds;   /* bits of PSK_* */
} PerfStampRun;
typedef struct {
    uint64_t page;  /* page + 1, 0 free */
    uint64_t newest;
    uint64_t floor; /* made after a page of its hash made room: its history before this is unknown */
    uint32_t n;
    PerfStampRun r[PERF_STAMP_RUNS];
} PerfStampPage;
typedef struct {
    uint64_t start, end, gen;
    uint8_t kinds;
} PerfStampWide;
typedef struct {
    PerfStampPage* page; /* PERF_STAMP_SLOTS, made at the first stamp */
    uint8_t* span[1u << 16];  /* by 16 MiB of GPU addresses: two bytes a page, first and last 16 byte step, made then */
    uint64_t* gone;           /* by a hash of the page: the newest generation on a page that made room, made then */
    PerfStampWide wide[PERF_STAMP_WIDES];
    uint64_t nwide;           /* ever: past PERF_STAMP_WIDES the oldest are gone */
    uint64_t wide_gone;       /* the newest generation of a wide run that made room */
    int failed, span_lost;    /* no memory (every answer unknown), a span not kept (every span unknown): atomic */
    uint64_t stamps, pages, merged, wides; /* for the line, atomic: taken by it */
} PerfStamps;
extern PerfStamps g_perf_stamps, g_perf_real;
/* a stamp of gen on [a, a + size) by kind (PSK_*), under the writers' lock */
void perf_stamp_add(PerfStamps* t, uint64_t a, uint64_t size, uint64_t gen, int kind);
enum { PSA_NONE, PSA_IN, PSA_UNKNOWN };
/* whether a byte of [a, a + len) was stamped after since (PSA_*). *outside: 1 when on each of its pages the range is
   outside every byte ever stamped there, 0 not, -1 unknown. *kinds: who stamped its pages after since (PSK_* bits) */
int perf_stamp_ask(const PerfStamps* t, uint64_t since, uint64_t a, uint64_t len, int* outside, unsigned* kinds);
/* PERF-23: buf_get's slow path by step, with the steps before it that led there (a patch that failed, the written
   check). For the patch outcome POS_PATCH is the patch that succeeded */
enum { POS_PATCH, POS_WRITTEN, POS_WHY, POS_OWNED, POS_BYTES, POS_HASH, POS_SAMPLE, POS_USE, POS_OTHER, POS_N };
/* an own buffer found the same after a GPU-side write, by the stamps: none in range outside the span ever stamped
   on its pages, none in range inside it, none with the span unknown, one in range, not known */
enum { PSQ_OUT, PSQ_SPAN, PSQ_SPAN_UNKNOWN, PSQ_IN, PSQ_UNKNOWN, PSQ_N };
/* which tests clear an own_same after a GPU-side write (research 20:55): no byte of its range in a binding as stamped,
   its range also outside its pages' spans as stamped, no byte of it in a changed run as written */
enum { PCL_BYTES = 1, PCL_SPAN = 2, PCL_REAL = 4 };
#define PERF_WPAGES 4096u /* the host pages a window finds written under own buffers found the same */
typedef struct {
    uint64_t page; /* + 1, 0 free */
    uint64_t last; /* the frame it was last seen */
    uint32_t frames;
} PerfWPage;
/* a pending result sharing a byte with the range, oldest first, for perf_inplace_class */
typedef struct {
    uint64_t addr, size, buf, off;
    int same;  /* it shares the bytes at its own address, not only through another one */
    int exact; /* it shares a byte with the exact length's range */
} PerfResult;
#define PERF_RESULTS 32u /* a walk keeps this many: more are several */
/* the class of [gva, gva + len) over its n results (n may exceed PERF_RESULTS: several), exact 0 or the shorter
   length a vertex stream reads */
int perf_inplace_class(const PerfResult* r, uint32_t n, uint64_t gva, uint64_t len, uint64_t exact);
typedef struct {
    uint64_t gva;
    uint32_t w, h, fmt;
} PerfTarget;
#define PERF_COPY_TARGETS 64u
#define PERF_COPY_PRINT 4u /* the targets printed, most copy pass ends and composed or landed reads first */
#define PERF_COPIES_LINE 2000u /* the line's size at most: the log keeps 2047 bytes of one */
typedef struct {
    PerfTarget t; /* gva 0: free */
    uint64_t draws, passes;
    uint64_t why[PE_N];                               /* its passes ended, by the reason (PE_*) */
    uint64_t ends[PK_SITES], bytes[PK_SITES];         /* of the copy ends, by site, with the bytes that copy moved */
    uint64_t src[PB_KINDS][PS_N];                     /* its draws' bindings over pending results */
    uint64_t cls[PI_CLASSES];                         /* its draws' composed input bindings (PERF-17) */
    uint64_t own[PO_N], own_landed[PO_N];             /* its draws' buf_get slow paths, and of them inside a
                                                         landed result, by outcome (PERF-22, reviewer N1) */
    uint64_t lnd_ends;                                /* PERF-25: passes those inside a landed result ended */
} PerfCopyTarget;
typedef struct {
    uint64_t ops[PK_SITES], bytes[PK_SITES], ends[PK_SITES]; /* every copy, the ones that ended a pass, all targets */
    uint64_t src[PB_KINDS][PS_N];                            /* every binding over pending results */
    uint64_t bind[PBK_KINDS][PS_N];                          /* the same by what is bound (PERF-17) */
    uint64_t cls[PI_CLASSES];                                /* every composed input binding */
    uint64_t empty[PK_SITES], empty_bytes[PK_SITES];         /* compositions that copied no byte, the bytes staged */
    uint64_t extra[PX_N];
    uint64_t made[PR_N];
    uint64_t wr_ns[PW_PATHS][PWS_N], wr_calls[PW_PATHS], wr_bytes[PW_PATHS]; /* PERF-20 */
    uint64_t xfb[PXF_N], xfb_bound, xfb_captured; /* bindings by class, their bytes, the bytes provably captured */
    uint64_t own_n[PO_N][POW_N], own_bytes[PO_N], own_landed[PO_N], own_ns[PO_N][POT_N]; /* PERF-22 */
    uint64_t own_uses[PERF_OWN_USES];                                                  /* at each own drop */
    uint64_t step_ns[PO_N][POS_N], step_n[PO_N][POS_N]; /* PERF-23: the slow path by step, and the calls it ran in */
    uint64_t gsame[2][PSQ_N], gkinds[2][PSK_N]; /* own_same after a GPU-side write by each record (as stamped, as
                                                   written), and of those with none in range the writers on their
                                                   pages */
    uint64_t probe_ns, probe_n; /* this probe's own asks and page walks after a slow path's end */
    uint64_t cleared[8];        /* own_same after a GPU-side write by which tests clear it (PCL_* bits) */
    uint64_t csame[2], cbig, cbytes, cbytes_written; /* own_same after a CPU write: tracked or not, at or past the
                                                        patch size, bytes, bytes on the pages written */
    uint64_t quiet, quiet_ns; /* written checks on own buffers that found nothing */
    uint64_t sample_skips;    /* PERF-24: full checks that took no sample */
    /* PERF-25: buf_get's lookups inside a landed result, staged [0] or own_made [1]: by the range (PLK_* - 1), the
       submissions since the landing (0, 1, 2-3, more), its buffer alive [0] or freed 0, 1, 2-3, more submissions
       before, cleared by page tests, with the byte record, not, what wrote it (PLW_*), ns at the site, pass ends */
    uint64_t lnd_n[2][3], lnd_age[2][4], lnd_freed[2][5], lnd_clear[2][3], lnd_wrote[2][4], lnd_ns[2], lnd_ends[2];
    uint64_t lnd_kept;   /* the bytes of results asked for after their buffer was freed, each once */
    uint64_t pass_end_n; /* passes ended, and the target of the last (end_slot), for PERF-25 */
    int end_slot;
    PerfWPage wpage[PERF_WPAGES];
    uint64_t wpages_lost;
    PerfCopyTarget tg[PERF_COPY_TARGETS];
    int site, slot;   /* the copy being recorded and the slot of the pass it ended: -1 none */
    int draw;         /* the slot of the draw being prepared's first target: -1 none */
    int last;         /* the slot found last, tried first */
    uint64_t lost;    /* events for a target with no slot left */
    uint64_t nodraw;  /* bindings over pending results outside a draw (dispatches) */
    uint64_t elsewhere;   /* hook calls off the GPU thread, not counted: atomic */
    uint64_t atlas_total; /* atlas pages made since the start, never reset by a window (research 05:05) */
} PerfCopies;
extern PerfCopies g_perf_cp;
void perf_copies_reset(PerfCopies* c); /* all of it, before any hook runs */
/* a copy at site begins: its pass end and bytes go to site until the next one */
void perf_copy_begin(PerfCopies* c, int site);
void perf_copy_bytes(PerfCopies* c, uint64_t n);
/* a pass on t (gva 0: no target) begun, or ended for why (a PE_*) */
void perf_copy_pass_begin(PerfCopies* c, const PerfTarget* t);
void perf_copy_pass_end(PerfCopies* c, const PerfTarget* t, int why);
/* a draw on t being prepared, or none (NULL) once it is done */
void perf_copy_draw(PerfCopies* c, const PerfTarget* t);
void perf_copy_src(PerfCopies* c, int kind, int outcome); /* also counted under g_perf_bind */
void perf_copy_class(PerfCopies* c, int cls);
void perf_copy_empty(PerfCopies* c, int site, uint64_t bytes);
void perf_copy_extra(PerfCopies* c, int which, uint64_t n);
void perf_copy_made(PerfCopies* c, int producer);
/* a vk_buf_copy_writable that returned a new result on path, its steps' times (PWS_N of them) and its bytes */
void perf_copy_writable(PerfCopies* c, int path, const uint64_t* ns, uint64_t bytes);
/* a transform feedback binding of size bytes, of class cls, captured bytes provably written (whole or short) */
void perf_copy_xfb(PerfCopies* c, int cls, uint64_t size, uint64_t captured);
/* a buf_get slow path: what it did (PO_*), why (POW_*), its bytes, inside a landed result or not, its times (POT_N) */
void perf_copy_own(PerfCopies* c, int what, int why, uint64_t bytes, int landed, const uint64_t* ns);
void perf_copy_own_drop(PerfCopies* c, uint64_t uses); /* an own buffer dropped after uses draws */
/* the window's line, as perf_times_line, and the next window started */
size_t perf_copies_line(PerfCopies* c, uint32_t frames, char* out, size_t size);
/* PERF-17's line from the same window: written before perf_copies_line, which starts the next one */
size_t perf_inplace_line(const PerfCopies* c, uint32_t frames, char* out, size_t size);
/* PERF-20's, the same */
size_t perf_writable_line(const PerfCopies* c, uint32_t frames, char* out, size_t size);
/* PERF-22's, the same */
size_t perf_own_line(const PerfCopies* c, uint32_t frames, char* out, size_t size);
/* PERF-23: a slow path's steps (POS_N) by outcome, an own_same after a GPU-side write by the stamps (PSQ_*, kinds of
   PSK_* bits), one after a CPU write (tracked, bytes, bytes on written pages), a written check finding nothing, and a
   host page found written under one in frame */
void perf_copy_steps(PerfCopies* c, int what, const uint64_t* ns, const uint8_t* ran);
void perf_copy_gsame(PerfCopies* c, int real, int q, unsigned kinds); /* real: g_perf_real's answer */
void perf_copy_probe(PerfCopies* c, uint64_t ns);
void perf_copy_cleared(PerfCopies* c, unsigned tests); /* PCL_* bits */
void perf_copy_csame(PerfCopies* c, int tracked, int big, uint64_t bytes, uint64_t written);
void perf_copy_quiet(PerfCopies* c, uint64_t ns);
void perf_copy_sample_skip(PerfCopies* c);
enum { PLW_LATER, PLW_FRONT_PAGE, PLW_FRONT_BYTE, PLW_CPU, PLW_N }; /* what wrote a landed range since (PERF-25) */
/* one lookup inside a landed result: made (own_made, else staged), kind (PLK_*), age and freed buckets (freed 0 for
   alive), clear (0 page tests, 1 the byte record, 2 not), wrote (PLW_* bits), its ns, the passes it ended and the
   target slot of the last (-1 none) */
void perf_copy_landed(PerfCopies* c, int made, int kind, int age, int freed, int clear, unsigned wrote, uint64_t ns,
                      uint64_t ends, int slot);
void perf_copy_landed_kept(PerfCopies* c, uint64_t bytes);
/* PERF-25's line: from the same window as perf-same, before perf-copies resets it */
size_t perf_landed_line(PerfCopies* c, uint32_t frames, char* out, size_t size);

/* PERF-27 (research 07:07, log only): the transform feedback captures' ranges, a frame at a time, and the landing of
   graphics results split by step: on the release thread, under the hold PT_RELEASE_HOLD times, and elsewhere. The
   ranges are the GPU thread's. The landing counts take atomic adds from any thread holding vk_lock */
#define PERF_CAP_MAX 4096u /* captures a frame kept: more are counted as lost */
typedef struct {
    uint64_t a;
    uint32_t n;
    uint8_t front; /* another GPU-side write or a publication on its pages since the frame before (PSK_GPU, _PUBLISH) */
} PerfCapRange;
enum { PCN_RANGES, PCN_LOST, PCN_DISTINCT, PCN_OVERLAP, PCN_ABUT, PCN_PAGE, PCN_APART, PCN_RECUR, PCN_FRONT, PCN_BYTES,
       PCN_SIZE, PCN_N = PCN_SIZE + 5 }; /* a window's capture counts, the sizes last: 1, 4, 16, 64 KB or less, more */
/* a result's landing by step: invalidate, compare and write the changed bytes, unmark its pages, find its changed runs
   for the rebase, walk the later results, the rest */
enum { PCL_INVALIDATE, PCL_WRITE, PCL_PAGES, PCL_RUNS, PCL_WALK, PCL_OTHER, PCL_STEPS };
/* the walk: results passed, composed ones asked, met and rebased. Then the landed result's changed runs, and 1 when its
   own write found them (SNX_ARCEUS_PERF_COMPARE=2) */
enum { PCW_VISITED, PCW_COMPOSED, PCW_MET, PCW_RUNS, PCW_FOUND, PCW_N };
enum { PCH_COMPUTE, PCH_GRAPHICS, PCH_RELEASES, PCH_N }; /* the release thread's hold by part, end to end */
typedef struct {
    PerfCapRange cur[PERF_CAP_MAX], prev[PERF_CAP_MAX];
    uint32_t ncur, nprev;
    uint64_t frame;        /* the frame being collected */
    uint64_t count[PCN_N]; /* the GPU thread's, read at the line */
    uint64_t land_ns[2][PCL_STEPS], landed[2], walk[2][PCW_N]; /* atomic: [0] the release thread, [1] elsewhere */
    uint64_t hold_ns[PCH_N], holds;                             /* atomic */
} PerfCaps;
extern PerfCaps g_perf_caps;
extern _Thread_local int g_perf_release_thread; /* set on the release thread with SNX_ARCEUS_PERF_TIMES */
/* a capture [a, a + n) made in frame, front its pages' other writes since the frame before. A new frame first counts
   the one collected (perf_caps_frame_end) */
void perf_caps_add(PerfCaps* c, uint64_t frame, uint64_t a, uint32_t n, int front);
void perf_caps_frame_end(PerfCaps* c);
/* one result landed, its steps' ns (PCL_*) and its walk's counts (PCW_*), on the release thread or not */
void perf_caps_landed(PerfCaps* c, int release, const uint64_t ns[PCL_STEPS], const uint64_t walk[PCW_N]);
/* one hold of the release thread: its parts' ns (PCH_*) */
void perf_caps_hold(PerfCaps* c, uint64_t compute, uint64_t graphics, uint64_t releases);
/* PERF-27's line, after perf-landed: the window's counts a frame, which it starts again */
size_t perf_caps_line(PerfCaps* c, uint32_t frames, char* out, size_t size);
/* PERF-29 (research 01:36), log only, with SNX_ARCEUS_PERF_PROBES=1: vk_hash's and vk_hash_copy's callers by return
   address (the first PERF_HASH_SITES, the rest together), the replay's draws timed by step on one in
   PERF_REPLAY_SAMPLE, and the release thread's releases by kind */
#define PERF_HASH_SITES 24u
#define PERF_REPLAY_SAMPLE 16u
/* the replay's draws, then of the sampled ones: the whole call (its binds and its draw words), their sends, a
   decode-only pass over the same words before each send, and their words and methods */
enum { PRP_DRAWS, PRP_SAMPLED, PRP_ALL_NS, PRP_SEND_NS, PRP_DECODE_NS, PRP_WORDS, PRP_METHODS,
       /* every draw's 3D register methods (0x40 to below the macro calls), its binds' and its draw words' apart, and
          of them the ones that write the value the register already holds (PERF-30, research 02:39) */
       PRP_BIND_REGS, PRP_BIND_SAME, PRP_DRAW_REGS, PRP_DRAW_SAME,
       PRP_PLAIN_NS, /* the draws not timed, each whole (PERF-30): what the replay's binds and draw words cost */
       PRP_N };
enum { PRL_SYNC, PRL_PAYLOAD, PRL_SAMPLES, PRL_KINDS }; /* a syncpoint increment, a payload, a samples report */
typedef struct {
    uint64_t base;                                     /* vk_hash's address, which the callers are printed against */
    uint64_t ra[PERF_HASH_SITES];                      /* atomic: 0 for a free slot, else the caller's address */
    uint64_t calls[PERF_HASH_SITES + 1], bytes[PERF_HASH_SITES + 1], long_ns[PERF_HASH_SITES + 1]; /* atomic */
    uint64_t replay[PRP_N];                            /* atomic */
    uint64_t rel_n[PRL_KINDS], rel_ns[PRL_KINDS], rel_book_ns; /* the release thread's own */
    uint64_t idx_fell[2], idx_max[2]; /* atomic: buffer.c's index lookups, gfx_gpu_src's [0] and the landing's [1] */
} PerfP29;
extern PerfP29 g_perf_p29;
/* one vk_hash of n bytes from ra (base: vk_hash), ns for one of 1 KB or more (else 0) */
void perf_hash_note(PerfP29* p, uint64_t ra, uint64_t base, uint64_t n, uint64_t ns);
void perf_replay_add(PerfP29* p, int what, uint64_t v);
/* one release performed: its kind (PRL_*), its own ns, and its queue's bookkeeping under g_rlock without it */
void perf_release_add(PerfP29* p, int kind, uint64_t ns, uint64_t book_ns);
/* one index lookup (which: 0 gfx_gpu_src's, 1 the landing's) that met n candidates, or gave up past its bound */
void perf_index_note(PerfP29* p, int which, int fell_back, uint64_t n);
size_t perf_p29_line(PerfP29* p, uint32_t frames, char* out, size_t size);
/* PERF-31 (research 03:57), log only, with SNX_ARCEUS_PERF_PROBES=1: where the kernel time is. The process's page
   faults, each named thread's kernel and user time (each thread stores its own, perf_sample.h), and the allocations
   of 4 KB or more the GPU thread makes by site, with the results' frees by the thread that frees them */
enum { PTH_GPU, PTH_RELEASE, PTH_RECORDER, PTH_GUEST, PTH_N }; /* PTH_GUEST: the guest thread that posts presents */
/* a result's block (all sizes, apart below 4 KB), range_map's page map, a walk's or compose's list, an own buffer */
enum { PMA_RESULT, PMA_RESULT_SMALL, PMA_RANGE_MAP, PMA_LIST, PMA_OWN, PMA_N };
typedef struct {
    uint64_t kernel[PTH_N], user[PTH_N];   /* atomic: the thread's CPU time so far, ns, as it last stored it */
    uintptr_t owner[PTH_N];                /* atomic: the thread a slot is for, the first to store (its token) */
    uint64_t others;                       /* atomic: stores from another thread, refused */
    uint64_t kernel0[PTH_N], user0[PTH_N]; /* the line's: those at the line before */
    uint64_t faults0;                      /* the line's: the page faults at the line before */
    uint64_t alloc_n[PMA_N], alloc_b[PMA_N]; /* atomic */
    uint64_t free_n[2], free_b[2];           /* atomic: results freed on the release thread [0], elsewhere [1] */
} PerfMem;
extern PerfMem g_perf_mem;
/* token: one per thread (an address of its own); a slot keeps the first thread's and refuses the others' */
int perf_mem_thread(PerfMem* m, int who, uintptr_t token, uint64_t kernel_ns, uint64_t user_ns); /* 0: refused */
void perf_mem_alloc(PerfMem* m, int site, uint64_t bytes);
void perf_mem_free(PerfMem* m, int release, uint64_t bytes);
/* the line, faults the process's page faults so far: the window's are those since the line before */
size_t perf_mem_line(PerfMem* m, uint32_t frames, uint64_t faults, char* out, size_t size);
/* PERF-32 (research 04:22), log only, with SNX_ARCEUS_PERF_PROBES=1: the waits, wakes, yields and sleeps of every
   thread by caller (host.h's wait hook), and brackets on the graphics timeline's waits, the write watch's scans and
   the recorder's submits and command buffer ends. By thread: PTH_* and the write scanner, the rest as other */
enum { PWHO_SCANNER = PTH_N, PWHO_OTHER, PWHO_N };
enum { PW_VKWAIT = 5, PW_SCAN, PW_SUBMIT, PW_ENDCB, PW_N0 }; /* after host.h's PMW_* (lock, cond, wake, yield, sleep) */
/* a payload release's split (release.c perform): its write's before-write hooks, copy and written notice, and a
   samples report's sum */
/* PERF-34's probes: a result's block made, and a freed list's walk for its CALLs */
enum { PW_HOOKS = PW_N0, PW_COPY, PW_WRITTEN, PW_QUERY, PW_ALLOC, PW_UNREF, PW_N };
/* SNX_ARCEUS_PERF_SMALL (PERF-34, research 11:52): small exact steps, a bit each, read once at start. 1: release()
   wakes the release thread only when it moved g_rheld. 2: a result's block is not zeroed where every byte is written
   before it is read. 4: a freed list's CALLs are found at its marks. 8: one written notice for a release hold's
   payload writes, each distinct page stamped once */
extern int g_perf_small;
enum { PSM_WAKE = 1, PSM_NOZERO = 2, PSM_UNREF = 4, PSM_NOTICE = 8 };
#define PERF_WAIT_SLOTS 512u
typedef struct {
    uint64_t key[PERF_WAIT_SLOTS]; /* atomic: 0 free, else 1 << 56 | kind << 52 | who << 48 | the caller's address */
    uint64_t n[PERF_WAIT_SLOTS], ns[PERF_WAIT_SLOTS]; /* atomic */
    uint64_t lost;                                    /* atomic: notes the full table refused */
    uint64_t base;                                    /* the callers are printed against it (pm_wait_note's address) */
} PerfWaits;
extern PerfWaits g_perf_waits;
void perf_who_set(int who); /* the calling thread's label, PWHO_OTHER until set */
void perf_waits_note(PerfWaits* w, int who, int kind, const void* at, uint64_t ns);
void perf_wait_note(int kind, const void* at, uint64_t ns); /* the hook's: g_perf_waits, this thread's label */
size_t perf_waits_line(PerfWaits* w, uint32_t frames, char* out, size_t size);
void perf_copy_wpage(PerfCopies* c, uint64_t page, uint64_t frame);
/* PERF-23's, the same, with the stamps' counts since the last and the cost of one clock read. real NULL: the record as
   written is not kept (SNX_ARCEUS_PERF_TIMES=1), its columns not printed */
size_t perf_same_line(const PerfCopies* c, PerfStamps* s, PerfStamps* real, uint32_t frames, double clock_ns, char* out,
                      size_t size);

/* SNX_ARCEUS_PERF_INPLACE=1, 2 or 3, bench only (PERF-17, research 02:42), each level with the ones below: 1 a read
   binds in place the newest pending result sharing a byte with its range when it covers it, 2 a vertex stream with
   pending results on its pages is looked up at its exact length, 3 a read skips the reuse barrier when no writable
   result was handed out since the last one in its submission, 4 a writable composition skips gfx_overlay when no
   pending result shares a byte with its range (PERF-21). 0 unless set. Read once at start */
extern int g_perf_inplace;
/* SNX_ARCEUS_PERF_QUERY=1, bench only (PERF-19, research 05:05): a samples-passed report neither ends the pass nor
   copies the counts, which the submission it waits for copies at its end (vk/query.c vk_query_room). 0 unless set */
extern int g_perf_query;
/* SNX_ARCEUS_PERF_SAMPLE=1 (PERF-24): with write tracking a buffer's full check takes no sample (gpu/vk/buffer.c) */
extern int g_perf_sample;
/* SNX_ARCEUS_PERF_COMPARE=2 (PERF-28, gpu/vk/buffer.c): a landing's masked pass finds its changed runs, and its walk
   and gfx_gpu_src's go through a page index */
extern int g_perf_compare;
/* SNX_ARCEUS_PATCH_HASH=1: a patched own buffer takes the next full check as changed (gpu/vk/buffer.c) */
extern int g_patch_hash;

#endif
