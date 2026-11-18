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
} PerfSub;
/* A tail's times, each written once by the thread that reaches that point, and closed when both its vk_lock release
   and the game's wake are in */
#define PERF_TAILS 4u
typedef struct {
    uint64_t armed, gate, seq, t0, t_submit, t_done, t_incr, t_free, t_wake;
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
/* the graphics results landed lately, by guest page, for buf_get's slow path: under vk_lock, both sides */
typedef struct {
    uint64_t page, start, end, frame;
} PerfLanded;
#define PERF_LANDED 8192u
#define PERF_LANDED_PAGES 16u /* a result's first pages are entered: a range further into a bigger one is missed */
extern PerfLanded g_perf_landed[PERF_LANDED];
void perf_landed_add(PerfLanded* t, uint64_t start, uint64_t size, uint64_t frame);
/* whether [gva, gva + len) lies inside one result landed in frame or the one before */
int perf_landed_inside(const PerfLanded* t, uint64_t gva, uint64_t len, uint64_t frame);
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
    uint64_t own[PO_N], own_landed;                   /* its draws' buf_get slow paths (PERF-22) */
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

/* SNX_ARCEUS_PERF_INPLACE=1, 2 or 3, bench only (PERF-17, research 02:42), each level with the ones below: 1 a read
   binds in place the newest pending result sharing a byte with its range when it covers it, 2 a vertex stream with
   pending results on its pages is looked up at its exact length, 3 a read skips the reuse barrier when no writable
   result was handed out since the last one in its submission, 4 a writable composition skips gfx_overlay when no
   pending result shares a byte with its range (PERF-21). 0 unless set. Read once at start */
extern int g_perf_inplace;
/* SNX_ARCEUS_PERF_QUERY=1, bench only (PERF-19, research 05:05): a samples-passed report neither ends the pass nor
   copies the counts, which the submission it waits for copies at its end (vk/query.c vk_query_room). 0 unless set */
extern int g_perf_query;

#endif
