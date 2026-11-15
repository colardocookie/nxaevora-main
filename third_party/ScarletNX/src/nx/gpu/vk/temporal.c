/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Temporal passes for Pokemon Legends: Arceus (SNX_ARCEUS_TEMPORAL). "probe" finds the scene colour and depth of a
   frame, keeps the camera's matrices and makes camera motion vectors between the scene colour's last write and first
   read. "taa" also gives the scene's camera a sub-pixel jitter and resolves the scene there with a history of its
   own. SNX_ARCEUS_TEMPORAL_VIEW draws what they see (docs/arceus-temporal.md). Unset, nothing here runs. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nx/gpu/gpu.h"
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/vk/post_spv.h"
#include "nx/gpu/vk/temporal_spv.h"
#include "nx/gpu/vk/temporal_math.h"
#include "nx/gpu/vk/temporal_spatial.h"
#include "nx/gpu/vk/upscaler.h"
#include "nx/gpu/vk/upscaler_needs.h"
#include "nx/runtime/platform/log.h"

/* the host's monotonic clock in nanoseconds (runtime/platform/plat.h, whose Windows headers define near and far,
   which are names here) */
u64 plat_ns(void);

#define CAMERA_BANK 4       /* the constant bank of the camera block */
#define CAMERA_BYTES 0x140  /* view-projection at +0x00, its inverse at +0xC0, row vectors */
#define CAMERA_INVERSE 0xc0
/* red and green the vector, alpha 1 where it is an object's and blue then the depth its point had a frame ago */
#define MOTION_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT
#define SKINS 512          /* slots of the table of skinned characters' position buffers */
#define SKIN_BYTES (16u << 20) /* the largest position buffer kept */
#define HISTORY_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT
#define JITTERS 8          /* the offsets of the jitter, a Halton (2, 3) sequence */
#define FRAME_WEIGHT 0.1f  /* the frame's share in the blend with the history */
#define OBJECT_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT
#define OBJECT_BANK 6      /* the object's constant block: its world matrix at +0, row vectors */
#define OBJECTS 8192       /* slots of the table of world matrices */
#define OBJECT_DRAWS 256   /* extra draws a frame at most */
#define OBJECT_SIGNATURES 8 /* draws of one block in a frame that are kept one by one */
#define OBJECT_TOUCHED 4096 /* blocks bound in a frame that are judged at its end */
/* why a frame's record of blocks is not complete (the links): record full, table full, a draw before the camera */
enum { LINK_RECORD, LINK_TABLE, LINK_EARLY };
#define MODE_PROBE 1
#define MODE_TAA 2
#define SETS_BINDINGS 7
#define STATS_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT
#define STATS_W 96u /* the numbers' image: one texel a sampled pixel, in each of two halves side by side */
#define STATS_H 54u
/* Runs between two readings. Not a multiple of the jitter's period, so the readings walk through all its offsets:
   every 60 runs they saw two of the eight. */
#define STATS_EVERY 61u
/* Runs between two copies into the numbers' buffer at least. The host reads a copy at the next one, without waiting
   for it, so the copy must be done by then: the spacing stays well above the frames in flight. */
#define STATS_SPACING 8u
_Static_assert(STATS_SPACING >= 4 * VK_FRAMES && 2 * STATS_SPACING < STATS_EVERY, "numbers runs too close");
#define STATS_ERROR 0.25f  /* a relative colour error above this counts as wrong */

typedef struct {
    VkImage img;
    VmaAllocation mem;
    VkImageView view;
    VkFormat format;
    u32 w, h;
} Image;
/* the passes' push constants (shaders/temporal_motion.frag) */
typedef struct {
    float reproject[16];
    float viewport[4];
    float depth[4];
    float size[4];
    float jitter[4]; /* this frame's jitter in clip units over w (x, y), which the camera's vectors take off */
} Constants;

static struct {
    int mode; /* -1 not read yet, 0 off, MODE_PROBE, MODE_TAA */
    int view; /* 0 none, 1 the vectors, 2 the reprojection error, 3 the history weight, 4 the jittered frame alone */
    /* what a frame taught: the scene colour and depth targets' guest addresses, and the fragment program of the first
       draw that samples the scene colour */
    u64 scene, depth, reader;
    /* this frame, by g_vk_present + 1 (0 is never) */
    u32 camera_at, written_at, ran_at, reader_at, history_at;
    Surf *scene_rt, *depth_rt;
    VkViewport viewport;
    float depth_translate, depth_scale; /* stored depth = translate + scale x the projection's z */
    int depth_zero_to_one;
    float now[16], inverse[16], before[16], kept[16];
    int have_before;
    u64 pair_equal, pair_differ, runs, skipped;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout layout;
    VkSampler linear, point;
    VkPipeline motion_pipe, view_pipe, stats_pipe;
    Image stats;
    VkBuffer stats_buffer;
    VmaAllocation stats_memory;
    const u16* stats_words;
    u64 stats_run; /* the run whose numbers the buffer holds, 0 for none */
    VkFormat view_format;
    Image motion, colour[2];
    int newest; /* which of colour[] holds the newer frame */
    /* anti-aliasing: the two addresses the scene camera's block takes in turn, its jittered copy for this
       submission, and our history */
    u64 camera_address[2], jitter_address, jitter_submit, jittered;
    /* a miss of the paired slot: what the two neighbours held, to compare with the next frame's block */
    u8 miss_bytes[2][64];
    u32 miss_at;
    u64 miss_ahead, miss_other;
    u8 jitter_source[0x1000]; /* the guest's bytes the copy was made from */
    /* object vectors: the target, the frame it was last drawn in, and the counts of the stats line */
    int objects;
    /* draws without a matrix of a frame ago, [0] rigid and [1] reading skinned positions: all of them, those whose
       matrix differs from what the address last held, those never bound before. Then the skinned reads' fates. */
    u64 uncovered[2][3], uncovered_blind[2], skin_plain, skin_left, skin_rigid;
    /* SNX_ARCEUS_TEMPORAL_LINKS: a block's other slot found by its draws. link_frame: the frame being recorded.
       Then the draws a link gave a matrix and the ones it did not by reason (TEMPORAL_LINK_*), the links that
       reached their fourth frame, the frames not complete by cause and the frames given up for a link. */
    int links;
    u32 link_frame, link_unknown_at[3], link_logged_at;
    /* link_left: draws left without a matrix, [rigid or skinned][not asked, then TEMPORAL_LINK_*][all, moving] */
    u64 link_left[2][7][2];
    u64 link_why[6], links_made, link_unknown_frames[3], link_early_draws, link_broke_frames;
    int reject; /* SNX_ARCEUS_TEMPORAL_REJECT: no history where an object's vector is far from the camera's */
    float sharpen;  /* 0 off, to 1: the sharpen after the resolve */
    float mip_bias; /* 0 off, negative: added to the level bias of material textures on scene draws */
    VkPipeline sharpen_pipe;
    VkFormat sharpen_format;
    /* the resolve's: 1 Catmull-Rom history, 2 variance clip, 4 weight by motion and clip, 8 the nearest depth's
       vector, 16 blend by brightness */
    int options;
    Image object;
    VkShaderModule object_fs;
    u32 object_at, object_frame, object_draws;
    u64 object_seen, object_moved, object_drawn, object_capped, object_new, object_odd, object_pipelines,
        object_frames, object_far, object_same, object_never, object_older, object_skinned, skinned_lines;
    /* skinned characters: our vertex and fragment programs, and the counts of the stats line */
    VkShaderModule skin_vs, skin_fs;
    u64 skin_kept, skin_drawn, skin_first, skin_twice, skin_changed, skin_short, skin_unowned, skin_shared,
        skin_uncopied, skin_unkept;
    /* the learned pairs: why draws got no matrix of a frame ago, what the end of each frame judged, and the frames
       whose object vectors were given up because a pair broke after its draws */
    u64 object_learning, object_other, object_rest, object_full, object_unjudged;
    u64 pairs_learned, stays_learned, pairs_broken, rest_broken, broke_frames;
    void* touched[OBJECT_TOUCHED]; /* the table entries bound in touched_frame, for the judging at its end */
    u32 touched_n, touched_frame, broke_logged_at;
    /* the draw render.c is at (vk_temporal_object_note): its block and what the table gave it */
    struct {
        u64 address;
        void *slot, *other;
        const float* was;
        int source;
        u64 pair, signature; /* the pair's lower slot (the slot itself when it takes no turns), the draw */
        u64 pair_other;      /* a link's higher slot, 0 for a pair: the two tag the skinned copies */
        int chosen;          /* what the pairs said of the draw, before the guards */
        u64 link;            /* the linked slot whose matrix it took, 0 for none */
        int link_why;        /* what the links said of the draw (TEMPORAL_LINK_*), -1 when they were not asked */
        int unskinned;       /* it reads skinned positions and was not drawn again with last frame's */
        float world[16];
    } current;
    /* each extra draw of this frame by its number, and the same for the frame the numbers' image was drawn in:
       the probe writes the number to the vector target, so a wrong vector names its draw */
    struct Drawn {
        u64 block, feedback, signature, link;
        float shift; /* the origin's move on screen in pixels */
        u8 skinned, source, far; /* far: a long move with the camera nearly still */
        void *slot, *other; /* the table entries its matrix of a frame ago came from */
        const TemporalStreamCopy* stream; /* a skinned draw: this frame's copy at its capture address */
    } drawn[OBJECT_DRAWS + 1], stats_drawn[OBJECT_DRAWS + 1];
    /* stats_draws and stats_height: the extra draws and the scene's height of the frame the numbers' image was
       drawn in */
    u32 long_logged_at, stats_draws, stats_height;
    /* the jitter of the frame: decided at its first draw with the scene camera's block (jitter_decide) from that
       draw's depth target and registers, which vk_temporal_draw notes for every draw */
    const Surf* draw_zt;
    const u32* draw_r3d;
    /* SNX_ARCEUS_TEMPORAL_RECT: the scene may be a rectangle of its targets, and the frame goes through the
       rectangle's gate (temporal_math.h) in place of the whole-target one. The draw's channel and programs are
       kept for it, as are this frame's count of offsets and the making of the images a history is tied to. */
    int rect;
    const GpuChannel* draw_ch;
    const VkSh *draw_vs, *draw_fs;
    TemporalRectGate rect_gate;
    u32 offsets, rect_early_said, rect_fraction_said;
    u32 camera_new_at; /* the frame whose G-buffer draw showed a camera block address not known before */
    /* The draw noted: whether it goes into the scene (its depth target is the scene's depth, or a colour target is
       the scene colour) and its target's size. rect_kept: a scene draw of an undecided frame (rect_note). */
    int draw_scene, rect_stop_said;
    u32 draw_w, draw_h;
    struct {
        int have, shaped;
        u32 frame;
        TemporalRectState geometry;
        TemporalSceneBlock vertex, fragment;
    } rect_kept;
    /* the frame's copy of the scene as drawn, for the numbers and the views: what it was made from, and when */
    TemporalRectStamp rect_raw;
    u32 rect_raw_at;
    int mip_bias_set; /* SNX_ARCEUS_TEMPORAL_MIPBIAS holds a number, 0 included: no automatic bias */
    int rect_extras_off; /* the numbers and the views could not be made once: none for the rest of the session */
    u32 object_failed_at; /* the frame an object pass could not get what it needs (vk_temporal_object_failed) */
    /* SNX_ARCEUS_TEMPORAL_GPUTIME: -1 not looked at yet, 0 off or given up, 1 measuring. The pool has two queries
       a pair of the ring. time_bits and time_period are the graphics queue's clock. */
    int gputime;
    VkQueryPool time_pool;
    u32 time_bits;
    /* The window the kept times belong to: what our passes were configured as when it began (whole numbers
       only, so two can be held against each other), and the present it began at. A line is written, and a new
       window begun, every 600 presents and whenever the configuration is another. */
    TemporalGpuWindow time_window;
    double time_period;
    /* the brackets of the current run of our passes that are begun and not yet ended: -1 for none */
    TemporalGpuHandle time_hook, time_numbers;
    u64 rect_generation, rect_gated, rect_seen, rect_steady, rect_spoilt, rect_left;
    VkImage rect_images[5];
    TemporalGate gate;
    float jitter_ndc[2], jitter_made[2];
    u64 gated, mismatched, early, unsteady;
    /* the transform feedback outputs of this frame, for finding the draws that read them */
    struct {
        u64 address;
        u32 size, index;
    } feedback[256];
    u32 feedback_n, feedback_frame, far_logged_at;
    u64 other_cameras;
    u32 jitter_index, jitter_size, camera_read; /* camera_read: the most bytes a program read of the block */
    VkBuffer jitter_buffer;
    u64 jitter_offset;
    VkPipeline resolve_pipe;
    VkFormat resolve_format;
    Image history[2];
    int history_newest;
    u32 resolved_at;
    u64 resets;
} g_t = {.mode = -1, .gputime = -1, .time_hook = -1, .time_numbers = -1};
static TemporalGpuRing g_time_ring; /* the brackets under way and the times kept since the last line */
/* SNX_ARCEUS_UPSCALER: the state of a library's upscaler (the functions are further down, before the hook) */
/* The private output: four 32 bit floats, as the library's shader declares the image it writes. Its uses are a
   storage image for the library and a sampled one for our write-back, and no copy of ours. */
#define UPSCALED_FORMAT VK_FORMAT_R32G32B32A32_SFLOAT
#define UPSCALED_USAGE (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)
/* Once a library was entered, whatever it answers, every image it was handed counts as last written by any
   command: what it recorded is not known to the runtime, and a failed call is no proof of none. A barrier after
   it has this source, and its destination is every command, reading and writing, until the next use is known. */
#define UPSCALER_WROTE VK_ACCESS_2_MEMORY_WRITE_BIT
#define UPSCALER_ANY_ACCESS (VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT)
static struct {
    int asked; /* -1 not looked at, 0 off or not available, 1 started (and it stays 1 when it stops) */
    const SnxUpscalerApi* api;
    double nominal; /* the target rate's frame time in seconds, 0 without one */
    int was_rect, was_view, was_extras; /* what the start changed, to put back if no context can be made */
    TemporalUpscalerHealth health;
    TemporalUpscalerContexts contexts;
    SnxUpscaler *live, *pending;
    u64 pending_number;    /* the submission the pending context's prepare went into */
    int noted;             /* what a recycle noted of it (temporal_upscaler_poll) */
    unsigned unread;       /* recycles in a row in which the timeline could not be read */
    unsigned published, evaluated_with;
    TemporalUpscalerMaking making;
    unsigned long long generation;
    TemporalUpscalerConfig wanted, decided; /* what the frames want now, and what this frame was decided with */
    TemporalUpscalerColour colour;          /* the scene colour of the last frame that reached its hook */
    u64 block;                              /* the camera block the frame was decided at */
    float decided_projection[16];           /* its unjittered projection as read at the decision: a prediction */
    float decided_view[16];                 /* and its view-projection, which tells two cameras of one lens apart */
    TemporalUpscalerUsed used, used_view;   /* what the frame's jittered draws were given of both, by value */
    u32 decided_at, outcome_at, evaluated_at;
    int decision, known; /* the frame's decision, and whether its configuration was known at it */
    u64 evaluated_ns, unresolved, contexts_made, strangers; /* strangers: lines of draws with another bank 4 */
    TemporalUpscalerTally tally;
    Image output;
    /* DL2 point 3: an adapter that takes the vectors in two channels (DLSS) is handed a private copy of red and
       green of the motion image, made each evaluated frame (upscaler_motion) */
    int two_channel;
    Image motion2;
    VkPipeline motion2_pipe;
    TemporalUpscalerHanded handed; /* for the log: the four images as last said */
    /* D3-3: FSR below native on D3-2's hand-over, with the fixed resolution; the scene scale is the opt-in */
    int below;
    u32 output_max[2]; /* the output's largest width and height, as the start's format question answered */
    /* the output's uses: the runtime's (sampled for the write-back, storage) and the adapter's traits' (research
       19:00: NGX clears it, a transfer destination), and those the image was last made with */
    VkImageUsageFlags output_usage, output_made_usage;
    VkPipeline write_pipe;
    VkFormat write_format;
} g_u = {.asked = -1};
/* SNX_ARCEUS_UPSCALER=spatial's state (the functions are further down, before the hook) */
static struct {
    int asked;
    TemporalSpatialLatch latch;
    TemporalSpatialFrame fr;
    u32 frame; /* the frame fr is of */
    int said_off, restarted;
    Image picture, larger;
    VkPipeline scale_pipe;
    VkFormat scale_format;
    /* published: the scene colour's Surf, its image, write sequence and memory at the hook, the fractions */
    const Surf* scene;
    VkImage image;
    u64 wseq, gva, bytes;
    double f[2];
    int W, H, M, N;
    int binding_on, binding_listed, binding_given; /* the binding being made (vk_temporal_spatial_binding) */
    int stats_private; /* the numbers last written are of the private picture */
    /* the larger texture's views by component mapping, the game view's own (research 19:40), of the image views_of */
    struct {
        VkComponentMapping map;
        VkImageView view;
    } views[4];
    int nviews;
    VkImage views_of;
    /* for the line every 600th frame: frames replaced and left as drawn, the first reason, the first consumer */
    u64 replaced, as_drawn, consumers, faults;
    char reason[200], consumer[200];
} g_s;
const Surf* g_vk_watch;
static int spatial_now(u32 frame);
static void spatial_views_drop(void);
static int spatial_larger(const Surf* scene, int M, int N, int picture);
static void spatial_larger_begin(VkCommandBuffer cb);
static void spatial_fill_publish(VkCommandBuffer cb, const Surf* scene);
static int spatial_below_ready(u32 frame, const Surf* scene, const TemporalRectGate* rg);
static void upscaler_start(void);
#define MOTION2_FORMAT VK_FORMAT_R16G16_SFLOAT
static VkFormat upscaler_motion_format(void) { return g_u.two_channel ? MOTION2_FORMAT : MOTION_FORMAT; }
static Image* upscaler_motion(void) { return g_u.two_channel ? &g_u.motion2 : &g_t.motion; }
static int upscaler_on(void);
static int upscaler_jittered(u32 frame);
static int upscaler_decide(u32 frame, int accepted);
static void upscaler_outcome(u32 frame, int jittered, int outcome, const char* what);

static int temporal_on(void) {
    if (g_t.mode < 0) {
        const char* mode = getenv("SNX_ARCEUS_TEMPORAL");
        const char* view = getenv("SNX_ARCEUS_TEMPORAL_VIEW");
        g_t.mode = !mode ? 0 : !strcmp(mode, "probe") ? MODE_PROBE : !strcmp(mode, "taa") ? MODE_TAA : 0;
        g_t.view = view && view[0] >= '1' && view[0] <= '4' && !view[1] ? view[0] - '0' : 0;
        /* the probe has views 1 and 2, the anti-aliasing 3 and 4 */
        if ((g_t.mode == MODE_PROBE && g_t.view > 2) || (g_t.mode == MODE_TAA && g_t.view < 3)) g_t.view = 0;
        const char* history = getenv("SNX_ARCEUS_TEMPORAL_HISTORY");
        const char* clamp = getenv("SNX_ARCEUS_TEMPORAL_CLAMP");
        const char* blend = getenv("SNX_ARCEUS_TEMPORAL_BLEND");
        const char* nearest = getenv("SNX_ARCEUS_TEMPORAL_NEAREST");
        const char* luma = getenv("SNX_ARCEUS_TEMPORAL_LUMA");
        g_t.options = (history && !strcmp(history, "catmull") ? 1 : 0) |
                      (clamp && !strcmp(clamp, "variance") ? 2 : 0) | (blend && !strcmp(blend, "adaptive") ? 4 : 0) |
                      (nearest && !strcmp(nearest, "1") ? 8 : 0) | (luma && !strcmp(luma, "1") ? 16 : 0);
        const char* sharpen = getenv("SNX_ARCEUS_TEMPORAL_SHARPEN");
        const char* mip_bias = getenv("SNX_ARCEUS_TEMPORAL_MIPBIAS");
        g_t.sharpen = g_t.mode == MODE_TAA && sharpen ? strtof(sharpen, NULL) : 0.0f;
        if (!(g_t.sharpen > 0.0f)) g_t.sharpen = 0.0f;
        if (g_t.sharpen > 1.0f) g_t.sharpen = 1.0f;
        char* mip_end = NULL;
        g_t.mip_bias = g_t.mode == MODE_TAA && mip_bias ? strtof(mip_bias, &mip_end) : 0.0f;
        /* set: the line holds one number and nothing else. Without one the rectangle's gate gives its own bias. */
        g_t.mip_bias_set = g_t.mode == MODE_TAA && mip_bias && mip_end != mip_bias && !*mip_end &&
                           isfinite(g_t.mip_bias);
        if (!(g_t.mip_bias < 0.0f)) g_t.mip_bias = 0.0f;
        if (g_t.mip_bias < -2.0f) g_t.mip_bias = -2.0f;
        const char* vectors = getenv("SNX_ARCEUS_TEMPORAL_VECTORS");
        g_t.objects = g_t.mode && vectors && !strcmp(vectors, "object");
        const char* links = getenv("SNX_ARCEUS_TEMPORAL_LINKS");
        g_t.links = g_t.objects && links && !strcmp(links, "1");
        const char* reject = getenv("SNX_ARCEUS_TEMPORAL_REJECT");
        g_t.reject = g_t.objects && reject && !strcmp(reject, "1");
        const char* rect = getenv("SNX_ARCEUS_TEMPORAL_RECT");
        g_t.rect = g_t.mode && rect && !strcmp(rect, "1");
        g_t.offsets = JITTERS;
        /* Every option's state, on or off, in each run's log. A value other than the one named leaves an option
           off: NEAREST and LUMA take 1, HISTORY catmull, CLAMP variance, BLEND adaptive, VECTORS object, SHARPEN
           a number above 0, MIPBIAS one below 0. */
        if (g_t.mode)
            LOG("arceus-temporal: vectors %s\n", g_t.objects ? "of the camera and of objects that moved (object)"
                                                             : "of the camera only");
        if (g_t.mode)
            LOG("arceus-temporal: a block's other slot where it is not the neighbouring one: %s\n",
                g_t.links ? "looked for by its draws (links)" : "not looked for");
        if (g_t.mode)
            LOG("arceus-temporal: an object's vector further than an eighth of the scene's height from the "
                "camera's: %s\n", g_t.reject ? "rejected, the pixel takes no history (reject)" : "kept");
        if (g_t.mode)
            LOG("arceus-temporal: a scene inside a rectangle of its targets: %s\n",
                g_t.rect ? "taken when the scene block confirms it (rect)" : "refused, the whole target only");
        char bias[48] = "none";
        if (g_t.mip_bias_set) snprintf(bias, sizeof bias, "set to %.2f", (double)g_t.mip_bias);
        else if (g_t.rect && g_t.mode == MODE_TAA) snprintf(bias, sizeof bias, "automatic (log2 of the scene's scale)");
        if (g_t.mode == MODE_TAA)
            LOG("arceus-temporal: resolve options: history %s, clamp %s, weight %s, vector %s, blend %s, sharpen "
                "%.2f, texture level bias %s\n",
                g_t.options & 1 ? "Catmull-Rom (catmull)" : "linear", g_t.options & 2 ? "variance clip" : "min and max",
                g_t.options & 4 ? "by motion and clip (adaptive)" : "fixed",
                g_t.options & 8 ? "of the nearest depth" : "own", g_t.options & 16 ? "by brightness (luma)" : "plain",
                (double)g_t.sharpen, bias);
        if (g_t.mode)
            LOG("arceus-temporal: %s on, view %d (0 none, 1 motion vectors, 2 reprojection error, 3 history weight, "
                "4 jittered frame alone)\n",
                g_t.mode == MODE_TAA ? "anti-aliasing" : "probe", g_t.view);
        upscaler_start();
    }
    return g_t.mode;
}

static int image_ensure(Image* t, VkFormat format, u32 w, u32 h, VkImageUsageFlags usage, const char* what) {
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                            .imageType = VK_IMAGE_TYPE_2D,
                            .format = format,
                            .extent = {w, h, 1},
                            .mipLevels = 1,
                            .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = usage,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageViewCreateInfo vci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                 .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                 .format = format,
                                 .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (t->img && t->format == format && t->w == w && t->h == h) return 1;
    if (t->img) vk_defer_image(t->img, t->mem, t->view, VK_NULL_HANDLE);
    *t = (Image){0};
    if (!vk_image_create(&ii, &t->img, &t->mem)) {
        *t = (Image){0};
        LOG_LIMIT(1, "arceus-temporal: no memory for the %ux%u %s image\n", w, h, what);
        return 0;
    }
    vci.image = t->img;
    if (!VK_OK(vkCreateImageView(g_vk.dev, &vci, NULL, &t->view))) {
        vk_defer_image(t->img, t->mem, VK_NULL_HANDLE, VK_NULL_HANDLE);
        *t = (Image){0};
        return 0;
    }
    t->format = format;
    t->w = w;
    t->h = h;
    return 1;
}

static int layouts_make(void) {
    VkDescriptorSetLayoutBinding b[SETS_BINDINGS];
    VkDescriptorSetLayoutCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = SETS_BINDINGS, .pBindings = b};
    VkPushConstantRange pc = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Constants)};
    VkPipelineLayoutCreateInfo lci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                      .setLayoutCount = 1,
                                      .pSetLayouts = &g_t.dsl,
                                      .pushConstantRangeCount = 1,
                                      .pPushConstantRanges = &pc};
    VkSamplerCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                               .magFilter = VK_FILTER_LINEAR,
                               .minFilter = VK_FILTER_LINEAR,
                               .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                               .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                               .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                               .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    if (g_t.layout) return 1;
    for (u32 i = 0; i < SETS_BINDINGS; i++)
        b[i] = (VkDescriptorSetLayoutBinding){.binding = i,
                                              .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                              .descriptorCount = 1,
                                              .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    if (!VK_OK(vkCreateDescriptorSetLayout(g_vk.dev, &dci, NULL, &g_t.dsl))) return 0;
    if (!VK_OK(vkCreateSampler(g_vk.dev, &sci, NULL, &g_t.linear))) return 0;
    sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
    if (!VK_OK(vkCreateSampler(g_vk.dev, &sci, NULL, &g_t.point))) return 0;
    if (!VK_OK(vkCreatePipelineLayout(g_vk.dev, &lci, NULL, &g_t.layout))) {
        g_t.layout = VK_NULL_HANDLE;
        return 0;
    }
    return 1;
}

/* a pipeline of one fragment shader with the post passes' fullscreen triangle, for one colour format */
static VkPipeline pipeline_make_n(const VkFormat* formats, u32 count, const u32* fs_words, u64 fs_bytes,
                                  const char* what);
static VkPipeline pipeline_make(VkFormat format, const u32* fs_words, u64 fs_bytes, const char* what) {
    return pipeline_make_n(&format, 1, fs_words, fs_bytes, what);
}
static VkPipeline pipeline_make_n(const VkFormat* formats, u32 count, const u32* fs_words, u64 fs_bytes,
                                  const char* what) {
    VkPipelineShaderStageCreateInfo st[2] = {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                              .stage = VK_SHADER_STAGE_VERTEX_BIT,
                                              .pName = "main"},
                                             {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                              .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                              .pName = "main"}};
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                 .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                 .polygonMode = VK_POLYGON_MODE_FILL,
                                                 .cullMode = VK_CULL_MODE_NONE,
                                                 .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                               .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba[2] = {{.colorWriteMask = 0xF}, {.colorWriteMask = 0xF}};
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                              .attachmentCount = count,
                                              .pAttachments = ba};
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dyn};
    VkPipelineRenderingCreateInfo ri = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                        .colorAttachmentCount = count,
                                        .pColorAttachmentFormats = formats};
    VkGraphicsPipelineCreateInfo gci = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                        .pNext = &ri,
                                        .stageCount = 2,
                                        .pStages = st,
                                        .pVertexInputState = &vi,
                                        .pInputAssemblyState = &ia,
                                        .pViewportState = &vp,
                                        .pRasterizationState = &rs,
                                        .pMultisampleState = &ms,
                                        .pColorBlendState = &cb,
                                        .pDynamicState = &ds,
                                        .layout = g_t.layout};
    VkPipeline pipe = VK_NULL_HANDLE;
    VkShaderModule vs = vk_module_spv(post_vs_spv, (size_t)(post_vs_spv_size / 4));
    VkShaderModule fs = vk_module_spv(fs_words, (size_t)(fs_bytes / 4));
    st[0].module = vs;
    st[1].module = fs;
    if (vs && fs && !VK_OK(vkCreateGraphicsPipelines(g_vk.dev, vk_pipeline_cache(), 1, &gci, NULL, &pipe)))
        pipe = VK_NULL_HANDLE;
    if (vs) vkDestroyShaderModule(g_vk.dev, vs, NULL);
    if (fs) vkDestroyShaderModule(g_vk.dev, fs, NULL);
    if (!pipe)
        LOG_LIMIT(1, "arceus-temporal: the %s pipeline for format %d could not be made\n", what, (int)formats[0]);
    return pipe;
}

static VkDescriptorSet set_make(const VkImageView views[SETS_BINDINGS], const VkSampler samplers[SETS_BINDINGS]) {
    VkDescriptorSet set = vk_desc_alloc(g_t.dsl);
    VkDescriptorImageInfo di[SETS_BINDINGS];
    VkWriteDescriptorSet wr[SETS_BINDINGS];
    if (!set) return VK_NULL_HANDLE;
    for (u32 i = 0; i < SETS_BINDINGS; i++) {
        di[i] = (VkDescriptorImageInfo){samplers[i], views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        wr[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                       .dstSet = set,
                                       .dstBinding = i,
                                       .descriptorCount = 1,
                                       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                       .pImageInfo = &di[i]};
    }
    vkUpdateDescriptorSets(g_vk.dev, SETS_BINDINGS, wr, 0, NULL);
    return set;
}

/* one fullscreen triangle into view (w x h, in COLOR_ATTACHMENT_OPTIMAL), every pixel written */
/* One pass over an image of w by h. area: the scene's rectangle when the pass may write that alone, NULL for the
   whole image. The viewport stays the image's, so a pixel keeps its place. */
static void fullscreen2(VkCommandBuffer cb, VkImageView view, VkImageView second, u32 w, u32 h, VkPipeline pipe,
                        VkDescriptorSet set, const Constants* pc, const TemporalRect* area) {
    VkRenderingAttachmentInfo at[2] = {{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                        .imageView = view,
                                        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE},
                                       {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                        .imageView = second,
                                        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE}};
    VkRect2D sc = {{0, 0}, {w, h}};
    if (area) sc = (VkRect2D){{area->x, area->y}, {(u32)area->w, (u32)area->h}};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                          .renderArea = sc,
                          .layerCount = 1,
                          .colorAttachmentCount = second ? 2 : 1,
                          .pColorAttachments = at};
    VkViewport vp = {0, 0, (float)w, (float)h, 0, 1};
    rec_CmdBeginRendering(cb, &ri);
    vk_render_forget_state();
    rec_CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    rec_CmdSetViewport(cb, 0, 1, &vp);
    rec_CmdSetScissor(cb, 0, 1, &sc);
    rec_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_t.layout, 0, 1, &set, 0, NULL);
    rec_CmdPushConstants(cb, g_t.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof *pc, pc);
    rec_CmdDraw(cb, 3, 1, 0, 0);
    rec_CmdEndRendering(cb);
}
static void fullscreen(VkCommandBuffer cb, VkImageView view, u32 w, u32 h, VkPipeline pipe, VkDescriptorSet set,
                       const Constants* pc, const TemporalRect* area) {
    fullscreen2(cb, view, VK_NULL_HANDLE, w, h, pipe, set, pc, area);
}

/* out = a x b for 4 x 4 matrices in memory order. With the game's row vectors, a then b */
void vk_temporal_multiply(const float a[16], const float b[16], float out[16]) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            double sum = 0;
            for (int k = 0; k < 4; k++) sum += (double)a[4 * r + k] * (double)b[4 * k + c];
            out[4 * r + c] = (float)sum;
        }
}

static void block_read(u64 address, float out[16]) {
    for (u32 w = 0; w < 16; w++) {
        const u32 bits = gmem_r32(address + 4 * w);
        memcpy(&out[w], &bits, 4);
    }
}

/* Read only, for an upscaler to come: the camera block's matrices at +0x40 and +0x80 as the game wrote them, the
   frame's viewport, and what temporal_projection_camera makes of each. An event is the block seen first or the
   matrix at +0x80 changed. Eight events at most, each two lines, one a matrix: sixteen lines at most. Nothing
   uses the numbers. */
static void projection_log(u64 address, u32 size, const char* stage, u32 frame) {
    static float last[16];
    static u32 events;
    float at40[16], at80[16];
    if (events >= 8) return;
    block_read(address + 0x40, at40);
    block_read(address + 0x80, at80);
    if (events && !memcmp(last, at80, sizeof last)) return;
    memcpy(last, at80, sizeof last);
    events++;
    const float* both[2] = {at40, at80};
    for (int k = 0; k < 2; k++) {
        const float* m = both[k];
        static const char* const why[] = {"",
                                          "word %d, a scale, is not finite and positive",
                                          "word %d is not exactly -1",
                                          "word %d is not finite and positive",
                                          "word %d is not finite, or is negative",
                                          "word %d must be exactly zero and is not",
                                          "near is not above 0 or far is not beyond near",
                                          "the field of view is not above 0 and below pi",
                                          "the numbers do not pass the check on the floats handed on"};
        TemporalProjection p;
        const int read = temporal_projection_camera(m, &p);
        char made[200], reason[96];
        if (read == TEMPORAL_PROJECTION_OK)
            snprintf(made, sizeof made, "near %.9g, far %.9g (%s), field of view %.9g rad (%.4f degrees)",
                     p.near_plane, p.far_plane, p.far_infinite ? "at infinity" : "finite", p.fov_y,
                     p.fov_y * 57.29577951308232);
        else {
            snprintf(reason, sizeof reason, why[read], p.word);
            snprintf(made, sizeof made, "refused, not the game's form of projection: %s", reason);
        }
        LOG("arceus-temporal-projection: present %u, camera block %llx (%s stage, %x bytes bound), viewport %g,%g "
            "%gx%g, matrix at +0x%x in memory order: %.9g %.9g %.9g %.9g | %.9g %.9g %.9g %.9g | %.9g %.9g %.9g "
            "%.9g | %.9g %.9g %.9g %.9g. The decoder: %s\n",
            frame, (unsigned long long)address, stage, size, (double)g_t.viewport.x, (double)g_t.viewport.y,
            (double)g_t.viewport.width, (double)g_t.viewport.height, k ? 0x80 : 0x40, (double)m[0], (double)m[1],
            (double)m[2], (double)m[3], (double)m[4], (double)m[5], (double)m[6], (double)m[7], (double)m[8],
            (double)m[9], (double)m[10], (double)m[11], (double)m[12], (double)m[13], (double)m[14], (double)m[15],
            made);
    }
}

/* The camera of this frame, read at the first G-buffer draw: the view-projection and its inverse. Last frame's
   view-projection is the one in the block's neighbouring slot (the game writes the block to two slots in turn) when
   that equals the copy kept here, else the copy. */
static void camera_take(const GpuChannel* ch, Surf* zt) {
    const u32 frame = g_vk_present + 1;
    u64 address = ch->cbind[ST_FS][CAMERA_BANK];
    u32 size = ch->cbind_size[ST_FS][CAMERA_BANK];
    const char* stage = "fragment"; /* the stage the block is taken from, for the projection's line */
    if (!address || size < CAMERA_BYTES) {
        address = ch->cbind[ST_VS][CAMERA_BANK];
        size = ch->cbind_size[ST_VS][CAMERA_BANK];
        stage = "vertex";
    }
    if (!address || size < CAMERA_BYTES) {
        LOG_LIMIT(2, "arceus-temporal: no camera block at the G-buffer draw (bank %d size %x)\n", CAMERA_BANK, size);
        return;
    }
    const int had = g_t.camera_at && g_t.camera_at + 1 == frame;
    if (had) memcpy(g_t.kept, g_t.now, sizeof g_t.kept);
    block_read(address, g_t.now);
    block_read(address + CAMERA_INVERSE, g_t.inverse);
    if (address != g_t.camera_address[0]) {
        if (address != g_t.camera_address[1]) g_t.camera_new_at = frame;
        g_t.camera_address[1] = g_t.camera_address[0];
        g_t.camera_address[0] = address;
    }
    g_t.have_before = had;
    /* a neighbour that did not hold last frame's matrix: did it already hold this frame's? */
    if (g_t.miss_at && g_t.miss_at + 1 == frame) {
        if (!memcmp(g_t.miss_bytes[0], g_t.now, 64) || !memcmp(g_t.miss_bytes[1], g_t.now, 64)) g_t.miss_ahead++;
        else g_t.miss_other++;
        if (g_t.miss_ahead + g_t.miss_other <= 4 || (g_t.miss_ahead + g_t.miss_other) % 300 == 0)
            LOG("arceus-temporal: when the neighbouring slot missed, it held the next frame's matrix %llu times and "
                "something else %llu times\n",
                (unsigned long long)g_t.miss_ahead, (unsigned long long)g_t.miss_other);
    }
    g_t.miss_at = 0;
    if (had) {
        int equal = 0;
        memcpy(g_t.before, g_t.kept, sizeof g_t.before);
        for (int side = 0; side < 2 && !equal; side++) {
            const u8* other = gmem_host(side ? address + size : address - size, 64);
            if (other && !memcmp(other, g_t.kept, 64)) equal = 1 + side;
        }
        if (equal) g_t.pair_equal++;
        else {
            g_t.pair_differ++;
            for (int side = 0; side < 2; side++) {
                const u8* other = gmem_host(side ? address + size : address - size, 64);
                memset(g_t.miss_bytes[side], 0, 64);
                if (other) memcpy(g_t.miss_bytes[side], other, 64);
            }
            g_t.miss_at = frame;
        }
        if (g_t.pair_equal + g_t.pair_differ <= 4 || (g_t.pair_equal + g_t.pair_differ) % 1800 == 0)
            LOG("arceus-temporal: camera block %llx size %x, last frame's view-projection %s (equal %llu, not %llu)\n",
                (unsigned long long)address, size,
                equal == 1 ? "is in the slot below" : equal == 2 ? "is in the slot above" : "is in neither slot",
                (unsigned long long)g_t.pair_equal, (unsigned long long)g_t.pair_differ);
    }
    g_t.viewport = vk_render_viewport(ch->r3d);
    projection_log(address, size, stage, frame);
    /* The jitter was decided on another viewport than the scene's: the frame is left as drawn, so the next has no
       history, and this frame's object vectors are given up. */
    const TemporalViewport scene_view = {g_t.viewport.x, g_t.viewport.y, g_t.viewport.width, g_t.viewport.height};
    if (g_t.mode == MODE_TAA && !g_t.rect && temporal_gate_scene(&g_t.gate, frame, &scene_view)) {
        if (g_t.mismatched++ < 4 || g_t.mismatched % 600 == 0)
            LOG("arceus-temporal: the scene's viewport %g,%g %gx%g is not the one the jitter was decided on (%g,%g "
                "%gx%g), %llu frames so far\n",
                (double)g_t.viewport.x, (double)g_t.viewport.y, (double)g_t.viewport.width,
                (double)g_t.viewport.height, (double)g_t.gate.decided.x, (double)g_t.gate.decided.y,
                (double)g_t.gate.decided.width, (double)g_t.gate.decided.height,
                (unsigned long long)g_t.mismatched);
    }
    /* depth as vk_render_viewport makes its range: both depth modes store translate + scale x z */
    {
        const u32* r = ch->r3d;
        float vt[6], zn, zf;
        memcpy(vt, &r[0x280], sizeof vt);
        memcpy(&zn, &r[0x302], 4);
        memcpy(&zf, &r[0x303], 4);
        g_t.depth_zero_to_one = (int)(r[0x35F] & 1);
        if (vt[2] != 0.0f || vt[5] != 0.0f) {
            g_t.depth_translate = vt[5];
            g_t.depth_scale = vt[2];
        } else if (g_t.depth_zero_to_one) {
            g_t.depth_translate = zn;
            g_t.depth_scale = zf - zn;
        } else {
            g_t.depth_translate = (zn + zf) * 0.5f;
            g_t.depth_scale = (zf - zn) * 0.5f;
        }
    }
    g_t.depth_rt = zt;
    g_t.camera_at = frame;
}

/* The scene block of one stage at the draw vk_temporal_draw noted: whether the stage's program reads it as far as
   the fraction, and what is bound there (temporal_scene_block). */
static TemporalSceneBlock rect_block(int stage, const VkSh* program) {
    const GpuChannel* ch = g_t.draw_ch;
    const u64 address = ch->cbind[stage][TEMPORAL_SCENE_BANK];
    const u32 size = ch->cbind_size[stage][TEMPORAL_SCENE_BANK];
    const u8* bytes = address && size >= TEMPORAL_SCENE_BYTES ? gmem_host(address + TEMPORAL_SCENE_FRACTION, 8) : NULL;
    float words[2] = {0.0f, 0.0f};
    if (bytes) memcpy(words, bytes, sizeof words);
    return temporal_scene_block(program ? program->si.cbuf_mask : 0,
                                program ? program->si.cbuf_max[TEMPORAL_SCENE_BANK] : 0, address, size,
                                bytes ? words : NULL);
}
/* the geometry of the draw noted, from its own viewport in its target: 1 when it is a scene's */
static int rect_geometry(TemporalRectState* out) {
    const VkViewport v = vk_render_viewport(g_t.draw_r3d);
    const TemporalViewport view = {v.x, v.y, v.width, v.height};
    return temporal_rect_geometry(&view, (int)g_t.draw_w, (int)g_t.draw_h, TEMPORAL_CORNER_TOP_LEFT, out) ==
           TEMPORAL_RECT_OK;
}
static int rect_line(u64 n) { return n <= 4 || n % 600 == 0; }
/* 1 when the rectangle's gate let this frame be drawn with jitter */
static int rect_accepted(u32 frame) { return g_t.rect_gate.decided_at == frame && g_t.rect_gate.accepted; }

/* Our passes could not be made or run, or a draw could not be given its jitter, in a frame that was accepted,
   confirmed yet or not: its picture is shown as drawn, jittered in whole or in part. That must not repeat, so
   the gate accepts no frame any more this session (temporal_rect_failed). One line names what failed. */
static void rect_fail(u32 frame, const char* what) {
    TemporalRectGate* rg = &g_t.rect_gate;
    /* with an upscaler: a pass of ours that its hand-over needs failed in a frame drawn with jitter */
    if (upscaler_on() && upscaler_jittered(frame)) upscaler_outcome(frame, 1, TEMPORAL_UPSCALER_STOPPED, what);
    if (!g_t.rect || g_t.mode != MODE_TAA || rg->stopped || !rect_accepted(frame)) return;
    temporal_rect_failed(rg, frame);
    g_t.rect_stop_said = 1;
    LOG("arceus-temporal-rect: frame %u was drawn with jitter and the anti-aliasing passes failed (%s): no frame "
        "is drawn with jitter any more in this session\n", frame, what);
}

/* Under the rectangle's gate the passes after the resolve do not fail in silence, and do not come and go from
   frame to frame. The numbers and the views: none from here on, and a view that stood in for the picture gives
   way to the resolved picture. The frame and its history are not touched by it. */
static void rect_extras_off(const char* what) {
    if (!g_t.rect || g_t.rect_extras_off) return;
    g_t.rect_extras_off = 1;
    g_t.view = 0;
    LOG("arceus-temporal-rect: the numbers and the views are off for the rest of the session (%s could not be "
        "made)\n", what);
}

/* A scene draw of a decided frame, before our passes: the one vk_temporal_draw noted. The first whose vertex or
   fragment program reads the scene block as far as the fraction confirms the frame from what is bound there
   (temporal_rect_confirm), and it alone. Every one is held to the frame's viewport, and after the confirmation
   to its fraction in each stage that reads the block (temporal_rect_late). */
static void rect_watch(u32 frame) {
    TemporalRectGate* rg = &g_t.rect_gate;
    if (rg->decided_at != frame || g_t.ran_at == frame || !g_t.draw_scene) return;
    if (rg->state == TEMPORAL_FRAME_NONE) return;
    const TemporalSceneBlock vertex = rect_block(ST_VS, g_t.draw_vs), fragment = rect_block(ST_FS, g_t.draw_fs);
    const VkViewport v = vk_render_viewport(g_t.draw_r3d);
    TemporalRectState geometry;
    const int shaped = rect_geometry(&geometry);
    if (temporal_rect_late(rg, frame, shaped ? &geometry : NULL, &vertex, &fragment)) {
        if (rect_line(++g_t.rect_spoilt))
            LOG("arceus-temporal-rect: a later scene draw (fragment program %llx) disagrees with the frame: "
                "viewport %g,%g %gx%g in %ux%u against %g,%g %gx%g, scene block vertex %d/%d %g by %g, fragment "
                "%d/%d %g by %g, confirmed %g by %g. The frame is left as drawn and the next starts again, %llu "
                "frames so far\n",
                (unsigned long long)(g_t.draw_fs ? g_t.draw_fs->hash : 0), (double)v.x, (double)v.y,
                (double)v.width, (double)v.height, g_t.draw_w, g_t.draw_h, (double)rg->now.viewport.x,
                (double)rg->now.viewport.y, (double)rg->now.viewport.width, (double)rg->now.viewport.height,
                vertex.reads, vertex.bound, (double)vertex.fx, (double)vertex.fy, fragment.reads, fragment.bound,
                (double)fragment.fx, (double)fragment.fy, (double)rg->now.fx, (double)rg->now.fy,
                (unsigned long long)g_t.rect_spoilt);
        return;
    }
    if (rg->state != TEMPORAL_FRAME_DECIDED || (!vertex.reads && !fragment.reads)) return;
    const int was_accepted = rg->accepted;
    const int state = temporal_rect_confirm(rg, frame, &vertex, &fragment);
    if (state == TEMPORAL_FRAME_NONE) {
        if (rect_line(++g_t.rect_spoilt))
            LOG("arceus-temporal-rect: the first scene draw that reads the scene block (fragment program %llx) "
                "does not confirm the frame (vertex reads %d bound %d %g by %g, fragment reads %d bound %d %g by "
                "%g, rectangle %dx%d in %dx%d): the frame is left as drawn and the next starts again, %llu "
                "frames so far\n",
                (unsigned long long)(g_t.draw_fs ? g_t.draw_fs->hash : 0), vertex.reads, vertex.bound,
                (double)vertex.fx, (double)vertex.fy, fragment.reads, fragment.bound, (double)fragment.fx,
                (double)fragment.fy, rg->now.rect.w, rg->now.rect.h, rg->now.target_w, rg->now.target_h,
                (unsigned long long)g_t.rect_spoilt);
    } else if (state == TEMPORAL_FRAME_SEEN) {
        if (rect_line(++g_t.rect_seen))
            LOG("arceus-temporal-rect: fraction %g by %g confirmed for a frame that is not resolved (%s): it is the "
                "next frame's candidate, %llu frames so far\n",
                (double)rg->now.fx, (double)rg->now.fy,
                was_accepted ? "the fraction is new for this viewport" : "not accepted at its first draw",
                (unsigned long long)g_t.rect_seen);
    } else if (rect_line(++g_t.rect_steady))
        LOG("arceus-temporal-rect: frame confirmed and steady: rectangle %d,%d %dx%d of %dx%d, fraction %g by %g, "
            "%u offsets, %llu frames so far\n",
            rg->now.rect.x, rg->now.rect.y, rg->now.rect.w, rg->now.rect.h, rg->now.target_w, rg->now.target_h,
            (double)rg->now.fx, (double)rg->now.fy, g_t.offsets, (unsigned long long)g_t.rect_steady);
}

/* Every draw, from vk_temporal_draw, before its own constant blocks are bound. In a decided frame it is watched.
   In an undecided one a scene draw is kept, and when the next draw comes with the frame still undecided it goes
   to the gate as completed before the decision (temporal_rect_before). The draw that decides the frame is not
   such a draw: rect_decide drops it from here and watches it after its decision. */
static void rect_note(u32 frame) {
    TemporalRectGate* rg = &g_t.rect_gate;
    if (!g_t.rect) return;
    if (g_t.rect_kept.have && g_t.rect_kept.frame == frame && rg->decided_at != frame)
        temporal_rect_before(rg, frame, g_t.rect_kept.shaped ? &g_t.rect_kept.geometry : NULL, &g_t.rect_kept.vertex,
                             &g_t.rect_kept.fragment);
    g_t.rect_kept.have = 0;
    if (g_t.ran_at == frame || !g_t.draw_scene) return;
    if (rg->decided_at == frame) {
        rect_watch(frame);
        return;
    }
    g_t.rect_kept.have = 1;
    g_t.rect_kept.frame = frame;
    g_t.rect_kept.shaped = rect_geometry(&g_t.rect_kept.geometry);
    g_t.rect_kept.vertex = rect_block(ST_VS, g_t.draw_vs);
    g_t.rect_kept.fragment = rect_block(ST_FS, g_t.draw_fs);
}

/* The rectangle's gate at a draw that binds the scene camera's block while the frame is undecided. The frame is
   decided here whatever the draw is: one that is not into the scene's depth cannot be tied to the scene and
   leaves the frame without a geometry. Else the draw's own viewport decides (temporal_rect_decide), after a scene
   block that happens to be bound at this draw had its say (temporal_rect_early: it can only refuse). */
static void rect_decide(u32 frame) {
    TemporalRectGate* rg = &g_t.rect_gate;
    const int depth_draw = g_t.draw_zt && g_t.depth && g_t.draw_zt->gva == g_t.depth && g_t.draw_r3d && g_t.draw_ch;
    g_t.jitter_ndc[0] = g_t.jitter_ndc[1] = 0;
    g_t.rect_kept.have = 0; /* this draw decides: it is not a draw before the decision */
    /* The block's address was learned in this very frame, at its G-buffer: draws before that one may have bound
       it without being known, so this is not the frame's first such draw and the frame has no geometry. */
    if (g_t.camera_new_at == frame || !depth_draw) {
        temporal_rect_decide(rg, frame, NULL, 0);
        if (upscaler_on()) upscaler_decide(frame, 0);
        if (g_t.camera_new_at == frame) {
            if (rect_line(++g_t.rect_gated))
                LOG("arceus-temporal-rect: frame gated (the scene camera's block was at an address first seen in "
                    "this frame, so its first draw is not known), %llu frames so far\n",
                    (unsigned long long)g_t.rect_gated);
        } else if (rect_line(++g_t.early))
            LOG("arceus-temporal: a draw with depth target %llx binds the scene camera's block before the frame's "
                "first draw into the scene depth: no jitter this frame, %llu frames so far\n",
                (unsigned long long)(g_t.draw_zt ? g_t.draw_zt->gva : 0), (unsigned long long)g_t.early);
    } else {
        const VkViewport v = vk_render_viewport(g_t.draw_r3d);
        TemporalRectState geometry;
        const int shaped = rect_geometry(&geometry);
        /* A viewport whose only fault is that it is not on whole pixels: said once, in words, with the scales
           that would be for this very target. The frame is gated like any other without a geometry. */
        const TemporalViewport asked = {v.x, v.y, v.width, v.height};
        if (!shaped && temporal_viewport_fractional(&asked, (int)g_t.draw_w, (int)g_t.draw_h) &&
            !g_t.rect_fraction_said++) {
            char scales[96];
            const int found = temporal_whole_scales((int)g_t.draw_w, (int)g_t.draw_h, scales, sizeof scales);
            LOG("arceus-temporal-rect: the scene's viewport %g,%g %gx%g is not on whole pixels of its %ux%u target, "
                "so no frame of it is taken: the game draws the target's size times SNX_ARCEUS_SCENE_SCALE as it "
                "comes. Scales p/q with q up to 8 that give whole pixels in both axes of this target: %s. That is "
                "about whole pixels only, and no promise that a frame is taken\n",
                (double)v.x, (double)v.y, (double)v.width, (double)v.height, g_t.draw_w, g_t.draw_h,
                found ? scales : "none");
        }
        /* the scene block as bound at this draw, whether or not its programs read it: vertex stage first */
        TemporalSceneBlock early = rect_block(ST_VS, NULL);
        int stage = ST_VS;
        if (!early.bound) {
            early = rect_block(ST_FS, NULL);
            stage = ST_FS;
        }
        if (!g_t.rect_early_said++)
            LOG("arceus-temporal-rect: at the frame's first draw with the scene camera's block, a scene block of "
                "0x%x bytes or more is %s%s\n",
                TEMPORAL_SCENE_BYTES, early.bound ? "bound in the " : "not bound",
                !early.bound ? "" : stage == ST_VS ? "vertex stage" : "fragment stage");
        const int refused = shaped && temporal_rect_early(&geometry, &early);
        const int came_early = rg->early_at == frame;
        const int on = temporal_rect_decide(rg, frame, shaped ? &geometry : NULL, refused);
        if (shaped) g_t.offsets = temporal_rect_offsets(&geometry.rect, geometry.target_w, geometry.target_h);
        /* with an upscaler a frame the gate accepts is drawn with jitter only when a context is ready for it */
        const int taken = !upscaler_on() || upscaler_decide(frame, on);
        if (on && taken && g_t.mode == MODE_TAA) {
            float pixels[2];
            temporal_jitter(g_vk_present % g_t.offsets, g_t.offsets, pixels);
            g_t.jitter_ndc[0] = 2.0f * pixels[0] / fabsf(v.width);
            g_t.jitter_ndc[1] = 2.0f * pixels[1] / fabsf(v.height);
        } else if (!on && rect_line(++g_t.rect_gated))
            LOG("arceus-temporal-rect: frame gated at its first draw with the scene camera's block (viewport %g,%g "
                "%gx%g in a target of %ux%u: %s), %llu frames so far\n",
                (double)v.x, (double)v.y, (double)v.width, (double)v.height, g_t.draw_w, g_t.draw_h,
                !shaped       ? "not the whole target and no rectangle of a scene at its corner"
                : rg->stopped ? "the gate has stopped for this session"
                : came_early  ? "scene draws came before it"
                : refused     ? "the scene block bound at this draw has another fraction"
                              : "not the viewport of a confirmed frame before",
                (unsigned long long)g_t.rect_gated);
    }
    if (rg->stopped && !g_t.rect_stop_said++)
        LOG("arceus-temporal-rect: %d frames in a row were drawn with jitter and the anti-aliasing passes were "
            "not reached in them: no frame is drawn with jitter any more in this session\n", TEMPORAL_RECT_UNHOOKED);
    /* this draw is a scene draw of the frame like the ones after it */
    rect_watch(frame);
}

/* Whether this frame is drawn with jitter (the gate of temporal_math.h), asked at a draw that binds the scene
   camera's block while the frame is undecided. A draw into the scene's depth decides it from its own viewport.
   Any other draw cannot be tied to the scene and gates the whole frame, so no two draws of a frame differ in
   jitter. 1 when the frame is decided now. */
static int jitter_decide(u32 frame) {
    const Surf* zt = g_t.draw_zt;
    if (!zt || !g_t.depth || zt->gva != g_t.depth || !g_t.draw_r3d) {
        const int first = g_t.gate.early_at != frame;
        if (temporal_gate_early(&g_t.gate, frame) && first && (g_t.early++ < 4 || g_t.early % 600 == 0))
            LOG("arceus-temporal: a draw with depth target %llx binds the scene camera's block before the frame's "
                "first draw into the scene depth: no jitter this frame, %llu frames so far\n",
                (unsigned long long)(zt ? zt->gva : 0), (unsigned long long)g_t.early);
        return 0;
    }
    const VkViewport v = vk_render_viewport(g_t.draw_r3d);
    const float w = (float)zt->sw, h = (float)zt->sh;
    const TemporalViewport view = {v.x, v.y, v.width, v.height};
    const int on = temporal_gate_decide(&g_t.gate, frame, &view, w, h);
    g_t.jitter_ndc[0] = g_t.jitter_ndc[1] = 0;
    if (on) {
        float pixels[2];
        temporal_jitter(g_vk_present % JITTERS, JITTERS, pixels);
        g_t.jitter_ndc[0] = 2.0f * pixels[0] / fabsf(v.width);
        g_t.jitter_ndc[1] = 2.0f * pixels[1] / fabsf(v.height);
    } else if (g_t.gated++ < 4 || g_t.gated % 600 == 0)
        LOG("arceus-temporal: no jitter and no history this frame (viewport %g,%g %gx%g in a target of %gx%g, not "
            "the whole target or not last frame's), %llu frames so far\n",
            (double)v.x, (double)v.y, (double)v.width, (double)v.height, (double)w, (double)h,
            (unsigned long long)g_t.gated);
    return 1;
}

/* this frame's jitter as the draws got it, in clip units over w: 0 when the frame has none */
static void frame_jitter(u32 frame, double* jx, double* jy) {
    const int on = g_t.mode == MODE_TAA && (!upscaler_on() || upscaler_jittered(frame)) &&
                   (g_t.rect ? rect_accepted(frame) : temporal_gate_jitter(&g_t.gate, frame));
    *jx = on ? (double)g_t.jitter_ndc[0] : 0.0;
    *jy = on ? (double)g_t.jitter_ndc[1] : 0.0;
}

/* With the rectangle's gate: a stage of the draw about to be recorded reads bank 4 and this is bound there. When
   it is the scene camera's block the frame is decided now, before the draw's texture level bias is asked for and
   its samplers are chosen, so that the deciding draw has the frame's bias like every draw after it. */
void vk_temporal_camera_seen(u64 address, u32 bound, u32 size, u64 program) {
    const u32 frame = g_vk_present + 1;
    const int slot = address && (address == g_t.camera_address[0] || address == g_t.camera_address[1]);
    const int taken = slot && bound <= 0x1000 && bound >= CAMERA_BYTES && size <= bound;
    /* With an upscaler, until our passes ran: a draw into the scene's targets whose program reads bank 4, however
       much of it, must have the frame's decided camera block there. Any other block, at a known slot or not,
       and one that cannot decide the frame, leaves the frame's draws unaccounted for: it is not evaluated. */
    if (upscaler_on() && g_t.draw_scene && g_t.ran_at != frame &&
        temporal_upscaler_stranger(g_u.decided_at == frame, address, g_u.block, taken)) {
        temporal_upscaler_poison(&g_u.used, frame);
        if (g_u.strangers++ < 8)
            LOG("arceus-upscaler: frame %u: a draw into the scene's targets reads %u bytes of constant bank 4 (program "
                "%llx) bound to %llx, which is not the frame's camera block (%llx): the frame is not evaluated\n",
                frame, size, (unsigned long long)program, (unsigned long long)address,
                (unsigned long long)(g_u.decided_at == frame ? g_u.block : 0));
    }
    if (!g_t.rect || !taken) return;
    if (g_t.rect_gate.decided_at != frame) {
        if (upscaler_on()) g_u.block = address;
        rect_decide(frame);
    }
}

/* A draw binds constant bank 4: the scene camera's block, with anti-aliasing on, is replaced by a copy shifted by
   the frame's jitter, made once a submission in the frame's staging. Guest memory is not written. With row
   vectors a shift of clip x and y by jitter x w is the matrix times J, the jitter in its fourth row:
   view-projection x J at +0x00, projection x J at +0x80, J's inverse x the inverse view-projection at +0xC0. */
int vk_temporal_camera_block(u64 address, u32 bound, u32 size, VkBuffer* buffer, u64* offset) {
    /* Without the rectangle's gate no draw is jittered before our passes ran once. With it the gate's own start
       (a confirmed frame before) stands in for that, and the frame must be decided before our passes can run. */
    if ((g_t.mode != MODE_TAA && !g_t.rect) || !address || (!g_t.rect && !g_t.motion.w) ||
        (address != g_t.camera_address[0] && address != g_t.camera_address[1]))
        return 0;
    if (bound > 0x1000 || bound < CAMERA_BYTES || size > bound) {
        /* with an upscaler: a draw took a known camera slot in a way that is not looked at, so the frame's
           draws are not all accounted for and it is not evaluated */
        if (upscaler_on()) temporal_upscaler_poison(&g_u.used, g_vk_present + 1);
        return 0;
    }
    /* With the rectangle's gate the frame is decided here in the probe too, which then draws without jitter. The
       renderer asks before the draw's samplers are chosen (vk_temporal_camera_seen), so this is a second look. */
    if (g_t.rect && g_t.rect_gate.decided_at != g_vk_present + 1) {
        if (upscaler_on()) g_u.block = address;
        rect_decide(g_vk_present + 1);
    }
    if (g_t.mode != MODE_TAA) return 0;
    /* only +0x00, +0x80 and +0xC0 are shifted: a program that reads further is worth knowing about */
    if (size > g_t.camera_read) {
        g_t.camera_read = size;
        LOG("arceus-temporal: a program reads %u of the camera block's %u bytes\n", size, bound);
    }
    const u32 offsets = g_t.rect ? g_t.offsets : JITTERS, index = g_vk_present % offsets;
    if (g_t.rect) {
        if (!rect_accepted(g_vk_present + 1)) return 0;
        /* No jitter is staged for an upscaler unless a context is ready for this frame. What the draw takes is
           noted all the same, by value, so that the hook can confirm the frame before a context is settled. */
        if (upscaler_on() && !upscaler_jittered(g_vk_present + 1)) {
            float taken[0xC0 / 4];
            gmem_read(address, taken, sizeof taken);
            temporal_upscaler_used(&g_u.used, g_vk_present + 1, taken + 0x80 / 4);
            temporal_upscaler_used(&g_u.used_view, g_vk_present + 1, taken);
            return 0;
        }
    } else {
        if (g_t.gate.decided_at != g_vk_present + 1 && !jitter_decide(g_vk_present + 1)) return 0;
        if (!temporal_gate_jitter(&g_t.gate, g_vk_present + 1)) return 0;
    }
    /* the guest's bytes now: the copy is remade when they differ from the ones it was made from */
    u8 source[0x1000];
    gmem_read(address, source, bound);
    /* once this frame's scene camera is known, a block with another view-projection is another camera in the slot.
       Before that (the depth pre-pass) the address alone decides. */
    if (g_t.camera_at == g_vk_present + 1 && memcmp(source, g_t.now, sizeof g_t.now)) {
        if (g_t.other_cameras++ < 4 || g_t.other_cameras % 3600 == 0)
            LOG("arceus-temporal: another camera at %llx is drawn without jitter, %llu draws so far\n",
                (unsigned long long)address, (unsigned long long)g_t.other_cameras);
        /* with an upscaler: a draw of the frame was given another camera, without jitter */
        if (upscaler_on()) temporal_upscaler_poison(&g_u.used, g_vk_present + 1);
        return 0;
    }
    if (g_t.jitter_address != address || g_t.jitter_submit != vk_submit_number() || g_t.jitter_index != index ||
        g_t.jitter_size != bound || g_t.jitter_made[0] != g_t.jitter_ndc[0] ||
        g_t.jitter_made[1] != g_t.jitter_ndc[1] || memcmp(source, g_t.jitter_source, bound)) {
        u8* bytes = vk_stage(bound, vk_stage_align(), &g_t.jitter_buffer, &g_t.jitter_offset);
        float pixels[2], m[16];
        if (!bytes) {
            /* with an upscaler: this draw goes without its jittered copy */
            if (upscaler_on()) temporal_upscaler_poison(&g_u.used, g_vk_present + 1);
            rect_fail(g_vk_present + 1, "no room for the jittered camera block");
            return 0;
        }
        memcpy(bytes, source, bound);
        memcpy(g_t.jitter_source, source, bound);
        temporal_jitter(index, offsets, pixels);
        /* in the viewport's pixels the frame's jitter was decided on, which are the target's */
        const float jx = g_t.jitter_ndc[0], jy = g_t.jitter_ndc[1];
        g_t.jitter_made[0] = jx;
        g_t.jitter_made[1] = jy;
        /* the view-projection and the projection x J, and J's inverse x the inverse view-projection */
        static const u32 forward[2] = {0x00, 0x80};
        for (int k = 0; k < 2; k++) {
            memcpy(m, source + forward[k], sizeof m);
            temporal_jitter_forward(m, jx, jy);
            memcpy(bytes + forward[k], m, sizeof m);
        }
        memcpy(m, source + CAMERA_INVERSE, sizeof m);
        temporal_jitter_inverse(m, jx, jy);
        memcpy(bytes + CAMERA_INVERSE, m, sizeof m);
        g_t.jitter_address = address;
        g_t.jitter_submit = vk_submit_number();
        g_t.jitter_index = index;
        g_t.jitter_size = bound;
        if (!g_t.jittered && bound >= CAMERA_BYTES + 16) {
            /* once, the words after the matrices we know, as the game wrote them */
            char line[1024];
            int at = 0;
            for (u32 o = CAMERA_BYTES; o + 4 <= bound && at < (int)sizeof line - 12; o += 4) {
                u32 word;
                memcpy(&word, source + o, 4);
                at += snprintf(line + at, sizeof line - (size_t)at, " %08x", word);
            }
            LOG("arceus-temporal: camera block from +0x%x:%s\n", CAMERA_BYTES, line);
        }
        if (g_t.jittered++ < 3)
            LOG("arceus-temporal: camera block %llx drawn with jitter %g,%g pixels (offset %u of %u)\n",
                (unsigned long long)address, (double)pixels[0], (double)pixels[1], index, offsets);
    }
    /* with an upscaler: the unjittered projection and view-projection this draw's copy was made from, by value */
    if (upscaler_on()) {
        float projection[16], view[16];
        memcpy(projection, source + 0x80, sizeof projection);
        memcpy(view, source, sizeof view);
        temporal_upscaler_used(&g_u.used, g_vk_present + 1, projection);
        temporal_upscaler_used(&g_u.used_view, g_vk_present + 1, view);
    }
    *buffer = g_t.jitter_buffer;
    *offset = g_t.jitter_offset;
    return 1;
}

static float half_float(u16 h) {
    const int e = h >> 10 & 31, m = h & 1023;
    const float v = e == 0 ? ldexpf((float)m, -24) : e == 31 ? (m ? NAN : INFINITY) : ldexpf((float)(m + 1024), e - 25);
    return h & 0x8000 ? -v : v;
}

/* The numbers of the run the buffer holds, a second old by now: for the pixels sampled, the mean and longest camera
   vector, and by depth band how many differ from last frame by more than STATS_ERROR with the vector and without. */
static void stats_log(void) {
    static const char* const names[3] = {"near", "mid", "far"};
    unsigned n[3] = {0, 0, 0}, wrong[3] = {0, 0, 0}, wrong_still[3] = {0, 0, 0}, sky = 0, off = 0, changed = 0;
    unsigned shown = 0, shown_fine[2] = {0, 0}, still_fine[2] = {0, 0};
    unsigned object_n = 0, object_wrong = 0, object_camera = 0, object_still = 0, skin_near = 0;
    const int taa = g_t.mode == MODE_TAA;
    double length = 0, longest = 0;
    double drawn_gradient = 0, shown_gradient = 0;
    u16 capped[OBJECT_DRAWS + 1];
    /* per extra draw: its sampled pixels, and those wrong with its vector, with the camera's, with none */
    /* and those rejected, and of them the ones without a finite distance from the camera's vector */
    static u16 per_draw[OBJECT_DRAWS + 1][6];
    static float per_draw_far[OBJECT_DRAWS + 1], per_draw_long[OBJECT_DRAWS + 1];
    memset(per_draw_long, 0, sizeof per_draw_long);
    unsigned rejected = 0, rejected_blind = 0, rejected_all = 0;
    float rejected_far = 0.0f;
    memset(per_draw_far, 0, sizeof per_draw_far);
    memset(capped, 0, sizeof capped);
    memset(per_draw, 0, sizeof per_draw);
    for (u32 i = 0; i < STATS_W * STATS_H; i++) {
        /* the left half's texel of this sample, then the right half's */
        const u16* words = &g_t.stats_words[4 * (i / STATS_W * 2 * STATS_W + i % STATS_W)];
        const u16* more = words + 4 * STATS_W;
        const float moved = half_float(words[0]), still = half_float(words[1]);
        float vector = half_float(words[2]);
        const float z = half_float(words[3]);
        if (taa) {
            drawn_gradient += (double)half_float(more[0]);
            shown_gradient += (double)half_float(more[1]);
        }
        /* A rejected object pixel: how far its vector is from the camera's, 60000 for no finite distance
           (temporal_stats.frag). Sky and samples whose vector leaves the picture are not among the counted ones. */
        const float residual = half_float(more[2]);
        const int blind = residual > 59500.0f;
        if (residual > 0.0f) {
            rejected_all++;
            rejected_blind += blind;
            if (!blind && residual > rejected_far) rejected_far = residual;
        }
        /* the probe marks a pixel that took an object's vector in the thousands of the length (temporal_stats.frag) */
        const int object_marks = vector >= 1000.0f ? (int)(vector / 1000.0f) : 0;
        vector -= 1000.0f * (float)object_marks;
        /* an object's vector at the cap: counted for the draw that left it */
        if ((object_marks & 1) && vector >= 989.5f) {
            const float number = half_float(more[0]);
            capped[number >= 1.0f && number <= (float)OBJECT_DRAWS ? (u32)(number + 0.5f) : 0]++;
        }
        if (z == -1.0f) {
            sky++;
            continue;
        }
        if (z == -2.0f) {
            off++;
            continue;
        }
        /* the projection gives z = 0.1 / distance: nearer than 10, to 100, beyond */
        const int band = z > 0.01f ? 0 : z > 0.001f ? 1 : 2;
        n[band]++;
        /* with anti-aliasing on the first number is three marks: 1 history dropped, 2 the frame as drawn changed
           since the last one, 4 the resolved picture changed since the last one */
        const int marks = taa ? (int)(moved + 0.5f) : 0;
        wrong[band] += taa ? marks & 1 : moved > STATS_ERROR;
        changed += marks >> 1 & 1;
        shown += marks >> 2 & 1;
        shown_fine[0] += marks >> 3 & 1;
        shown_fine[1] += marks >> 4 & 1;
        wrong_still[band] += still > STATS_ERROR;
        rejected += residual > 0.0f;
        skin_near += object_marks >> 2 & 1;
        if (object_marks & 1) {
            const float number = half_float(more[0]);
            const u32 d = number >= 1.0f && number <= (float)OBJECT_DRAWS ? (u32)(number + 0.5f) : 0;
            u16* tally = per_draw[d];
            tally[0]++;
            tally[1] += moved > STATS_ERROR;
            tally[2] += object_marks >> 1 & 1;
            tally[3] += still > STATS_ERROR;
            if (vector > per_draw_long[d]) per_draw_long[d] = vector;
            if (residual > 0.0f) {
                tally[4]++;
                tally[5] += blind;
                if (!blind && residual > per_draw_far[d]) per_draw_far[d] = residual;
            }
            object_n++;
            object_wrong += moved > STATS_ERROR;
            object_camera += object_marks >> 1 & 1;
            object_still += still > STATS_ERROR;
        }
        still_fine[0] += still > 0.05f;
        still_fine[1] += still > 0.02f;
        length += vector;
        if (vector > longest) longest = vector;
    }
    const unsigned all = n[0] + n[1] + n[2];
    char bands[600];
    int at = 0;
    /* with anti-aliasing on, the first number is where the history was dropped and the second where the resolved
       colour is far from the frame's own: trails behind what moves show there. frame_to_frame is the share of
       pixels that differ from the frame before as drawn: with a still camera, the jitter at work on the edges.
       shown_frame_to_frame is the same for the resolved picture, where the jitter should be gone. */
    if (taa && g_t.view == 4) {
        /* the jittered frame alone: nothing is resolved, so only the frame as drawn has a number */
        LOG("arceus-temporal-stats: run=%llu vector_mean=%.2f px frame_to_frame=%.1f%% (view 4: no resolve, no "
            "numbers for the shown picture)\n",
            (unsigned long long)g_t.stats_run, all ? length / all : 0.0, all ? 100.0 * changed / all : 0.0);
        return;
    }
    /* sharpness: the gradient length of the picture shown over that of the frame as drawn, summed over every
       sample. An edge the resolve smooths lowers it, and so does a blur of the whole picture. */
    if (taa)
        at += snprintf(bands, sizeof bands,
                       " frame_to_frame=%.1f%% shown_frame_to_frame=%.1f%% above_0.05=%.1f%% above_0.02=%.1f%% "
                       "sharpness=%.3f (gradient drawn %.5f, shown %.5f)",
                       all ? 100.0 * changed / all : 0.0, all ? 100.0 * shown / all : 0.0,
                       all ? 100.0 * shown_fine[0] / all : 0.0, all ? 100.0 * shown_fine[1] / all : 0.0,
                       drawn_gradient > 0.0 ? shown_gradient / drawn_gradient : 0.0,
                       drawn_gradient / (STATS_W * STATS_H), shown_gradient / (STATS_W * STATS_H));
    for (int b = 0; b < 3; b++)
        at += snprintf(bands + at, sizeof bands - (size_t)at, taa ? " %s n=%u dropped=%.1f%% far_from_frame=%.1f%%"
                                                               : " %s n=%u wrong=%.1f%% without_vectors=%.1f%%",
                       names[b], n[b], n[b] ? 100.0 * wrong[b] / n[b] : 0.0,
                       n[b] ? 100.0 * wrong_still[b] / n[b] : 0.0);
    /* the probe: the frame as drawn against the one before at the finer steps too, the base for the same shares
       of the resolved picture */
    if (!taa && at < (int)sizeof bands)
        at += snprintf(bands + at, sizeof bands - (size_t)at, " frame_to_frame_above_0.05=%.1f%% above_0.02=%.1f%%",
                       all ? 100.0 * still_fine[0] / all : 0.0, all ? 100.0 * still_fine[1] / all : 0.0);
    /* the pixels that took an object's vector: wrong with it, with the camera's vector, with none */
    if (!taa && g_t.objects && at < (int)sizeof bands)
        snprintf(bands + at, sizeof bands - (size_t)at,
                 " object_pixels n=%u wrong=%.1f%% with_camera_vectors=%.1f%% without_vectors=%.1f%% "
                 "skinned_depth_near_miss n=%u",
                 object_n, object_n ? 100.0 * object_wrong / object_n : 0.0,
                 object_n ? 100.0 * object_camera / object_n : 0.0, object_n ? 100.0 * object_still / object_n : 0.0,
                 skin_near);
    /* rejected: counted in this sampled frame only, one frame of the numbers, not every frame */
    /* two counts: of every sample, and of the eligible ones the shares above are taken of (not sky, the vector
       inside the picture). The distance is the largest finite one, which the shader caps at 59000. */
    char refused[260] = "";
    if (g_t.reject)
        snprintf(refused, sizeof refused, " rejected_sampled of_all n=%u of %u, of_eligible n=%u of %u, "
                 "without_finite_camera_distance n=%u, largest_finite_distance=%.0f px (a half float, capped at 59000)",
                 rejected_all, STATS_W * STATS_H, rejected, all, rejected_blind, (double)rejected_far);
    LOG("arceus-temporal-stats: run=%llu vector_mean=%.2f px vector_max=%.2f px sky=%u off_screen=%u%s%s%s\n",
        (unsigned long long)g_t.stats_run, all ? length / all : 0.0, longest, sky, off, bands, refused,
        g_s.stats_private ? " (of the private picture before the scale, not of the picture shown)" : "");
    /* The probe names the draws that left a sampled vector longer than an eighth of the scene's height in that
       frame, four at most. The length has the camera's part in it, so a fast pan names draws too and nothing here
       says a pixel would be rejected. It is a half float, cut at 990 px by the numbers' shader. */
    int long_named = 0;
    for (u32 d = 1; !taa && g_t.stats_height && d <= g_t.stats_draws && d <= OBJECT_DRAWS && long_named < 4; d++) {
        const struct Drawn* w = &g_t.stats_drawn[d];
        if (!(per_draw_long[d] > (float)g_t.stats_height / 8.0f)) continue;
        long_named++;
        LOG("arceus-temporal-longest: run=%llu draw %u (%s, block %llx, draw %llx, matrix before from %s, origin "
            "moved %.0f px): longest sampled vector about %.0f px with the camera's part (990 at most), sampled pixels "
            "n=%u wrong=%u "
            "with_camera_vectors=%u\n",
            (unsigned long long)g_t.stats_run, d, w->skinned ? "skinned" : "rigid", (unsigned long long)w->block,
            (unsigned long long)w->signature,
            w->source == TEMPORAL_BEFORE_OWN    ? "its own address"
            : w->source == TEMPORAL_BEFORE_LINK ? "its linked slot"
                                                : "the other slot",
            (double)w->shift, (double)per_draw_long[d], per_draw[d][0], per_draw[d][1], per_draw[d][2]);
    }
    /* the probe names the draws of the rejected samples, four at most */
    int named = 0;
    for (u32 d = 0; !taa && d <= OBJECT_DRAWS && named < 4; d++) {
        const struct Drawn* w = &g_t.stats_drawn[d];
        if (!per_draw[d][4]) continue;
        named++;
        if (!d || d > g_t.stats_draws) {
            LOG("arceus-temporal-rejected: run=%llu %u sampled pixels rejected name no draw\n",
                (unsigned long long)g_t.stats_run, per_draw[d][4]);
            continue;
        }
        LOG("arceus-temporal-rejected: run=%llu draw %u (%s, block %llx, draw %llx): %u of its %u eligible sampled "
            "pixels rejected (%u without a finite camera distance, the largest finite one about %.0f px, a half "
            "float capped at 59000), wrong with its vector %u\n",
            (unsigned long long)g_t.stats_run, d, w->skinned ? "skinned" : "rigid", (unsigned long long)w->block,
            (unsigned long long)w->signature, per_draw[d][4], per_draw[d][0], per_draw[d][5],
            (double)per_draw_far[d], per_draw[d][1]);
    }
    /* the draws whose origin moved far on screen: what the picture says of their vectors, four at most */
    int moves = 0;
    for (u32 d = 1; !taa && d <= g_t.stats_draws && d <= OBJECT_DRAWS && moves < 4; d++) {
        const struct Drawn* w = &g_t.stats_drawn[d];
        if (!w->far) continue;
        moves++;
        LOG("arceus-temporal-moved: run=%llu draw %u (%s, block %llx, origin moved %.0f px): sampled pixels n=%u "
            "wrong=%u with_camera_vectors=%u without_vectors=%u\n",
            (unsigned long long)g_t.stats_run, d, w->skinned ? "skinned" : "rigid", (unsigned long long)w->block,
            (double)w->shift, per_draw[d][0], per_draw[d][1], per_draw[d][2], per_draw[d][3]);
    }
    /* the draws that left vectors at the cap, four at most */
    int lines = 0;
    for (u32 d = 0; d <= OBJECT_DRAWS && lines < 4; d++) {
        const struct Drawn* w = &g_t.stats_drawn[d];
        if (!capped[d]) continue;
        lines++;
        if (!d) {
            LOG("arceus-temporal-long: run=%llu %u sampled pixels with a vector at the cap name no draw\n",
                (unsigned long long)g_t.stats_run, capped[d]);
            continue;
        }
        LOG("arceus-temporal-long: run=%llu %u sampled pixels with a vector at the cap from draw %u: %s, block %llx, "
            "feedback output %llx, matrix before from %s, origin moved %.1f px\n",
            (unsigned long long)g_t.stats_run, capped[d], d, w->skinned ? "skinned" : "rigid",
            (unsigned long long)w->block, (unsigned long long)w->feedback,
            w->source == TEMPORAL_BEFORE_OWN       ? "its own address"
            : w->source == TEMPORAL_BEFORE_PARTNER ? "the other slot"
            : w->source == TEMPORAL_BEFORE_LINK    ? "its linked slot"
                                                   : "none (this frame's)",
            (double)w->shift);
    }
}

static int stats_ready(void) {
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                              .size = (u64)2 * STATS_W * STATS_H * 8,
                              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VmaAllocationCreateInfo bai = {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                                            VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                   .usage = VMA_MEMORY_USAGE_AUTO};
    VmaAllocationInfo info;
    if (g_t.stats_buffer) return 1;
    if (!VK_OK(vmaCreateBuffer(g_vk.vma, &bci, &bai, &g_t.stats_buffer, &g_t.stats_memory, &info))) {
        g_t.stats_buffer = VK_NULL_HANDLE;
        return 0;
    }
    g_t.stats_words = info.pMappedData;
    return g_t.stats_words != NULL;
}

/* What a draw tells before it is recorded: the composite names the scene colour and depth, the first G-buffer draw
   of a frame has the camera, a write to the scene colour gives its surface, and the first draw that samples the
   scene colour names the program before which the probe runs from the next frame on. */
/* Read only, for the log alone, with probe or taa: what three readers of the scene are drawn with. In every
   600th frame, for the first recorded draw of each of their fragment programs (by the whole 64 bit hash,
   temporal_readers_program, the composite under either of its two), lines that begin "arceus-temporal-readers:"
   and name the frame and the program. No draw is changed. */
#define READERS_TEXTURES 16
static struct {
    int pending, which; /* a reader's draw is described here, and whether it is recorded is not known yet */
    u32 frame;
    u64 program;
    char draw[900], blocks[3][700];
    int nt;
    struct {
        char surface[160];
        int header; /* the game's two headers were there */
        u32 tsc[3], tic[2];
        int bound; /* a sampler was bound for the slot while this draw was set up */
        VkSampler sampler;
    } texture[READERS_TEXTURES];
} g_readers;
static u32 g_readers_said_at[3];
/* float words of a constant block of a stage into the line: each word, or why it cannot be read */
static size_t readers_words(char* line, size_t at, size_t room, const GpuChannel* ch, int stage, int bank,
                            const u32* offsets, int count) {
    const u64 address = ch->cbind[stage][bank];
    const u32 size = ch->cbind_size[stage][bank];
    if (!address) return at + (size_t)snprintf(line + at, room - at, "not bound");
    at += (size_t)snprintf(line + at, room - at, "at %llx, %u bytes bound:", (unsigned long long)address, size);
    for (int k = 0; k < count && at + 48 < room; k++) {
        const u8* bytes = size >= offsets[k] + 4 ? gmem_host(address + offsets[k], 4) : NULL;
        float value = 0.0f;
        if (bytes) memcpy(&value, bytes, sizeof value);
        if (bytes) at += (size_t)snprintf(line + at, room - at, " +%X %.9g", offsets[k], (double)value);
        else
            at += (size_t)snprintf(line + at, room - at, " +%X %s", offsets[k],
                                   size >= offsets[k] + 4 ? "not readable" : "too short");
    }
    return at;
}
/* The draw is only described here, into g_readers: nothing is printed. The renderer may set a draw up more than
   once before it records it, and may not record it at all. The lines are printed where the draw is recorded
   (vk_temporal_recorded), for the draw that is, once. */
static void readers_note(u32 frame, const GpuChannel* ch, const VkSh* vs, const VkSh* fs0, Surf* const* sampled,
                         const u32* const* tic, const u32* const* tsc, int nt, Surf* const* rt, u32 n) {
    static const char* const names[3] = {"copy of the scene colour", "blur prepare", "composite"};
    static const u32 scene[13] = {0x628, 0x62C, 0x670, 0x674, 0x710, 0x714, 0x718,
                                  0x71C, 0x720, 0x724, 0x728, 0x794, 0x798};
    static const u32 fraction[2] = {0x628, 0x62C};
    static const u32 first[16] = {0x0,  0x4,  0x8,  0xC,  0x10, 0x14, 0x18, 0x1C,
                                  0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C};
    const int which = fs0 && !(frame % 600) ? temporal_readers_program(fs0->hash) : -1;
    /* A reader's draw was described and another draw begins: of another program, so that one was not recorded.
       The same program again is the same draw set up once more, or the reader's next, and which is not known
       here: it is described anew, and the description that is replaced gets a line and is counted. */
    if (g_readers.pending && (which < 0 || g_readers.which != which || g_readers.frame != frame)) {
        LOG("arceus-temporal-readers: frame %u, program %llx: a draw of it was set up and not recorded, so nothing "
            "is said of that draw\n", g_readers.frame, (unsigned long long)g_readers.program);
        g_readers.pending = 0;
    } else if (g_readers.pending) {
        static u64 replaced;
        LOG("arceus-temporal-readers: frame %u, program %llx: the description of a draw of it was replaced before "
            "that draw was recorded, by the same draw set up again or by the program's next draw (which is not "
            "known here), %llu such so far\n", frame, (unsigned long long)g_readers.program,
            (unsigned long long)++replaced);
    }
    if (which < 0 || g_readers_said_at[which] == frame) return;
    memset(&g_readers, 0, sizeof g_readers);
    g_readers.pending = 1;
    g_readers.which = which;
    g_readers.frame = frame;
    g_readers.program = fs0->hash;
    const VkViewport v = vk_render_viewport(ch->r3d);
    const Surf* colour = NULL;
    u32 colours = 0;
    for (u32 i = 0; i < n; i++)
        if (rt[i]) {
            if (!colour) colour = rt[i];
            colours++;
        }
    /* The game's scissor of its first viewport, as its registers have it: whether the test is on, and the
       rectangle in the game's own pixels, before the render scale and before a flip of the window's origin. */
    const u32* r = ch->r3d;
    const u32 xs = r[0x381], ys = r[0x382];
    size_t at = (size_t)snprintf(g_readers.draw, sizeof g_readers.draw,
                                 "the first recorded draw of the %s, with vertex program %llx. The game's registers: "
                                 "viewport %g,%g %gx%g, scissor test %s, its rectangle x %u to %u, y %u to %u in the "
                                 "game's pixels%s. %u colour targets, the first ",
                                 names[which], (unsigned long long)(vs ? vs->hash : 0), (double)v.x, (double)v.y,
                                 (double)v.width, (double)v.height, r[0x380] & 1 ? "on" : "off", xs & 0xFFFF,
                                 xs >> 16, ys & 0xFFFF, ys >> 16, r[0x4EB] & 1 ? " (y counted from the bottom)" : "",
                                 colours);
    if (colour)
        snprintf(g_readers.draw + at, sizeof g_readers.draw - at, "at %llx of %ux%u (image %ux%u). %d sampled "
                                                                  "textures",
                 (unsigned long long)colour->gva, colour->w, colour->h, colour->sw, colour->sh, nt);
    else
        snprintf(g_readers.draw + at, sizeof g_readers.draw - at, "none. %d sampled textures", nt);
    g_readers.nt = nt;
    for (int i = 0; i < nt && i < READERS_TEXTURES; i++) {
        if (sampled[i])
            snprintf(g_readers.texture[i].surface, sizeof g_readers.texture[i].surface,
                     "at %llx of %ux%u (image %ux%u)", (unsigned long long)sampled[i]->gva, sampled[i]->w,
                     sampled[i]->h, sampled[i]->sw, sampled[i]->sh);
        else
            snprintf(g_readers.texture[i].surface, sizeof g_readers.texture[i].surface, "none");
        if (tsc && tsc[i] && tic && tic[i]) {
            g_readers.texture[i].header = 1;
            memcpy(g_readers.texture[i].tsc, tsc[i], sizeof g_readers.texture[i].tsc);
            g_readers.texture[i].tic[0] = tic[i][4];
            g_readers.texture[i].tic[1] = tic[i][5];
        }
    }
    /* the blocks: the scene block of both stages, and the start of the vertex stage's bank 1 */
    at = (size_t)snprintf(g_readers.blocks[0], sizeof g_readers.blocks[0],
                          "the fragment stage's scene block (bank 3), %u bytes read by the program, ",
                          fs0->si.cbuf_max[TEMPORAL_SCENE_BANK]);
    readers_words(g_readers.blocks[0], at, sizeof g_readers.blocks[0], ch, ST_FS, TEMPORAL_SCENE_BANK, scene, 13);
    at = (size_t)snprintf(g_readers.blocks[1], sizeof g_readers.blocks[1], "the vertex stage's scene block (bank 3) ");
    readers_words(g_readers.blocks[1], at, sizeof g_readers.blocks[1], ch, ST_VS, TEMPORAL_SCENE_BANK, fraction, 2);
    at = (size_t)snprintf(g_readers.blocks[2], sizeof g_readers.blocks[2],
                          "the vertex stage's bank 1, its first sixteen words, ");
    readers_words(g_readers.blocks[2], at, sizeof g_readers.blocks[2], ch, ST_VS, 1, first, 16);
}
/* From the renderer, where a fragment stage's texture gets its sampler while a draw is set up: the sampler that
   is bound, for a reader's draw that is being described. */
void vk_temporal_sampler(u32 slot, VkSampler sampler) {
    if (!g_readers.pending || slot >= READERS_TEXTURES) return;
    g_readers.texture[slot].bound = 1;
    g_readers.texture[slot].sampler = sampler;
}
/* From the renderer, where a draw's first viewport and scissor are made for Vulkan: after the render scale and
   the turn of the window's origin, as they are recorded. Here the lines of a reader's draw are printed, once. */
void vk_temporal_recorded(const VkViewport* viewport, const VkRect2D* scissor) {
    static const char* const modes[5] = {"repeat", "mirrored repeat", "clamp to edge", "clamp to border",
                                         "mirror clamp to edge"};
    char head[96], line[1900];
    if (!g_readers.pending) return;
    g_readers.pending = 0;
    g_readers_said_at[g_readers.which] = g_readers.frame;
    snprintf(head, sizeof head, "arceus-temporal-readers: frame %u, program %llx", g_readers.frame,
             (unsigned long long)g_readers.program);
    LOG("%s: %s\n", head, g_readers.draw);
    for (int i = 0; i < g_readers.nt && i < READERS_TEXTURES; i++) {
        VkSamplerCreateInfo made;
        size_t at = (size_t)snprintf(line, sizeof line, "texture %d %s. ", i, g_readers.texture[i].surface);
        /* the sampler Vulkan has for the slot: what it was made with, not what the game's header says */
        if (!g_readers.texture[i].bound)
            at += (size_t)snprintf(line + at, sizeof line - at,
                                   "Vulkan's sampler: not known, none was bound for the slot while this draw was set "
                                   "up (a draw can keep the draw before's)");
        else if (!vk_sampler_made(g_readers.texture[i].sampler, &made))
            at += (size_t)snprintf(line + at, sizeof line - at, "Vulkan's sampler: bound, and what it was made with "
                                                                "was not kept");
        else
            at += (size_t)snprintf(line + at, sizeof line - at,
                                   "Vulkan's sampler as it was made: mag %s, min %s, between levels %s, x %s, y %s, "
                                   "levels %g to %g, level bias %g, %s coordinates, compare %s, anisotropy %s",
                                   made.magFilter == VK_FILTER_LINEAR ? "linear" : "nearest",
                                   made.minFilter == VK_FILTER_LINEAR ? "linear" : "nearest",
                                   made.mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR ? "linear" : "nearest",
                                   (u32)made.addressModeU < 5 ? modes[made.addressModeU] : "another mode",
                                   (u32)made.addressModeV < 5 ? modes[made.addressModeV] : "another mode",
                                   (double)made.minLod, (double)made.maxLod, (double)made.mipLodBias,
                                   made.unnormalizedCoordinates ? "unnormalised" : "normalised",
                                   made.compareEnable ? "on" : "off", made.anisotropyEnable ? "on" : "off");
        /* beside it, under its own name: the game's sampler header as it is, numbers and words */
        if (g_readers.texture[i].header) {
            const u32* t = g_readers.texture[i].tsc;
            snprintf(line + at, sizeof line - at,
                     ". The game's sampler header: mag %u, min %u, mip %u, address x %u, y %u, levels %g to %g, words "
                     "%08x %08x %08x. The game's texture header, words 4 and 5: %08x %08x",
                     t[1] & 3, (t[1] >> 4) & 3, (t[1] >> 6) & 3, t[0] & 7, (t[0] >> 3) & 7,
                     (double)(t[2] & 0xFFF) / 256.0, (double)((t[2] >> 12) & 0xFFF) / 256.0, t[0], t[1], t[2],
                     g_readers.texture[i].tic[0], g_readers.texture[i].tic[1]);
        } else
            snprintf(line + at, sizeof line - at, ". The game's headers for the slot were not there");
        LOG("%s: %s\n", head, line);
    }
    if (g_readers.nt > READERS_TEXTURES)
        LOG("%s: %d more textures are not listed\n", head, g_readers.nt - READERS_TEXTURES);
    for (int k = 0; k < 3; k++) LOG("%s: %s\n", head, g_readers.blocks[k]);
    LOG("%s: recorded with Vulkan's viewport %g,%g %gx%g, depth %g to %g, and scissor %d,%d %ux%u, in the image's "
        "pixels\n", head, (double)viewport->x, (double)viewport->y, (double)viewport->width,
        (double)viewport->height, (double)viewport->minDepth, (double)viewport->maxDepth, scissor->offset.x,
        scissor->offset.y, scissor->extent.width, scissor->extent.height);
}

void vk_temporal_draw(GpuChannel* ch, const VkSh* vs, const VkSh* fs0, Surf* const* sampled,
                      const u32* const* tic, const u32* const* tsc, int nt, Surf* const* rt, u32 n, Surf* zt) {
    const u32 frame = g_vk_present + 1;
    spatial_now(frame); /* a frame's first draw settles the one before, before this frame is decided */
    g_t.draw_zt = zt;
    g_t.draw_r3d = ch->r3d;
    g_t.draw_ch = ch;
    g_t.draw_vs = vs;
    g_t.draw_fs = fs0;
    if (!fs0) nt = 0;
    readers_note(frame, ch, vs, fs0, sampled, tic, tsc, nt, rt, n);
    u32 colours = 0;
    int writes_scene = 0;
    const Surf* scene_colour = NULL;
    for (u32 i = 0; i < n; i++) {
        if (!rt[i]) continue;
        colours++;
        if (g_t.scene && rt[i]->gva == g_t.scene) {
            g_t.scene_rt = rt[i];
            g_t.written_at = frame;
            writes_scene = 1;
            scene_colour = rt[i];
        }
    }
    /* for the rectangle's gate: a scene draw has the scene's depth as its depth target or the scene colour among
       its colour targets, and its viewport is held against that target's size */
    const int depth_draw = zt && g_t.depth && zt->gva == g_t.depth;
    g_t.draw_scene = depth_draw || scene_colour;
    g_t.draw_w = depth_draw ? zt->sw : scene_colour ? scene_colour->sw : 0;
    g_t.draw_h = depth_draw ? zt->sh : scene_colour ? scene_colour->sh : 0;
    rect_note(frame);
    /* the composite: one colour target that is not the scene's float format, no depth target, and among its textures
       a float one and a depth one of the target's size */
    if (colours == 1 && n == 1 && !zt && rt[0]->f.vk != VK_FORMAT_B10G11R11_UFLOAT_PACK32 && !rt[0]->f.depth) {
        const Surf *colour = NULL, *depth = NULL;
        for (int i = 0; i < nt; i++) {
            const Surf* t = sampled[i];
            if (!t || t->w != rt[0]->w || t->h != rt[0]->h) continue;
            if (t->f.depth) depth = t;
            else if (t->f.vk == VK_FORMAT_B10G11R11_UFLOAT_PACK32) colour = t;
        }
        if (colour && depth && (colour->gva != g_t.scene || depth->gva != g_t.depth)) {
            g_t.scene = colour->gva;
            g_t.depth = depth->gva;
            g_t.reader = 0;
            LOG("arceus-temporal: scene colour %llx (%ux%u, format %d) and depth %llx (format %d) from the composite "
                "(fragment program %llx into %llx)\n",
                (unsigned long long)colour->gva, colour->w, colour->h, (int)colour->f.vk,
                (unsigned long long)depth->gva, (int)depth->f.vk, (unsigned long long)fs0->hash,
                (unsigned long long)rt[0]->gva);
        }
    }
    if (g_t.depth && zt && zt->gva == g_t.depth && colours >= 3 && g_t.camera_at != frame) camera_take(ch, zt);
    if (g_t.scene && !writes_scene && g_t.written_at == frame && g_t.reader_at != frame)
        for (int i = 0; i < nt; i++)
            if (sampled[i] && sampled[i]->gva == g_t.scene) {
                g_t.reader_at = frame;
                if (g_t.reader != fs0->hash) {
                    g_t.reader = fs0->hash;
                    LOG("arceus-temporal: the scene colour is first read by fragment program %llx\n",
                        (unsigned long long)fs0->hash);
                }
                break;
            }
}

/* The making of the images a frame's pictures are tied to under the rectangle's gate: a number that changes when
   the scene's image, a copy of the frame or a history image is another than the last time. */
static u64 rect_making(const Surf* scene) {
    const VkImage now[5] = {scene->img, g_t.colour[0].img, g_t.colour[1].img, g_t.history[0].img, g_t.history[1].img};
    if (memcmp(now, g_t.rect_images, sizeof now)) {
        memcpy(g_t.rect_images, now, sizeof now);
        g_t.rect_generation++;
    }
    return g_t.rect_generation;
}

/* SNX_ARCEUS_TEMPORAL_GPUTIME=1, read only: the approximate time between two completions on the GPU's clock,
   around our passes (temporal_math.h has the rules). Off, nothing here adds a command or a query. A failure
   switches the measure off for the session with one line and touches nothing else. The pool is never
   destroyed in a session: marks recorded with it may still be on their way. */
static void gputime_off(const char* why) {
    temporal_gpu_stop(&g_time_ring);
    g_t.gputime = 0;
    LOG("arceus-temporal-gputime: off for the rest of the session (%s)\n", why);
}
static int gputime_on(void) {
    if (g_t.gputime >= 0) return g_t.gputime;
    const char* asked = getenv("SNX_ARCEUS_TEMPORAL_GPUTIME");
    g_t.gputime = 0;
    if (!asked || strcmp(asked, "1")) return 0;
    VkQueueFamilyProperties families[32];
    u32 n = 32;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.phys, &n, families);
    g_t.time_bits = g_vk.gfx_family < n ? families[g_vk.gfx_family].timestampValidBits : 0;
    g_t.time_period = (double)g_vk.props.limits.timestampPeriod;
    if (!temporal_gpu_usable(g_vk.props.limits.timestampComputeAndGraphics, g_t.time_bits, g_t.time_period)) {
        LOG("arceus-temporal-gputime: not supported here (timestamps on the queue %d, %u valid bits, a period of %g "
            "ns): nothing is added\n",
            (int)g_vk.props.limits.timestampComputeAndGraphics, g_t.time_bits, g_t.time_period);
        return 0;
    }
    const VkQueryPoolCreateInfo info = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                        .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                        .queryCount = 2 * TEMPORAL_GPU_PAIRS};
    if (!VK_OK(vkCreateQueryPool(g_vk.dev, &info, NULL, &g_t.time_pool))) {
        g_t.time_pool = VK_NULL_HANDLE;
        LOG("arceus-temporal-gputime: the query pool could not be made: nothing is added\n");
        return 0;
    }
    g_t.gputime = 1;
    LOG("arceus-temporal-gputime: on. Two timestamps at the pipeline's last stage bracket a stretch of our "
        "commands, and their difference is the approximate time between the two completions: no exclusive time "
        "of the stretch and no bound on it. The clock has a period of %g ns and %u valid bits\n",
        g_t.time_period, g_t.time_bits);
    return 1;
}
/* A bracket's first mark: on a free pair only, whose two queries are reset in this same command buffer just
   before. The bracket's handle, -1 for none. */
static TemporalGpuHandle gputime_begin(VkCommandBuffer cb, int kind) {
    if (!gputime_on()) return -1;
    /* after a failed submission no pair is taken again: one could be reused whose reset never ran */
    if (vk_rec_submit_failed()) {
        gputime_off("a command buffer could not be begun or ended, a submission was refused or a command pool "
                    "could not be reset");
        return -1;
    }
    const TemporalGpuHandle h = temporal_gpu_begin(&g_time_ring, kind, vk_submit_number(), plat_ns());
    if (h < 0) return -1;
    const u32 first = 2 * (u32)temporal_gpu_index(h);
    rec_CmdResetQueryPool(cb, g_t.time_pool, first, 2);
    rec_CmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, g_t.time_pool, first);
    return h;
}
/* its second mark, recorded only while the submission is still the first mark's. The handle is then used up. */
static void gputime_end(VkCommandBuffer cb, TemporalGpuHandle* h) {
    if (*h >= 0 && gputime_on() && temporal_gpu_end(&g_time_ring, *h, vk_submit_number()))
        rec_CmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, g_t.time_pool,
                               2 * (u32)temporal_gpu_index(*h) + 1);
    *h = -1;
}
/* a bracket of an earlier run of our passes that never got its second mark (the passes returned between the
   two) is given up, so that its pair can leave the ring */
static void gputime_leftover(TemporalGpuHandle* h) {
    if (*h >= 0 && gputime_on()) temporal_gpu_abandon(&g_time_ring, *h);
    *h = -1;
}
/* Where the renderer has completed a slot's submissions and recycles it (cmd.c): every pair whose submission
   is known to have completed gives its time or is counted, and is free again. The completed number is read
   from the timeline as it stands and the results are asked for with their availability: nothing waits. */
void vk_temporal_gputime_poll(void) {
    if (g_t.gputime != 1) return;
    /* "Completed" is read from the timeline, and it means "ran" only while no submission has failed: the recorder
       signals a failed one's number itself. It marks the failure before that signal, so the mark is looked at
       after the timeline is read. With either wrong the measure stops and reads nothing more. */
    u64 done = 0;
    if (!vk_submit_completed(&done)) {
        gputime_off("the timeline's count of completed submissions could not be read");
        return;
    }
    if (vk_rec_submit_failed()) {
        gputime_off("a command buffer could not be begun or ended, a submission was refused or a command pool "
                    "could not be reset, so a completed submission need not have run");
        return;
    }
    const u64 now = plat_ns();
    for (int i = 0; i < TEMPORAL_GPU_PAIRS; i++) {
        const int state = g_time_ring.pair[i].state;
        /* a pair that is begun stays until its own end or until it is given up */
        if (state == TEMPORAL_GPU_FREE || state == TEMPORAL_GPU_BEGUN || done < g_time_ring.pair[i].submit) continue;
        u64 got[4] = {0, 0, 0, 0}; /* a tick and whether it is there, twice */
        unsigned long long ticks[2] = {0, 0};
        int there[2] = {0, 0}, fetched = 0;
        if (temporal_gpu_fetch(&g_time_ring, i, done)) {
            const VkResult rc =
                vkGetQueryPoolResults(g_vk.dev, g_t.time_pool, 2 * (u32)i, 2, sizeof got, got, 2 * sizeof(u64),
                                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
            if (rc != VK_SUCCESS && rc != VK_NOT_READY) {
                gputime_off("the results could not be asked for");
                return;
            }
            ticks[0] = got[0];
            ticks[1] = got[2];
            there[0] = got[1] != 0;
            there[1] = got[3] != 0;
            fetched = 1;
        }
        temporal_gpu_settle(&g_time_ring, i, done, now, fetched ? ticks : NULL, fetched ? there : NULL, g_t.time_bits,
                            g_t.time_period);
    }
}
/* A window's lines: under which configuration its times were taken, and for each kind of bracket how many were
   asked for, measured, still under way, and lost by each reason, with the median, the mean and the 95th
   percentile of the measured ones. Then the window is closed: the next begins empty, and brackets still under
   way are retired when they complete without giving it a time. */
static void gputime_window(const char* ended, const u32* c, u32 presents) {
    static const char* const names[TEMPORAL_GPU_KINDS] = {
        "our passes before the scene's first reader (vectors, the frame's copy, resolve, sharpen)",
        "the numbers and the view, only in the frames that have them",
        "an empty bracket, a baseline that is subtracted from nothing"};
    float sharpen;
    memcpy(&sharpen, &c[13], sizeof sharpen);
    LOG("arceus-temporal-gputime: approximate time between two completions, a window of %u presents %s. Taken "
        "with: %s, view %u, resolve options 0x%x, sharpen %.2f, object vectors %u, links %u, rejection %u, "
        "rectangle gate %u, images %ux%u, scene %ux%u at %u,%u. Ends that came late for their bracket and were "
        "ignored: %llu\n",
        presents, ended, c[0] == MODE_TAA ? "taa" : "probe", c[1], c[2], (double)sharpen, c[3], c[4], c[5], c[6],
        c[7], c[8], c[11], c[12], c[9], c[10], (unsigned long long)g_time_ring.late);
    for (int k = 0; k < TEMPORAL_GPU_KINDS; k++) {
        double median, mean, high;
        const unsigned n = g_time_ring.n[k], pending = temporal_gpu_pending(&g_time_ring, k);
        const unsigned long long* lost = g_time_ring.lost[k];
        temporal_gpu_summary(g_time_ring.ms[k], n, &median, &mean, &high);
        LOG("arceus-temporal-gputime: %s: asked %llu, measured %u, still under way and not carried over %u, split "
            "between submissions %llu, begun and not ended %llu, a result not there %llu, too old to rule out a "
            "turn of the clock %llu, longer than the host saw pass %llu, no free pair %llu. Retired from an "
            "earlier window, in no count above: %llu. Median %.4f ms, mean %.4f ms, 95th percentile %.4f ms\n",
            names[k], (unsigned long long)g_time_ring.asked[k], n, pending, lost[TEMPORAL_GPU_CUT],
            lost[TEMPORAL_GPU_PARTIAL], lost[TEMPORAL_GPU_UNAVAILABLE], lost[TEMPORAL_GPU_OLD],
            lost[TEMPORAL_GPU_ABSURD], lost[TEMPORAL_GPU_EXHAUSTED], lost[TEMPORAL_GPU_OUTDATED], median, mean, high);
    }
    temporal_gpu_restart(&g_time_ring);
}
/* Before a run of our passes records its first bracket: the configuration the run is taken under
   (temporal_gpu_configure). When it is another than the window's, the window is written and closed here, under
   its own configuration, so that this run's brackets are the new window's first and not the old one's last. */
static void gputime_configure(const Surf* scene, const TemporalRect* area) {
    u32 now[TEMPORAL_GPU_CONFIG] = {(u32)g_t.mode,
                                    (u32)g_t.view,
                                    (u32)g_t.options,
                                    (u32)g_t.objects,
                                    (u32)g_t.links,
                                    (u32)g_t.reject,
                                    (u32)g_t.rect,
                                    scene->sw,
                                    scene->sh,
                                    area ? (u32)area->x : 0,
                                    area ? (u32)area->y : 0,
                                    area ? (u32)area->w : scene->sw,
                                    area ? (u32)area->h : scene->sh,
                                    0,
                                    0,
                                    0};
    u32 before[TEMPORAL_GPU_CONFIG], presents;
    memcpy(&now[13], &g_t.sharpen, sizeof g_t.sharpen);
    if (temporal_gpu_configure(&g_t.time_window, now, g_vk_present, before, &presents))
        gputime_window("ended by a change of configuration", before, presents);
}
/* The window's clock is the count of presents. Called by the renderer right after it has raised that count,
   once a present and nowhere else, so that a window is written after 600 presents whether or not our passes
   ran in them and whichever way the present was reached (temporal_gpu_tick). Results are not read here. */
void vk_temporal_gputime_present(void) {
    u32 presents;
    if (g_t.gputime != 1) return;
    if (temporal_gpu_tick(&g_t.time_window, g_vk_present, &presents))
        gputime_window("", g_t.time_window.config, presents);
}

/* Before the draw whose fragment program is the scene colour's first reader: the probe's passes. Once a frame. */
int vk_temporal_enabled(void) { return temporal_on(); }

/* Object vectors. The world matrix of each object block that a G-buffer draw binds is kept in a table by its
   address, with the draws that bound it. The game writes many blocks to two slots in turn, one bound size apart, in
   step with the camera's block. Which slots are such a pair is learned from complete frames (temporal_math.h has
   the rules), and only an established pair, or a slot that takes no turns, gives a matrix of a frame ago. */
typedef struct {
    u64 key, partner;        /* partner: the other slot's address in the frame at, 0 for none */
    u32 at, before_at;       /* the frames world and before were taken in */
    u32 judged_at;           /* the frame turns and stays were judged for */
    u16 turns, stays;        /* temporal_pair_judge */
    u16 n, before_n;         /* the draws that bound the block in the frames at and before_at */
    u16 misses, said;        /* draws in a row without a matrix of a frame ago, and whether a line named the block */
    u64 sum, before_sum;     /* their signatures added up */
    u64 draws[OBJECT_SIGNATURES], before_draws[OBJECT_SIGNATURES];
    float world[16], before[16]; /* before: what this same address held the last time it was bound */
} Object;
static Object g_objects[OBJECTS];

static Object* object_slot(u64 key, u32 frame, int make) {
    Object* oldest = NULL;
    for (u32 i = 0; i < 8; i++) {
        Object* o = &g_objects[((key >> 8) * 0x9E3779B1u + i) % OBJECTS];
        if (o->key == key) return o;
        if (!oldest || o->at < oldest->at) oldest = o;
    }
    /* a slot not used for ten seconds may go */
    if (!make || (oldest->key && oldest->at + 600 > frame)) return NULL;
    memset(oldest, 0, sizeof *oldest);
    oldest->key = key;
    return oldest;
}

int vk_temporal_objects(void) { return temporal_on() && g_t.objects; }

/* Under the rectangle's gate: an object pass could not get a thing it needs (a program, the target, room for its
   block, a pipeline or a set). That is not a draw without a matrix, which falls back to the camera's vector by
   itself. This frame's object target is given up whole, as when a pair breaks, and object vectors are off for
   the rest of the session, with one line. The frame, its resolve and its history are not touched. */
void vk_temporal_object_failed(const char* what) {
    if (!g_t.rect || !g_t.objects) return;
    g_t.object_failed_at = g_vk_present + 1;
    g_t.objects = 0;
    LOG("arceus-temporal-rect: object vectors are off for the rest of the session (%s could not be made): the "
        "camera's vectors alone from here on\n", what);
}

/* The texture level shift for a draw into the scene (its depth target is the scene's), 0 for any other draw */
float vk_temporal_mip_bias(const Surf* zt) {
    if (!temporal_on() || !zt) return 0.0f;
    /* with an upscaler no bias of ours, set or automatic, in a frame its decision does not jitter */
    if (upscaler_on() && !upscaler_jittered(g_vk_present + 1)) return 0.0f;
    /* No value set and the rectangle's gate in use: every draw into the scene's depth of a frame drawn with
       jitter gets log2 of the scale. The frame is decided before its first such draw asks (render.c), so one
       frame has one bias. A value that was set, 0 included, takes the automatic one's place. */
    if (g_t.rect && g_t.mode == MODE_TAA && !g_t.mip_bias_set) {
        if (!g_t.depth || zt->gva != g_t.depth || !rect_accepted(g_vk_present + 1)) return 0.0f;
        return temporal_rect_bias(0, 0.0f, &g_t.rect_gate.now.rect, g_t.rect_gate.now.target_w);
    }
    /* A value that was set. With the rectangle's gate it is given, like the automatic one, only in a frame the
       gate accepted at its first draw. Without that gate it is given in every frame, as it always was. */
    if (g_t.rect && !rect_accepted(g_vk_present + 1)) return 0.0f;
    return g_t.mip_bias < 0.0f && zt == g_t.depth_rt ? g_t.mip_bias : 0.0f;
}

static void pairs_judge(void);
static void objects_log(u32 frame) {
    if (g_t.object_frame == frame) return;
    pairs_judge();
    g_t.touched_frame = frame;
    g_t.object_frame = frame;
    g_t.object_draws = 0;
    if (++g_t.object_frames % 600) return;
    LOG("arceus-temporal-objects: in 600 frames, G-buffer draws with an object block %llu. Without a matrix of a "
        "frame ago: nothing bound a frame ago %llu (never seen %llu, seen two frames ago or more %llu), pair still "
        "being learned %llu, another draw than the pair's %llu, at rest alone %llu, jumped too far %llu, table full "
        "%llu, not judged %llu, not an affine matrix %llu. With one: from its own address %llu, moved %llu, drawn "
        "again %llu, over the cap of %d a frame %llu, pipelines made %llu. Judged: pairs learned %llu, slots "
        "without turns learned %llu, pairs broken %llu (at rest alone %llu), frames whose object vectors were "
        "given up %llu. Skinned: reading a feedback buffer %llu, buffers kept %llu (a second time in a frame "
        "%llu), drawn again %llu, without last frame's buffer %llu, with a capture of another length %llu, "
        "reading past last frame's copy %llu, last frame's copy not this draw's or not known %llu, address "
        "captured more than once %llu, captures not copied %llu, captures without a slot %llu\n",
        (unsigned long long)g_t.object_seen, (unsigned long long)g_t.object_new,
        (unsigned long long)g_t.object_never, (unsigned long long)g_t.object_older,
        (unsigned long long)g_t.object_learning, (unsigned long long)g_t.object_other,
        (unsigned long long)g_t.object_rest, (unsigned long long)g_t.object_far,
        (unsigned long long)g_t.object_full, (unsigned long long)g_t.object_unjudged,
        (unsigned long long)g_t.object_odd, (unsigned long long)g_t.object_same,
        (unsigned long long)g_t.object_moved, (unsigned long long)g_t.object_drawn, OBJECT_DRAWS,
        (unsigned long long)g_t.object_capped, (unsigned long long)g_t.object_pipelines,
        (unsigned long long)g_t.pairs_learned, (unsigned long long)g_t.stays_learned,
        (unsigned long long)g_t.pairs_broken, (unsigned long long)g_t.rest_broken,
        (unsigned long long)g_t.broke_frames, (unsigned long long)g_t.object_skinned,
        (unsigned long long)g_t.skin_kept, (unsigned long long)g_t.skin_twice, (unsigned long long)g_t.skin_drawn,
        (unsigned long long)g_t.skin_first, (unsigned long long)g_t.skin_changed,
        (unsigned long long)g_t.skin_short, (unsigned long long)g_t.skin_unowned,
        (unsigned long long)g_t.skin_shared, (unsigned long long)g_t.skin_uncopied,
        (unsigned long long)g_t.skin_unkept);
    g_t.skin_kept = g_t.skin_drawn = g_t.skin_first = g_t.skin_twice = g_t.skin_changed = g_t.skin_short = 0;
    g_t.skin_unowned = g_t.skin_shared = g_t.skin_uncopied = g_t.skin_unkept = 0;
    g_t.object_seen = g_t.object_new = g_t.object_moved = g_t.object_drawn = g_t.object_capped = g_t.object_odd = 0;
    g_t.object_far = g_t.object_same = g_t.object_never = g_t.object_older = g_t.object_skinned = 0;
    g_t.object_learning = g_t.object_other = g_t.object_rest = g_t.object_full = g_t.object_unjudged = 0;
    g_t.pairs_learned = g_t.stays_learned = g_t.pairs_broken = g_t.rest_broken = g_t.broke_frames = 0;
    LOG("arceus-temporal-uncovered: in 600 frames, plain G-buffer draws without a matrix of a frame ago: rigid %llu "
        "(matrix not what the address last held %llu, address never bound before %llu), reading skinned positions "
        "%llu (%llu, %llu), block not looked at: rigid %llu, reading skinned positions %llu. Plain G-buffer draws "
        "reading skinned positions %llu, each counted once: with a matrix but not drawn again with last frame's "
        "positions %llu (the copy's refusals above, the stream's layout, the cap), and of those tried as a rigid "
        "object because the matrix moved %llu (tried, not known to be drawn)\n",
        (unsigned long long)g_t.uncovered[0][0], (unsigned long long)g_t.uncovered[0][1],
        (unsigned long long)g_t.uncovered[0][2], (unsigned long long)g_t.uncovered[1][0],
        (unsigned long long)g_t.uncovered[1][1], (unsigned long long)g_t.uncovered[1][2],
        (unsigned long long)g_t.uncovered_blind[0], (unsigned long long)g_t.uncovered_blind[1],
        (unsigned long long)g_t.skin_plain,
        (unsigned long long)g_t.skin_left, (unsigned long long)g_t.skin_rigid);
    if (g_t.links)
        LOG("arceus-temporal-links: in 600 frames, draws the pairs gave no matrix: a link gave one %llu, no link "
            "%llu, link still being learned %llu, another draw than the linked slot's at that place or past the "
            "eighth %llu, at rest alone %llu, records not complete or not of the frames before %llu. Links that "
            "reached their fourth frame %llu. Frames whose record is not complete: record full %llu, block table "
            "full or block not judged %llu, a draw before the camera's block or into another surface of the scene "
            "depth %llu (%llu such draws). Frames whose "
            "object vectors were given up for a link %llu\n",
            (unsigned long long)g_t.link_why[TEMPORAL_LINK_USED], (unsigned long long)g_t.link_why[TEMPORAL_LINK_NONE],
            (unsigned long long)g_t.link_why[TEMPORAL_LINK_LEARNING],
            (unsigned long long)g_t.link_why[TEMPORAL_LINK_OTHER], (unsigned long long)g_t.link_why[TEMPORAL_LINK_REST],
            (unsigned long long)g_t.link_why[TEMPORAL_LINK_UNKNOWN], (unsigned long long)g_t.links_made,
            (unsigned long long)g_t.link_unknown_frames[LINK_RECORD],
            (unsigned long long)g_t.link_unknown_frames[LINK_TABLE],
            (unsigned long long)g_t.link_unknown_frames[LINK_EARLY], (unsigned long long)g_t.link_early_draws,
            (unsigned long long)g_t.link_broke_frames);
    /* the draws still without a matrix with the links on, by what the links said, all and (moving) */
    for (int kind = 0; g_t.links && kind < 2; kind++) {
        const u64(*left)[2] = g_t.link_left[kind];
        LOG("arceus-temporal-links-left: in 600 frames, %s draws without a matrix: links not asked (the pairs' own "
            "refusal) %llu (%llu), a link's matrix refused as a jump %llu (%llu), records not complete %llu (%llu), "
            "no link %llu (%llu), still being learned %llu (%llu), another draw %llu (%llu), at rest alone %llu "
            "(%llu). In brackets: matrix not what the address last held\n",
            kind ? "skinned-reading" : "rigid", (unsigned long long)left[0][0], (unsigned long long)left[0][1],
            (unsigned long long)left[1][0], (unsigned long long)left[1][1], (unsigned long long)left[2][0],
            (unsigned long long)left[2][1], (unsigned long long)left[3][0], (unsigned long long)left[3][1],
            (unsigned long long)left[4][0], (unsigned long long)left[4][1], (unsigned long long)left[5][0],
            (unsigned long long)left[5][1], (unsigned long long)left[6][0], (unsigned long long)left[6][1]);
    }
    memset(g_t.link_left, 0, sizeof g_t.link_left);
    memset(g_t.link_why, 0, sizeof g_t.link_why);
    memset(g_t.link_unknown_frames, 0, sizeof g_t.link_unknown_frames);
    g_t.links_made = g_t.link_early_draws = g_t.link_broke_frames = 0;
    memset(g_t.uncovered, 0, sizeof g_t.uncovered);
    g_t.uncovered_blind[0] = g_t.uncovered_blind[1] = g_t.skin_plain = g_t.skin_left = g_t.skin_rigid = 0;
}

static int object_has_draw(const u64* draws, u32 n, u64 signature) {
    for (u32 i = 0; i < n && i < OBJECT_SIGNATURES; i++)
        if (draws[i] == signature) return 1;
    return 0;
}

/* The table's entry of the block at address with this frame's matrix and this draw in it. Several draws may bind
   one block: the first of a frame moves the matrix and the draws on. */
static Object* object_take(u64 address, u64 partner, const float world[16], u64 signature, u32 frame) {
    Object* o = object_slot(address, frame, 1);
    if (!o) {
        g_t.object_full++;
        return NULL;
    }
    if (o->at != frame) {
        memcpy(o->before, o->world, sizeof o->before);
        memcpy(o->before_draws, o->draws, sizeof o->before_draws);
        o->before_at = o->at;
        o->before_n = o->n;
        o->before_sum = o->sum;
        memcpy(o->world, world, sizeof o->world);
        o->at = frame;
        o->n = 0;
        o->sum = 0;
        o->partner = partner;
        /* an entry that does not fit the list is not judged, and what is not judged gives nothing */
        if (g_t.touched_n < OBJECT_TOUCHED) g_t.touched[g_t.touched_n++] = o;
        else g_t.object_unjudged++;
    }
    if (o->n < OBJECT_SIGNATURES) o->draws[o->n] = signature;
    if (o->n < 0xFFFF) o->n++;
    o->sum += signature;
    return o;
}

/* The end of a frame: every block bound in it is judged from what the complete frame shows (temporal_pair_judge).
   Run at the first object draw of a later frame, before anything of that frame is in the table. */
static void pairs_judge(void) {
    const u32 frame = g_t.touched_frame;
    for (u32 i = 0; i < g_t.touched_n; i++) {
        Object* o = g_t.touched[i];
        if (o->at != frame) continue;
        const Object* q = o->partner ? object_slot(o->partner, frame, 0) : NULL;
        TemporalSlotFrame f = {0};
        f.bound_before = o->before_at && o->before_at + 1 == frame;
        f.partner_now = q && q->at == frame;
        f.partner_before = q && q->at && (q->at + 1 == frame || (f.partner_now && q->before_at + 1 == frame));
        f.same_as_partner = q && q->at + 1 == frame && q->n == o->n && q->sum == o->sum;
        f.same_as_own = f.bound_before && o->before_n == o->n && o->before_sum == o->sum;
        f.at_rest_alone = q && q->at + 1 == frame &&
                          temporal_at_rest_alone(o->world, o->before_at + 2 == frame ? o->before : NULL, q->world);
        f.partner_turns = q && q->at + 1 == frame && q->judged_at == q->at ? q->turns : 0;
        f.own_stays = o->judged_at && o->judged_at + 1 == frame ? o->stays : 0;
        unsigned turns, stays;
        temporal_pair_judge(&f, &turns, &stays);
        if (turns + 1 == TEMPORAL_PAIR_FRAMES) g_t.pairs_learned++;
        if (stays + 1 == TEMPORAL_PAIR_FRAMES) g_t.stays_learned++;
        if (!turns && f.partner_turns + 1 >= TEMPORAL_PAIR_FRAMES) {
            g_t.pairs_broken++;
            if (f.at_rest_alone) g_t.rest_broken++;
        }
        o->turns = (u16)(turns < 0xFFFF ? turns : 0xFFFF);
        o->stays = (u16)(stays < 0xFFFF ? stays : 0xFFFF);
        o->judged_at = frame;
    }
    g_t.touched_n = 0;
}

/* One line for a block whose draws stay without a matrix of a frame ago: what the table holds of it, of both
   neighbouring slots, and of the blocks bound a frame ago that the same draws bound or that stood at the same place. */
static void object_missed(Object* o, u32 size, u64 signature, u32 frame, int source) {
    static u32 lines;
    if (source == TEMPORAL_BEFORE_OWN || source == TEMPORAL_BEFORE_PARTNER || source == TEMPORAL_BEFORE_LINK) {
        o->misses = 0;
        return;
    }
    if (o->misses < 0xFFFF) o->misses++;
    if (o->misses != 240 || o->said || lines >= 64) return;
    o->said = 1;
    lines++;
    char sides[400] = "", others[300] = "";
    int at = 0;
    for (int side = -1; side <= 1; side += 2) {
        const u64 key = side < 0 ? o->key - size : o->key + size;
        const Object* q = object_slot(key, frame, 0);
        const char* name = side < 0 ? "below" : "above";
        if (!q || !q->at)
            at += snprintf(sides + at, sizeof sides - (size_t)at, " %s never bound.", name);
        else
            at += snprintf(sides + at, sizeof sides - (size_t)at,
                           " %s%s bound %u and %u frames ago by %u draws (%s), turns %u stays %u, matrix %s.", name,
                           key == o->partner ? " (the partner)" : "", frame - q->at, frame - q->before_at, q->n,
                           object_has_draw(q->draws, q->n, signature) ? "this one among them" : "others", q->turns,
                           q->stays, memcmp(q->world, o->world, sizeof q->world) ? "another" : "equal");
        if (at >= (int)sizeof sides) break;
    }
    at = 0;
    for (u32 i = 0, found = 0; i < OBJECTS && found < 3 && at < (int)sizeof others; i++) {
        const Object* q = &g_objects[i];
        if (!q->key || q == o || q->at + 1 != frame) continue;
        const float dx = q->world[12] - o->world[12], dy = q->world[13] - o->world[13];
        const float dz = q->world[14] - o->world[14];
        const int same = object_has_draw(q->draws, q->n, signature);
        const int near = dx * dx + dy * dy + dz * dz < 0.0025f;
        if (!same && !near) continue;
        found++;
        at += snprintf(others + at, sizeof others - (size_t)at, " %llx (%+lld bytes, %s%s%u draws)",
                       (unsigned long long)q->key, (long long)(q->key - o->key), same ? "this draw, " : "",
                       near ? "this place, " : "", q->n);
    }
    LOG("arceus-temporal-objects: block %llx size %x stays without a matrix of a frame ago (%s): draw %llx, bound "
        "before %u frames ago by %u draws, judged %u frames ago, turns %u stays %u, matrix %s its last, origin %.2f "
        "%.2f %.2f.%s Bound a frame ago with this draw or at this place:%s\n",
        (unsigned long long)o->key, size, source == TEMPORAL_BEFORE_LEARNING ? "still being learned"
                                          : source == TEMPORAL_BEFORE_OTHER  ? "another draw" : "nothing bound",
        (unsigned long long)signature, frame - o->before_at, o->before_n, frame - o->judged_at, o->turns, o->stays,
        memcmp(o->world, o->before, sizeof o->world) ? "not" : "equal to", (double)o->world[12],
        (double)o->world[13], (double)o->world[14], sides, others[0] ? others : " none");
}

/* The matrix the draw's block had a frame ago, NULL for none (counted by reason), and in source and other where it
   came from. A draw gets one only from an established pair or from its own slot when that takes no turns. */
static const float* object_before(const Object* o, u64 signature, u32 frame, int* source, const Object** other) {
    const Object* q = o->partner ? object_slot(o->partner, frame, 0) : NULL;
    const int partner_now = q && q->at == frame;
    const int partner_last = q && q->at && (q->at + 1 == frame || (partner_now && q->before_at + 1 == frame));
    const int partner_then = q && q->at + 1 == frame; /* its entry still holds the frame before */
    *other = q;
    *source = temporal_before_choice(o->before_at && o->before_at + 1 == frame,
                                     o->judged_at && o->judged_at + 1 == frame ? o->stays : 0,
                                     object_has_draw(o->before_draws, o->before_n, signature), partner_last,
                                     partner_now, partner_then && q->judged_at == q->at ? q->turns : 0,
                                     partner_then && object_has_draw(q->draws, q->n, signature));
    g_t.current.chosen = *source;
    if (*source == TEMPORAL_BEFORE_PARTNER &&
        temporal_at_rest_alone(o->world, o->before_at + 2 == frame ? o->before : NULL, q->world)) {
        g_t.object_rest++;
        *source = TEMPORAL_BEFORE_NONE;
        return NULL;
    }
    if (*source == TEMPORAL_BEFORE_OWN) {
        g_t.object_same++;
        return o->before;
    }
    if (*source == TEMPORAL_BEFORE_PARTNER) return q->world;
    if (*source == TEMPORAL_BEFORE_LEARNING) g_t.object_learning++;
    else if (*source == TEMPORAL_BEFORE_OTHER) g_t.object_other++;
    else {
        g_t.object_new++;
        if (!o->before_at) g_t.object_never++;
        else g_t.object_older++;
    }
    return NULL;
}

/* The links (temporal_math.h). Four records in turn, one a frame: the one being filled, the two before it that a
   draw reads and are not changed any more, and one to spare. */
static TemporalFrame g_link_frames[4];
static TemporalLinkScratch g_link_scratch;

static TemporalFrame* link_record(u32 frame) {
    TemporalFrame* f = &g_link_frames[frame & 3];
    return frame && f->frame == frame ? f : NULL;
}

/* judges the frame's record as it now is against the two before it */
static void links_judge(u32 frame) {
    TemporalFrame* now = link_record(frame);
    if (now) temporal_links_judge(now, link_record(frame - 1), link_record(frame - 2), &g_link_scratch);
}

/* the first draw of a frame closes the record of the frame before it, complete now, and opens its own */
static void links_roll(u32 frame) {
    if (!g_t.links || g_t.link_frame == frame) return;
    if (g_t.link_frame) {
        const TemporalFrame* last = link_record(g_t.link_frame);
        links_judge(g_t.link_frame);
        for (u32 i = 0; last && i < last->n; i++) g_t.links_made += last->blocks[i].turns + 1 == TEMPORAL_PAIR_FRAMES;
    }
    temporal_frame_begin(&g_link_frames[frame & 3], frame);
    g_t.link_frame = frame;
}

/* something of the frame was not seen: its record proves nothing, counted once a frame by cause */
static void links_unknown(u32 frame, int cause) {
    if (!g_t.links) return;
    links_roll(frame);
    TemporalFrame* now = link_record(frame);
    if (now) now->complete = 0;
    if (g_t.link_unknown_at[cause] == frame) return;
    g_t.link_unknown_at[cause] = frame;
    g_t.link_unknown_frames[cause]++;
}

/* A block bound by a draw goes into the frame's record, which is there whichever way the draw came. Without room
   the frame is not complete. Off, nothing. */
static TemporalBlock* link_bind(u32 frame, u64 address, u32 size, const float world[16], int affine, u64 signature) {
    if (!g_t.links) return NULL;
    links_roll(frame);
    TemporalBlock* b = temporal_frame_bind(link_record(frame), address, size, world, affine, signature);
    if (!b) links_unknown(frame, LINK_RECORD);
    return b;
}

/* The draw vk_temporal_object_note is at goes into the frame's record. Where the pairs know nothing of its block,
   a link may give the matrix of a frame ago: from the record of the frame before, which nothing changes. */
static void link_try(u32 frame, u64 address, u32 size, u64 signature) {
    if (!g_t.links) return;
    links_roll(frame);
    TemporalFrame* now = link_record(frame);
    const int chosen = g_t.current.chosen;
    const int served =
        chosen == TEMPORAL_BEFORE_OWN || chosen == TEMPORAL_BEFORE_PARTNER || chosen == TEMPORAL_BEFORE_OTHER;
    if (!now) return;
    const TemporalBlock* mine = temporal_frame_find(now, address);
    const TemporalBlock* q = NULL;
    int why = TEMPORAL_LINK_NONE;
    if (!served)
        q = temporal_link_before(now, link_record(frame - 1), link_record(frame - 2), address, size,
                                 mine ? mine->count : 0, signature, g_t.current.world, &why);
    TemporalBlock* b = link_bind(frame, address, size, g_t.current.world, 1, signature);
    if (!b) {
        /* no room in the record: the links were asked and nothing is known */
        if (!served) g_t.current.link_why = TEMPORAL_LINK_UNKNOWN;
        return;
    }
    if (served) {
        b->served = 1;
        return;
    }
    g_t.link_why[why]++;
    g_t.current.link_why = why;
    if (!q) return;
    g_t.current.was = q->world;
    g_t.current.source = TEMPORAL_BEFORE_LINK;
    g_t.current.link = q->address;
    g_t.current.pair = address < q->address ? address : q->address;
    g_t.current.pair_other = address < q->address ? q->address : address;
    g_t.current.other = NULL;
}

/* After the G-buffer, before the vectors are read: 1 when every pair this frame's extra draws relied on still
   holds now that the frame's draws are known. The other slot was not bound too, and both ends have the same draws.
   Else the frame's object vectors are given up and the camera's stand. */
static int pairs_hold(u32 frame) {
    int judged = 0;
    for (u32 d = 1; d <= g_t.object_draws && d <= OBJECT_DRAWS; d++) {
        const struct Drawn* w = &g_t.drawn[d];
        const Object *o = w->slot, *q = w->other;
        int broke = 0;
        if (w->source == TEMPORAL_BEFORE_LINK) {
            /* the links are judged again on the frame's G-buffer as it now is: the draw's must still stand */
            const TemporalFrame* now = link_record(frame);
            if (!judged++) links_judge(frame);
            broke = !now || !temporal_link_holds(now, w->block, w->link);
            if (broke && g_t.link_logged_at != frame) {
                g_t.link_logged_at = frame;
                g_t.link_broke_frames++;
            }
        } else if (w->source == TEMPORAL_BEFORE_PARTNER)
            broke = !o || !q || q->at + 1 != frame || o->n != q->n || o->sum != q->sum;
        else if (w->source == TEMPORAL_BEFORE_OWN)
            broke = !o || o->before_at + 1 != frame || o->n != o->before_n || o->sum != o->before_sum;
        /* a skinned draw took last frame's copy, and its address was captured again later in this frame */
        const int again = w->stream && temporal_stream_conflict(w->stream, frame);
        if (!broke && !again) continue;
        g_t.broke_frames++;
        if (g_t.broke_logged_at + 60 <= frame) { /* one example a second */
            g_t.broke_logged_at = frame;
            LOG("arceus-temporal-objects: the pair of block %llx broke after its draw (%s), the frame's object "
                "vectors are given up, %llu frames so far\n",
                (unsigned long long)w->block,
                again                              ? "its positions' address was captured again"
                : w->source == TEMPORAL_BEFORE_LINK ? "its link did not hold"
                : w->source == TEMPORAL_BEFORE_OWN  ? "its own draws changed"
                : q && q->at == frame            ? "both slots bound"
                                                 : "the draws changed",
                (unsigned long long)g_t.broke_frames);
        }
        return 0;
    }
    return 1;
}

/* Another object may have taken the slot: a jump of more than 5 units in a frame is not a move. 1 for a jump. */
static int object_jumped(u64 address, const float world[16], const float was[16], u32 frame) {
    const float dx = world[12] - was[12], dy = world[13] - was[13], dz = world[14] - was[14];
    const float distance = dx * dx + dy * dy + dz * dz;
    if (distance < 25.0f) return 0;
    g_t.object_far++;
    if (g_t.far_logged_at + 60 <= frame) { /* one example a second */
        g_t.far_logged_at = frame;
        LOG("arceus-temporal-objects: jump of %.2f units at block %llx: now %.2f %.2f %.2f, a frame ago "
            "%.2f %.2f %.2f\n",
            sqrt((double)distance), (unsigned long long)address, (double)world[12], (double)world[13],
            (double)world[14], (double)was[12], (double)was[13], (double)was[14]);
    }
    return 1;
}

/* Notes the extra draw that comes next, for the numbers: its block, where its matrix of a frame ago came from and
   how far its origin moved on screen. A long move with a still camera has a line, one a second. Returns the draw's
   number. */
static u32 drawn_note(u64 block, u64 feedback, int skinned, int source, const float world[16], const float* was,
                      const Surf* zt, u32 frame) {
    const u32 number = g_t.object_draws + 1;
    struct Drawn* d = &g_t.drawn[number <= OBJECT_DRAWS ? number : 0];
    d->slot = g_t.current.slot;
    d->other = g_t.current.other;
    d->stream = NULL;
    d->block = block;
    d->feedback = feedback;
    d->signature = g_t.current.signature;
    d->link = source == TEMPORAL_BEFORE_LINK ? g_t.current.link : 0;
    d->skinned = (u8)skinned;
    d->source = (u8)source;
    /* the scene's size on screen: the target's, or under the rectangle's gate the viewport's */
    const double scene_w = g_t.rect ? fabs((double)g_t.viewport.width) : (double)zt->sw;
    const double scene_h = g_t.rect ? fabs((double)g_t.viewport.height) : (double)zt->sh;
    d->shift = (float)temporal_origin_shift(g_t.now, world, g_t.before, was, scene_w, scene_h);
    const float camera = (float)temporal_origin_shift(g_t.now, world, g_t.before, world, scene_w, scene_h);
    d->far = d->shift > 100.0f && camera >= 0.0f && camera < 20.0f;
    if (d->far && g_t.long_logged_at + 60 <= frame) {
        g_t.long_logged_at = frame;
        LOG("arceus-temporal-objects: long move of %.0f px (the camera alone %.0f px) at block %llx (%s, before from "
            "%s): now %.2f %.2f %.2f, a frame ago %.2f %.2f %.2f\n",
            (double)d->shift, (double)camera, (unsigned long long)block, skinned ? "skinned" : "rigid",
            source == TEMPORAL_BEFORE_OWN    ? "its own address"
            : source == TEMPORAL_BEFORE_LINK ? "its linked slot"
                                             : "the other slot",
            (double)world[12],
            (double)world[13], (double)world[14], (double)was[12], (double)was[13], (double)was[14]);
    }
    return number;
}

/* Every plain G-buffer draw is shown here once, before its extra draw is tried: its block's matrix and the draw's
   signature (vertex and index buffer addresses, index count, first index, vertex program) go into the table.
   1 when the block has a matrix of a frame ago. vk_temporal_skin and vk_temporal_object then work on this draw. */
int vk_temporal_object_note(const GpuChannel* ch, const Surf* zt, u64 vertex, u64 index, u32 count, u32 first,
                            u64 program) {
    const u32 frame = g_vk_present + 1;
    const u64 address = ch->cbind[ST_VS][OBJECT_BANK];
    const u32 size = ch->cbind_size[ST_VS][OBJECT_BANK];
    g_t.current.was = NULL;
    g_t.current.slot = g_t.current.other = NULL;
    g_t.current.address = 0;
    g_t.current.link = 0;
    g_t.current.link_why = -1;
    if (!g_t.objects || !zt) return 0;
    const u64 signature = temporal_draw_signature(vertex, index, count, first, program);
    /* The links' record must see every draw with an object block into the scene depth, by its address or by the
       surface the pairs go by (temporal_scene_gate). Where the two do not agree, or the camera's block is not
       known yet, the draw is noted as not seen. A block of a size the pairs leave alone is there, not usable. */
    const int seen = temporal_scene_gate(g_t.links, address != 0, g_t.depth && zt->gva == g_t.depth,
                                         zt == g_t.depth_rt, g_t.camera_at == frame);
    if (seen == TEMPORAL_SCENE_UNSEEN) {
        g_t.link_early_draws++;
        links_unknown(frame, LINK_EARLY);
    } else if (seen == TEMPORAL_SCENE_SEEN && (size < 64 || size > 0x10000)) {
        static const float none[16];
        link_bind(frame, address, size, none, 0, signature);
    }
    if (zt != g_t.depth_rt || !address || size < 64 || size > 0x10000 || g_t.camera_at != frame) return 0;
    objects_log(frame);
    g_t.object_seen++;
    block_read(address, g_t.current.world);
    const float* world = g_t.current.world;
    if (world[3] != 0.0f || world[7] != 0.0f || world[11] != 0.0f || world[15] != 1.0f) {
        g_t.object_odd++;
        /* such a block is in the record all the same: it counts among the blocks with the same draws */
        link_bind(frame, address, size, world, 0, signature);
        return 0;
    }
    /* the slot this block would have had a frame ago: the camera's block went the same way */
    u64 partner = 0;
    if (g_t.camera_address[0] && g_t.camera_address[1])
        partner = g_t.camera_address[0] > g_t.camera_address[1] ? address - size : address + size;
    const u64 unjudged = g_t.object_unjudged;
    Object* o = object_take(address, partner, world, signature, frame);
    /* a block the table could not take or will not judge: what the pairs make of it is not known */
    if (g_t.links && (!o || g_t.object_unjudged != unjudged)) links_unknown(frame, LINK_TABLE);
    if (!o) {
        link_bind(frame, address, size, world, 1, signature);
        return 0;
    }
    const Object* other = NULL;
    g_t.current.address = address;
    g_t.current.slot = o;
    g_t.current.was = object_before(o, signature, frame, &g_t.current.source, &other);
    g_t.current.signature = signature;
    g_t.current.pair = g_t.current.source == TEMPORAL_BEFORE_PARTNER && partner < address ? partner : address;
    g_t.current.pair_other = 0;
    g_t.current.other = (void*)other;
    link_try(frame, address, size, signature);
    object_missed(o, size, signature, frame, g_t.current.source);
    if (g_t.current.was && object_jumped(address, world, g_t.current.was, frame)) g_t.current.was = NULL;
    return g_t.current.was != NULL;
}

/* For the numbers: what became of the plain G-buffer draw just shown to vk_temporal_object_note. known is its
   answer, reads 1 when the draw reads skinned positions, again 1 when it was drawn again with last frame's. Called
   before the rigid try, which may still draw it again by its matrix alone (the try is counted there). */
void vk_temporal_object_fate(int known, int reads, int again) {
    g_t.current.unskinned = 0;
    if (!g_t.objects || g_t.object_frame != g_vk_present + 1) return;
    g_t.current.unskinned = known && reads && !again;
    const Object* o = g_t.current.slot;
    g_t.skin_plain += reads != 0;
    if (known) {
        g_t.skin_left += reads && !again;
        return;
    }
    if (!o) {
        g_t.uncovered_blind[reads != 0]++;
        return;
    }
    u64* count = g_t.uncovered[reads != 0];
    const int moving = o->before_at && memcmp(o->world, o->before, sizeof o->world);
    count[0]++;
    if (!o->before_at) count[2]++;
    else if (moving) count[1]++;
    if (g_t.links) {
        u64* left = g_t.link_left[reads != 0][g_t.current.link_why + 1];
        left[0]++;
        left[1] += moving != 0;
    }
}

int vk_temporal_object(const GpuChannel* ch, const Surf* zt, VkTemporalObject* out) {
    const u32 frame = g_vk_present + 1;
    const u64 address = g_t.current.address;
    const float *world = g_t.current.world, *was_world = g_t.current.was;
    const int source = g_t.current.source;
    (void)ch;
    if (!address || !was_world || !memcmp(was_world, world, sizeof g_t.current.world) || !g_t.have_before) return 0;
    g_t.object_moved++;
    g_t.skin_rigid += g_t.current.unskinned != 0;
    if (g_t.object_draws >= OBJECT_DRAWS) {
        g_t.object_capped++;
        return 0;
    }
    /* this frame's clip as drawn -> without jitter -> world -> the object -> its world before -> the clip before,
       composed in double (temporal_object_matrices) */
    double jx, jy;
    frame_jitter(frame, &jx, &jy);
    struct {
        float before[16], now[16], viewport[4], depth[4], size[4];
    } block;
    if (!temporal_object_matrices(g_t.inverse, world, was_world, g_t.before, jx, jy, block.now, block.before)) {
        g_t.object_odd++;
        return 0;
    }
    block.viewport[0] = g_t.viewport.x;
    block.viewport[1] = g_t.viewport.y;
    block.viewport[2] = g_t.viewport.width;
    block.viewport[3] = g_t.viewport.height;
    block.depth[0] = g_t.depth_translate;
    block.depth[1] = g_t.depth_scale;
    block.depth[2] = block.depth[3] = 0;
    block.size[0] = (float)zt->sw;
    block.size[1] = (float)zt->sh;
    /* in the probe the draw's number takes the place of the depth before, which only the resolve reads */
    const u32 number = drawn_note(address, 0, 0, source, world, was_world, zt, frame);
    block.size[2] = g_t.mode == MODE_PROBE ? (float)number : 0.0f;
    block.size[3] = 0;
    if (!(block.depth[1] != 0.0f) || !(block.viewport[2] != 0.0f) || !(block.viewport[3] != 0.0f)) return 0;
    if (!g_t.object_fs) {
        VkShaderModuleCreateInfo mi = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                       .codeSize = temporal_object_fs_spv_size,
                                       .pCode = temporal_object_fs_spv};
        if (!VK_OK(vkCreateShaderModule(g_vk.dev, &mi, NULL, &g_t.object_fs))) {
            g_t.object_fs = VK_NULL_HANDLE;
            vk_temporal_object_failed("the object pass's program");
            return 0;
        }
    }
    if (!image_ensure(&g_t.object, OBJECT_FORMAT, zt->sw, zt->sh,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "object vector")) {
        vk_temporal_object_failed("the object vectors' target");
        return 0;
    }
    u8* bytes = vk_stage(sizeof block, vk_stage_align(), &out->block, &out->offset);
    if (!bytes) {
        vk_temporal_object_failed("room for an object draw's block");
        return 0;
    }
    memcpy(bytes, &block, sizeof block);
    out->size = sizeof block;
    out->fs = g_t.object_fs;
    out->format = OBJECT_FORMAT;
    return 1;
}

/* Skinned characters. The game skins a character with transform feedback into a buffer of positions in the
   object's own space, at the same address every frame. After each such draw the positions are copied into a buffer
   of ours, two per address in turn, so the frame after has both this frame's and last frame's. */
typedef struct {
    u64 address;
    u32 capacity, size[2], at[2]; /* at: the frame each of the two copies was made in */
    TemporalStreamCopy copy[2];   /* whose each copy is (temporal_math.h) */
    u32 generation;               /* of the address: every capture into it takes a new one */
    VkBuffer buffer[2];
    VmaAllocation memory[2];
} Skin;
static Skin g_skins[SKINS];

/* The slot of an address among its eight candidates (temporal_slot_pick): a slot's age is its newest capture's,
   copied or not, so one a draw of this frame relied on is never given away. NULL: the address has no record. */
static Skin* skin_slot(u64 address, u32 frame, int make) {
    unsigned long long addresses[8];
    unsigned newest[8];
    Skin* slots[8];
    for (u32 i = 0; i < 8; i++) {
        slots[i] = &g_skins[((address >> 2) * 0x9E3779B1u + i) % SKINS];
        addresses[i] = slots[i]->address;
        newest[i] = temporal_stream_newest(slots[i]->copy);
    }
    const int pick = temporal_slot_pick(addresses, newest, 8, address, frame, make);
    if (pick < 0) return NULL;
    Skin* s = slots[pick];
    if (s->address == address) return s;
    for (int k = 0; k < 2; k++)
        if (s->buffer[k]) vk_defer_buffer(s->buffer[k], s->memory[k]);
    memset(s, 0, sizeof *s);
    s->address = address;
    return s;
}

/* After a transform feedback draw, outside a pass: output 0 (size bytes at address, in source at offset) is copied
   to this frame's buffer of its address. */
void vk_temporal_skin_keep(VkCommandBuffer cb, u64 address, u32 size, VkBuffer source, u64 offset) {
    const u32 frame = g_vk_present + 1;
    if (!g_t.objects || !address) return;
    Skin* s = skin_slot(address, frame, 1);
    if (!s) { /* no slot to be had: the address has no record, so nothing can take its content for known */
        g_t.skin_unkept++;
        return;
    }
    const int turn = (int)(frame & 1);
    if (s->copy[turn].at == frame) g_t.skin_twice++; /* a second capture to this address in one frame */
    /* The capture is counted before anything below can fail: from here the copy of this turn is not known to hold
       this capture until its bytes are copied. */
    temporal_stream_captured(&s->copy[turn], &s->generation, frame);
    if (!size || size > SKIN_BYTES || !source) {
        g_t.skin_uncopied++;
        return;
    }
    if (size > s->capacity) {
        VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                  .size = size,
                                  .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        VmaAllocationCreateInfo bai = {.usage = VMA_MEMORY_USAGE_AUTO};
        /* both new buffers first: when one cannot be made the slot keeps what it had */
        VkBuffer made[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
        VmaAllocation made_memory[2] = {NULL, NULL};
        for (int k = 0; k < 2; k++)
            if (!VK_OK(vmaCreateBuffer(g_vk.vma, &bci, &bai, &made[k], &made_memory[k], NULL)))
                made[k] = VK_NULL_HANDLE;
        if (!made[0] || !made[1]) {
            for (int k = 0; k < 2; k++)
                if (made[k]) vmaDestroyBuffer(g_vk.vma, made[k], made_memory[k]);
            g_t.skin_uncopied++;
            /* memory that could not be had is not a capture too large or without a slot: under the rectangle's
               gate it ends the object vectors (the capture stays counted, the slot keeps what it had) */
            vk_temporal_object_failed("memory for a skinned character's positions");
            return;
        }
        for (int k = 0; k < 2; k++) {
            if (s->buffer[k]) vk_defer_buffer(s->buffer[k], s->memory[k]);
            s->buffer[k] = made[k];
            s->memory[k] = made_memory[k];
            s->at[k] = 0;
            temporal_stream_poison(&s->copy[k]); /* the bytes go with the buffer, the counts stay */
        }
        s->capacity = size;
    }
    /* the capture's writes before the copy reads them, the copy's before a draw reads the vertices */
    VkMemoryBarrier2 before = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                               .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT,
                               .srcAccessMask = VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,
                               .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                               .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT};
    VkMemoryBarrier2 after = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                              .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                              .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                              .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
                              .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT};
    VkDependencyInfo di = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &before};
    const VkBufferCopy region = {.srcOffset = offset, .dstOffset = 0, .size = size};
    rec_CmdPipelineBarrier2(cb, &di);
    rec_CmdCopyBuffer(cb, source, s->buffer[turn], 1, &region);
    di.pMemoryBarriers = &after;
    rec_CmdPipelineBarrier2(cb, &di);
    s->size[turn] = size;
    s->at[turn] = frame;
    temporal_stream_copied(&s->copy[turn]);
    g_t.skin_kept++;
}

/* A G-buffer draw whose position stream starts offset bytes into the transform feedback output at address: 1 with
   what its vector draw needs when last frame's copy of that output is there. The draw is then made with our vertex
   and fragment programs: the position now from the game's binding, the position before from out->before. */
int vk_temporal_skin(const GpuChannel* ch, Surf* zt, u64 address, u64 offset, u64 last_vertex,
                     VkTemporalSkin* out) {
    const u32 frame = g_vk_present + 1;
    const u64 block_at = g_t.current.address;
    const float *world = g_t.current.world, *was = g_t.current.was;
    const int source = g_t.current.source;
    (void)ch;
    /* the copy of a frame ago is the same character's only as far as its block's pair is known */
    if (!block_at || !was || !g_t.have_before) return 0;
    Skin* s = skin_slot(address, frame, 0);
    const int last = (int)((frame - 1) & 1);
    /* this draw reads this frame's capture: the copy is tagged as its own for the frame after */
    if (s && s->capacity)
        temporal_stream_used(&s->copy[!last], frame, g_t.current.pair, g_t.current.pair_other,
                             g_t.current.signature);
    if (!s || !s->capacity || s->at[last] + 1 != frame || offset >= s->size[last]) {
        g_t.skin_first++;
        return 0;
    }
    /* another mesh may have taken the address: this frame's capture must be there and as long as last frame's */
    if (s->at[!last] != frame || s->size[!last] != s->size[last]) {
        g_t.skin_changed++;
        return 0;
    }
    /* last frame's copy must be this draw's own, and the address captured once in each frame */
    {
        const int own =
            temporal_stream_before(&s->copy[!last], &s->copy[last], frame, g_t.current.pair, g_t.current.pair_other,
                                   g_t.current.signature);
        if (own <= 0) {
            if (own < 0) g_t.skin_shared++;
            else g_t.skin_unowned++;
            return 0;
        }
    }
    /* every vertex the draw reads must be in last frame's copy, 12 bytes each from offset on */
    if (last_vertex == ~0ull || (last_vertex + 1) * 12 > (u64)s->size[last] - offset) {
        g_t.skin_short++;
        return 0;
    }
    if (g_t.object_draws >= OBJECT_DRAWS) {
        g_t.object_capped++;
        return 0;
    }
    double jx, jy;
    frame_jitter(frame, &jx, &jy);
    struct {
        float drawn[16], now[16], before[16], viewport[4], depth[4], size[4];
    } block;
    temporal_skin_matrices(g_t.now, world, was, g_t.before, jx, jy, block.drawn, block.now, block.before);
    block.viewport[0] = g_t.viewport.x;
    block.viewport[1] = g_t.viewport.y;
    block.viewport[2] = g_t.viewport.width;
    block.viewport[3] = g_t.viewport.height;
    block.depth[0] = g_t.depth_translate;
    block.depth[1] = g_t.depth_scale;
    block.depth[2] = g_t.depth_zero_to_one ? 0.0f : 1.0f;
    /* near misses of the depth tolerance are marked for the numbers in the probe only: a mark takes the place of
       a vector another draw may have left on that pixel */
    block.depth[3] = g_t.mode == MODE_PROBE ? 1.0f : 0.0f;
    block.size[0] = (float)zt->sw;
    block.size[1] = (float)zt->sh;
    const u32 number = drawn_note(block_at, address, 1, source, world, was, zt, frame);
    g_t.drawn[number <= OBJECT_DRAWS ? number : 0].stream = &s->copy[!last];
    block.size[2] = g_t.mode == MODE_PROBE ? (float)number : 0.0f;
    block.size[3] = 0;
    if (!(block.viewport[2] != 0.0f) || !(block.viewport[3] != 0.0f)) return 0;
    if (!layouts_make()) {
        vk_temporal_object_failed("the passes' layouts");
        return 0;
    }
    if (!g_t.skin_vs) g_t.skin_vs = vk_module_spv(temporal_skin_vs_spv, (size_t)(temporal_skin_vs_spv_size / 4));
    if (!g_t.skin_fs) g_t.skin_fs = vk_module_spv(temporal_skin_fs_spv, (size_t)(temporal_skin_fs_spv_size / 4));
    if (!g_t.skin_vs || !g_t.skin_fs) {
        vk_temporal_object_failed("the skinned pass's programs");
        return 0;
    }
    if (!image_ensure(&g_t.object, OBJECT_FORMAT, zt->sw, zt->sh,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "object vector")) {
        vk_temporal_object_failed("the object vectors' target");
        return 0;
    }
    const VkComponentMapping identity = {0};
    out->depth = vk_tex_view(zt, VK_IMAGE_VIEW_TYPE_2D, 0, &identity, 0);
    if (!out->depth) {
        vk_temporal_object_failed("the scene depth's view for a skinned draw");
        return 0;
    }
    u8* bytes = vk_stage(sizeof block, vk_stage_align(), &out->block, &out->offset);
    if (!bytes) {
        vk_temporal_object_failed("room for a skinned draw's block");
        return 0;
    }
    memcpy(bytes, &block, sizeof block);
    out->size = sizeof block;
    out->vs = g_t.skin_vs;
    out->fs = g_t.skin_fs;
    out->format = OBJECT_FORMAT;
    out->sampler = g_t.point;
    out->before = s->buffer[last];
    out->before_offset = offset;
    out->before_size = s->size[last] - offset;
    g_t.skin_drawn++;
    return 1;
}

/* Begins the pass of one object's extra draw: the vector target, cleared at the frame's first, and the scene depth
   as the caller attaches it. The caller ends it. */
int vk_temporal_object_begin(VkCommandBuffer cb, const VkRenderingAttachmentInfo* depth, int stencil) {
    const u32 frame = g_vk_present + 1;
    const int first = g_t.object_at != frame;
    VkRenderingAttachmentInfo at = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                    .imageView = g_t.object.view,
                                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    .loadOp = first ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
                                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                    .clearValue = {.color = {.float32 = {0.0f, 0.0f, -1.0f, 0.0f}}}};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                          .renderArea = {{0, 0}, {g_t.object.w, g_t.object.h}},
                          .layerCount = 1,
                          .colorAttachmentCount = 1,
                          .pColorAttachments = &at,
                          .pDepthAttachment = depth,
                          .pStencilAttachment = stencil ? depth : NULL};
    if (!g_t.object.view) return 0;
    if (first)
        /* after last frame's reads by the motion pass, and after its writes when that pass did not read the
           target (a frame whose object vectors were given up) */
        vk_barrier(cb, g_t.object.img, VK_IMAGE_ASPECT_COLOR_BIT,
                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    else
        /* an earlier extra draw wrote the target in a pass of its own: this one loads and writes after it */
        vk_barrier(cb, g_t.object.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    g_t.object_at = frame;
    g_t.object_draws++;
    g_t.object_drawn++;
    rec_CmdBeginRendering(cb, &ri);
    return 1;
}

void vk_temporal_object_pipeline(void) { g_t.object_pipelines++; }

/* Read-only, for the skinned characters. A transform feedback draw's output is noted, and a G-buffer draw whose
   vertex stream starts inside one of this frame's outputs is counted, the first ones with a line. */
void vk_temporal_feedback(u64 address, u32 size, u32 index) {
    const u32 frame = g_vk_present + 1;
    if (g_t.feedback_frame != frame) {
        g_t.feedback_frame = frame;
        g_t.feedback_n = 0;
    }
    if (g_t.feedback_n < 256) {
        g_t.feedback[g_t.feedback_n].address = address;
        g_t.feedback[g_t.feedback_n].size = size;
        g_t.feedback[g_t.feedback_n++].index = index;
    }
}
int vk_temporal_feedback_find(u64 address, u32* index, u64* offset, u32* size) {
    if (g_t.feedback_frame != g_vk_present + 1) return 0;
    for (u32 i = 0; i < g_t.feedback_n; i++)
        if (address >= g_t.feedback[i].address && address < g_t.feedback[i].address + g_t.feedback[i].size) {
            *index = g_t.feedback[i].index;
            *offset = address - g_t.feedback[i].address;
            *size = g_t.feedback[i].size;
            return 1;
        }
    return 0;
}
int vk_temporal_skinned_line(void) {
    g_t.object_skinned++;
    return g_t.skinned_lines++ < 24;
}

/* ------------------------------------------------------------ SNX_ARCEUS_UPSCALER: a library's upscaler */
/* Experimental, at full size only, and dormant without its switch: nothing below runs unless g_u.asked is 1, which
   only upscaler_start sets. The rules are temporal_math.h's (temporal_upscaler_*). With it on, our own resolve,
   sharpen, history, numbers and views never run, also after it has stopped: a stopped upscaler leaves every
   frame as the game draws it and does not fall back to our resolve. */
static int upscaler_on(void) { return g_u.asked == 1; }
/* this frame is drawn with jitter for the upscaler: decided so at its first draw with the camera's block */
static int upscaler_jittered(u32 frame) {
    return g_u.decided_at == frame && g_u.decision == TEMPORAL_UPSCALER_JITTER;
}

/* Queued for after the frames in flight (vk_defer_call). The recorder's mark is looked at again here, when it
   runs: once a submission did not run as recorded, nothing a submission may use is freed. */
static void upscaler_destroy(void* context) {
    if (vk_rec_submit_failed() || !g_u.api) return;
    g_u.api->destroy(context);
}

/* No context could be made before any was in use: the adapter is not available on this device after all. What
   the start changed is put back and the run goes on as without the switch. */
static void upscaler_unavailable(const char* why) {
    LOG("arceus-upscaler: not available (%s): the run goes on as without the switch\n", why);
    g_t.rect = g_u.was_rect;
    g_t.view = g_u.was_view;
    g_t.rect_extras_off = g_u.was_extras;
    g_u.asked = 0;
}

/* Something came of a frame (temporal_upscaler_after). jittered: it was drawn with jitter, so it counts. Never
   called from a frame's recycle, since turning off queues the live context's destruction. */
static void upscaler_outcome(u32 frame, int jittered, int outcome, const char* what) {
    const int was_off = g_u.health.off;
    /* a frame has one fate: the first reported for it is the one counted, in the line and towards the eight */
    const int first = jittered && temporal_upscaler_tallied(&g_u.tally, frame, outcome);
    if (jittered) g_u.outcome_at = frame;
    if (outcome != TEMPORAL_UPSCALER_EVALUATED && (first || !jittered) &&
        (g_u.unresolved++ < 8 || g_u.unresolved % 600 == 0))
        LOG("arceus-upscaler: frame %u %s (%s), %llu such so far\n", frame,
            !jittered                              ? "had no jitter"
            : outcome == TEMPORAL_UPSCALER_SKIPPED ? "was drawn with jitter and is left as drawn, skipped"
            : outcome == TEMPORAL_UPSCALER_FAILED  ? "was drawn with jitter and is left as drawn, the library failed"
                                                   : "was drawn with jitter and is left as drawn",
            what, (unsigned long long)g_u.unresolved);
    /* a stop latches whenever it comes, also as a frame's second outcome and for a frame without jitter */
    if (!temporal_upscaler_after(&g_u.health, first, outcome) || was_off) return;
    LOG("arceus-upscaler: off for the rest of the session (%s): no frame is drawn with jitter any more, and every "
        "frame is left as the game draws it\n",
        g_u.health.stopped ? what : "eight frames in a row were drawn with jitter and not evaluated");
    if (temporal_upscaler_stop(&g_u.contexts) && g_u.live) vk_defer_call(upscaler_destroy, g_u.live);
    g_u.live = NULL;
}

/* A frame's recycle looks at the context on its way and only notes what it finds: it publishes nothing and
   queues nothing, since queueing there would make a command in a slot that is being recycled. */
void vk_temporal_upscaler_poll(void) {
    u64 done = 0;
    if (g_u.asked != 1 || !g_u.pending || g_u.noted != TEMPORAL_UPSCALER_WAIT) return;
    const int read = vk_submit_completed(&done);
    g_u.noted = temporal_upscaler_poll(read, done, vk_rec_submit_failed(), g_u.pending_number, &g_u.unread);
    if (!read && g_u.unread == 1)
        LOG("arceus-upscaler: the timeline could not be read: a context waits, and %d such reads in a row stop the "
            "upscaler\n", TEMPORAL_UPSCALER_UNREAD);
}

/* Retirement while the upscaler is off, a path of its own: it needs no configuration, since it never publishes.
   The context on its way is released only on proof: a read of the timeline that worked and shows its
   submission reached, with the recorder's mark clear. Once, through the queue for after the frames in flight,
   whose callback reads the mark again. */
static void upscaler_retire(void) {
    const int failed = vk_rec_submit_failed();
    if (!g_u.pending || !temporal_upscaler_retire(g_u.health.off, g_u.noted, failed)) return;
    SnxUpscaler* context = g_u.pending;
    g_u.pending = NULL;
    g_u.noted = TEMPORAL_UPSCALER_WAIT;
    const int fate = temporal_upscaler_completed(&g_u.contexts, &g_u.wanted);
    /* after the stop it can only be stale or released: were it ever published, it is taken back at once */
    if (fate == TEMPORAL_UPSCALER_PUBLISHED || fate == TEMPORAL_UPSCALER_RETIRE)
        temporal_upscaler_stop(&g_u.contexts);
    vk_defer_call(upscaler_destroy, context);
}

/* At every place where the runtime records as usual (the gate's draw, the hook), before anything else of the
   upscaler: the recorder's mark is read, context on its way or not, and a stop the recycle noted is acted on.
   Either is a stop for the session. 1 when the upscaler may go on. When it is off this is also where a
   context on its way is released, once its submission has completed. It publishes nothing. */
static int upscaler_guard(u32 frame) {
    const int failed = vk_rec_submit_failed();
    const int noted = temporal_upscaler_noted(g_u.pending ? g_u.noted : TEMPORAL_UPSCALER_WAIT, failed);
    if (noted == TEMPORAL_UPSCALER_STOP) {
        const char* why = failed ? "a submission did not run as recorded"
                                 : "the timeline could not be read eight times in a row";
        /* A stop is never a release, and neither is a count of failed reads: the context on its way keeps its
           identity and its submission's number, and the recycle goes on looking for its completion. */
        g_u.noted = TEMPORAL_UPSCALER_WAIT;
        g_u.unread = 0;
        /* the poll's STOP is the outcome STOPPED: nothing is published and nothing is freed */
        if (!g_u.health.off) upscaler_outcome(frame, 0, TEMPORAL_UPSCALER_STOPPED, why);
        return 0;
    }
    if (!g_u.health.off) return 1;
    upscaler_retire();
    return 0;
}

/* The one place a completed context is published or let go, and it takes the configuration this frame is
   confirmed to have: at the gate's draw the one decided there, at the hook the one made from the frame's own
   colour target, the projection its draws were given and its whole-target state. That is what the frames want
   now. A context made for another goes unused, and the one in use stays. Without such a key nothing settles. */
static void upscaler_settle(u32 frame, const TemporalUpscalerConfig* confirmed) {
    if (!upscaler_guard(frame)) return;
    g_u.wanted = *confirmed;
    if (!g_u.pending || g_u.noted != TEMPORAL_UPSCALER_COMPLETED) return;
    SnxUpscaler* context = g_u.pending;
    g_u.pending = NULL;
    g_u.noted = TEMPORAL_UPSCALER_WAIT;
    switch (temporal_upscaler_completed(&g_u.contexts, &g_u.wanted)) {
    case TEMPORAL_UPSCALER_RETIRE:
        if (g_u.live) vk_defer_call(upscaler_destroy, g_u.live);
        /* fall through */
    case TEMPORAL_UPSCALER_PUBLISHED:
        g_u.live = context;
        g_u.published++;
        if (g_u.published <= 4)
            LOG("arceus-upscaler: context %u is ready (%dx%d, generation %llu), from frame %u on\n", g_u.published,
                g_u.contexts.live_config.out_w, g_u.contexts.live_config.out_h, g_u.contexts.live_config.generation,
                frame);
        break;
    default: /* stale, or one whose prepare failed: its submission has completed, so it may go */
        vk_defer_call(upscaler_destroy, context);
        break;
    }
}

/* The configuration a frame is evaluated with, and its camera: from the depth target, a description of the
   colour target, and an unjittered projection given by value. 0 when a part of it is not
   known or not one this piece takes: one layer each, the same size, the formats the self-test ran, and the
   game's form of projection. */
/* D3-3: a frame below native the hand-over takes (a listed scale at the origin, bit for bit), while it is on. Off,
   such a frame is not taken by FSR, so the gate gives it no jitter: never a jitter without a resolve */
static int upscaler_below(const TemporalRectState* s) {
    return g_u.below && g_s.asked && !g_s.latch.off &&
           temporal_spatial_below(s->target_w, s->target_h, s->rect.x, s->rect.y, s->rect.w, s->rect.h, s->fx,
                                  s->fy) >= 0;
}
/* the frames FSR takes: the whole target, or below native on the hand-over */
static int upscaler_admitted(const TemporalRectState* s) { return temporal_upscaler_whole(s) || upscaler_below(s); }
/* render: the rectangle of a frame below native, NULL for the whole target */
static int upscaler_key(const Surf* depth, const TemporalUpscalerColour* colour, const float m[16],
                        TemporalUpscalerConfig* config, TemporalProjection* projection, u32* flags,
                        const TemporalRect* render) {
    /* one layer each, at the image's first layer and not a member of an array: the depth target's are read here,
       at the gate's draw too, the colour target's are in its description */
    if (!depth || depth->layers != 1 || depth->group || depth->base_layer || colour->layers != 1) return 0;
    if ((int)depth->sw != colour->w || (int)depth->sh != colour->h) return 0;
    if (colour->format != (unsigned)VK_FORMAT_B10G11R11_UFLOAT_PACK32 || depth->f.vk != VK_FORMAT_D24_UNORM_S8_UINT)
        return 0;
    if (temporal_projection_camera(m, projection) != TEMPORAL_PROJECTION_OK) return 0;
    *flags = SNX_UPSCALER_COLOUR_LINEAR_HDR | SNX_UPSCALER_DEPTH_REVERSED | SNX_UPSCALER_VECTORS_UNJITTERED |
             SNX_UPSCALER_EXPOSURE_AUTOMATIC | (projection->far_infinite ? SNX_UPSCALER_FAR_PLANE_INFINITE : 0);
    const TemporalUpscalerMaking making = {colour->format, (unsigned)depth->f.vk, (unsigned)upscaler_motion_format(),
                                           (unsigned)UPSCALED_FORMAT, colour->layers,        depth->layers,
                                           *flags};
    *config = (TemporalUpscalerConfig){colour->w, colour->h, render ? render->w : colour->w,
                                       render ? render->h : colour->h, colour->w, colour->h,
                                       temporal_upscaler_generation(&g_u.making, &g_u.generation, &making)};
    return 1;
}

/* At the gate's draw, after the gate's own decision and before any jitter is staged: 1 when the frame is drawn
   with jitter (temporal_upscaler_decide). The colour target is the one thing taken from the frame before, which
   must have reached its hook. The depth target and the projection are this draw's. */
static int upscaler_decide(u32 frame, int accepted) {
    const TemporalRectGate* rg = &g_t.rect_gate;
    TemporalUpscalerConfig config = {0};
    TemporalProjection projection;
    u32 flags = 0;
    /* the frame decided before was drawn with jitter and its hook was never reached: a skipped evaluate */
    if (g_u.decided_at && g_u.decided_at != frame && g_u.decision == TEMPORAL_UPSCALER_JITTER &&
        g_u.outcome_at != g_u.decided_at)
        upscaler_outcome(g_u.decided_at, 1, TEMPORAL_UPSCALER_SKIPPED, "our passes were not reached in it");
    /* the frame before is final now: a window of the counts is closed here, before this frame is counted */
    if (temporal_upscaler_due(&g_u.tally)) {
        const TemporalUpscalerTally* t = &g_u.tally;
        LOG("arceus-upscaler: %u frames: %u evaluated, %u drawn with jitter and skipped, %u failed, %u stopped. "
            "Not jittered: %u the gate, %u not the whole target, %u a part of the configuration not known, %u no "
            "ready context, %u off\n",
            t->frames, t->evaluated, t->skipped, t->failed, t->stopped,
            t->not_jittered[TEMPORAL_UPSCALER_NOT_GATE], t->not_jittered[TEMPORAL_UPSCALER_NOT_WHOLE],
            t->not_jittered[TEMPORAL_UPSCALER_NOT_KNOWN], t->not_jittered[TEMPORAL_UPSCALER_NOT_READY],
            t->not_jittered[TEMPORAL_UPSCALER_NOT_ON]);
        temporal_upscaler_closed(&g_u.tally);
    }
    /* the gate accepted the frame's geometry as the candidate's, whose fraction was confirmed a frame ago */
    TemporalRectState now = rg->now;
    now.fx = rg->candidate.fx;
    now.fy = rg->candidate.fy;
    const int whole = accepted && upscaler_admitted(&now); /* D3-3: below native too, on the hand-over */
    /* the projection as the block holds it at this draw, kept by value: what the draws are given is checked
       against it at the hook */
    memset(g_u.decided_projection, 0, sizeof g_u.decided_projection);
    memset(g_u.decided_view, 0, sizeof g_u.decided_view);
    if (accepted && g_u.block) {
        block_read(g_u.block + 0x80, g_u.decided_projection);
        block_read(g_u.block, g_u.decided_view);
    }
    const int known = accepted && g_u.block && temporal_upscaler_predicted(&g_u.colour, frame) &&
                      upscaler_key(g_t.draw_zt, &g_u.colour, g_u.decided_projection, &config, &projection, &flags,
                                   upscaler_below(&now) ? &now.rect : NULL);
    g_u.decided_at = frame;
    g_u.decided = config;
    g_u.known = known;
    /* A completed context is held against this frame's configuration, read above, and with an unknown one
       nothing is published: only the mark is read. */
    if (known) upscaler_settle(frame, &config);
    else upscaler_guard(frame);
    g_u.decision = temporal_upscaler_decide(&g_u.health, accepted, whole, known, &g_u.contexts, &config);
    temporal_upscaler_decided(&g_u.tally, g_u.decision);
    return g_u.decision == TEMPORAL_UPSCALER_JITTER;
}

/* What a call of the library is handed through vk_rec_direct. frame is NULL for a prepare. */
typedef struct {
    SnxUpscaler* context;
    const SnxUpscalerFrame* frame;
    Surf *scene, *depth;
    u32 number; /* the frame's, for the log */
} UpscalerCall;
/* What must be recorded before the library: the end of our rendering, and for an evaluate the images as the
   library reads and writes them, in a compute shader. The scene's targets go through the renderer's own
   tracking, so that what comes after them is ordered behind the library's reads. */
static void upscaler_before(VkCommandBuffer cb, void* user) {
    const UpscalerCall* c = user;
    vk_render_end();
    if (!c->frame) return;
    vk_surf_use(cb, c->scene, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    vk_surf_use(cb, c->depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    vk_barrier(cb, upscaler_motion()->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    /* The private output: what it held is not kept. Its last use is whatever a library recorded in an earlier
       frame, also one whose evaluate failed, so the barrier's source is every command, reading and writing. */
    vk_barrier(cb, g_u.output.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, UPSCALER_WROTE,
               VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
               VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
}
/* After a call the library was entered in, for a target of the scene: an explicit barrier from "written by any
   command" in the layout it was handed in and must be back in, and the renderer's tracker is told that state,
   which it knows as a write. The target's next use, whatever it is, is then ordered behind the library. */
static void upscaler_took(VkCommandBuffer cb, Surf* s) {
    vk_barrier_layers(cb, s->img, vk_aspect(&s->f), s->base_layer, VK_REMAINING_ARRAY_LAYERS,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, UPSCALER_WROTE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, UPSCALER_ANY_ACCESS,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    s->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    s->stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    s->access = UPSCALER_ANY_ACCESS;
}
/* The library's answers go through as they are (vk_rec_direct): UNKNOWN is not reduced to a failure (DL2 rev 2) */
static int upscaler_prepare(VkCommandBuffer cb, void* user) {
    const UpscalerCall* c = user;
    return g_u.api->prepare(c->context, cb);
}
/* A library answered UNKNOWN: the frame's command buffer holds the game's earlier work of the frame and what the
   library left, in a state not known. Nothing more is recorded into it, it is never submitted (vk_rec_direct's
   latch), nothing is destroyed or freed, and the process ends with this one line (DL2 revision 2, O1). */
static void upscaler_stop(u32 frame, const char* call) {
    LOG("arceus-upscaler: FATAL: the adapter %s answered UNKNOWN to its %s at frame %u: the frame's command buffer is "
        "in a state not known and is not submitted. Nothing is released, and the run ends here, as an experiment "
        "that met a state the library leaves undefined\n", g_u.api && g_u.api->name ? g_u.api->name : "?", call,
        frame);
    vk_rec_end_run(3);
}
static int upscaler_evaluate(VkCommandBuffer cb, void* user) {
    const UpscalerCall* c = user;
    SnxUpscalerFrame frame = *c->frame;
    frame.cb = cb;
    /* For the log alone, here and nowhere earlier: the helper has not turned back, and the library is entered
       next with exactly these four images. Their handles as the validation layer prints an image's, so that a
       message of the layer about an image can be held against them. */
    const unsigned long long images[4] = {(unsigned long long)(uintptr_t)frame.colour.image,
                                          (unsigned long long)(uintptr_t)frame.depth.image,
                                          (unsigned long long)(uintptr_t)frame.motion.image,
                                          (unsigned long long)(uintptr_t)frame.output.image};
    const int say = temporal_upscaler_handed(&g_u.handed, images, g_u.published);
    if (say != TEMPORAL_UPSCALER_HANDED_QUIET)
        LOG("arceus-upscaler: handed images at frame %u: colour 0x%llx, depth 0x%llx, vectors 0x%llx, output "
            "0x%llx%s\n", c->number, images[0], images[1], images[2], images[3],
            say == TEMPORAL_UPSCALER_HANDED_LAST ? ". The last such line: later changes are not said" : "");
    return g_u.api->evaluate(c->context, &frame);
}

/* The hook of a frame that was not drawn with jitter. Nothing of ours runs in it but this: when the frame was
   left without jitter only because no context is ready, and what the hook sees is what the frame was decided
   with, a context is created and its prepare recorded. It is ready a frame later at the earliest. */
static void upscaler_warm(u32 frame, Surf* scene, Surf* depth, int steady) {
    const TemporalUpscalerColour colour = temporal_upscaler_colour(
        frame, (unsigned)scene->f.vk, scene->layers, scene->base_layer, scene->group != NULL, (int)scene->sw,
        (int)scene->sh);
    TemporalUpscalerConfig seen;
    TemporalProjection projection;
    u32 flags = 0;
    if (g_u.health.off || g_u.decided_at != frame || !steady) return;
    /* This frame is confirmed first: the whole target, the projection its draws took (the decision's, bit for
       bit, and the same view), and the key from them and the frame's own colour target. Only then is a
       completed context settled, against that key. */
    if (!upscaler_admitted(&g_t.rect_gate.now)) return;
    if (!temporal_upscaler_used_same(&g_u.used, frame, g_u.decided_projection) ||
        !temporal_upscaler_used_same(&g_u.used_view, frame, g_u.decided_view))
        return;
    const TemporalRect* render = upscaler_below(&g_t.rect_gate.now) ? &g_t.rect_gate.now.rect : NULL;
    if (!upscaler_key(depth, &colour, g_u.used.m, &seen, &projection, &flags, render)) return;
    upscaler_settle(frame, &seen);
    if (g_u.health.off || g_u.decision != TEMPORAL_UPSCALER_NOT_READY) return;
    if (!temporal_upscaler_config_same(&g_u.decided, &seen)) return;
    if (!temporal_upscaler_must_create(&g_u.contexts, &seen)) return;
    const SnxUpscalerCreate make = {.sizes = {seen.render_w, seen.render_h, scene->sw, scene->sh, scene->sw, scene->sh},
                                    .instance = g_vk.instance,
                                    .physical_device = g_vk.phys,
                                    .device = g_vk.dev,
                                    .instance_lookup = vkGetInstanceProcAddr,
                                    .device_lookup = vkGetDeviceProcAddr,
                                    .queue = g_vk.gfx,
                                    .queue_family = g_vk.gfx_family,
                                    .flags = flags,
                                    .colour_format = scene->f.vk,
                                    .depth_format = depth->f.vk,
                                    .motion_format = upscaler_motion_format(),
                                    .output_format = UPSCALED_FORMAT,
                                    .folder = "."};
    /* what the images handed over are made with: sampled inputs, a sampled storage image for the output */
    const VkImageUsageFlags inputs = VK_IMAGE_USAGE_SAMPLED_BIT;
    const VkImageUsageFlags output =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    SnxUpscalerTraits traits = {0};
    SnxUpscaler* made = NULL;
    const int created = g_u.api->create(&make, &traits, &made) == SNX_UPSCALER_OK && made;
    const VkImageUsageFlags reads = traits.colour_usage | traits.depth_usage | traits.motion_usage;
    /* the output with the runtime's uses and the traits', its format asked for them before the context is taken */
    VkImageFormatProperties output_has;
    const VkImageUsageFlags output_usage = UPSCALED_USAGE | traits.output_usage;
    const int output_ok =
        created && !(traits.output_usage & ~output) &&
        VK_OK(vkGetPhysicalDeviceImageFormatProperties(g_vk.phys, UPSCALED_FORMAT, VK_IMAGE_TYPE_2D,
                                                       VK_IMAGE_TILING_OPTIMAL, output_usage, 0, &output_has));
    if (output_ok) {
        g_u.output_usage = output_usage;
        g_u.output_max[0] = output_has.maxExtent.width;
        g_u.output_max[1] = output_has.maxExtent.height;
    }
    if (!created || traits.submits_itself || (reads & ~inputs) || !output_ok) {
        const char* why = !created              ? "a context could not be made on the game's device"
                          : traits.submits_itself ? "the adapter submits work on its own"
                                                  : "the adapter needs image usages the game's images do not have";
        /* nothing was recorded for it, so it may go at once */
        if (created) g_u.api->destroy(made);
        if (!g_u.published) upscaler_unavailable(why);
        else upscaler_outcome(frame, 0, TEMPORAL_UPSCALER_STOPPED, why);
        return;
    }
    UpscalerCall call = {made, NULL, NULL, NULL};
    const int recorded = vk_rec_direct(vk_cmd(), upscaler_before, upscaler_prepare, &call);
    if (recorded == VK_REC_DIRECT_UNKNOWN) upscaler_stop(frame, "prepare"); /* before anything else */
    /* Whatever the answer, work may be recorded for the context: it is kept until the submission that work is
       in has completed, and only then freed or published (vk_temporal_upscaler_poll, upscaler_settle). */
    g_u.pending = made;
    g_u.pending_number = vk_submit_number();
    g_u.noted = TEMPORAL_UPSCALER_WAIT;
    g_u.wanted = seen;
    temporal_upscaler_created(&g_u.contexts, &seen, recorded == VK_REC_DIRECT_OK);
    if (++g_u.contexts_made <= 4)
        LOG("arceus-upscaler: context made at frame %u for %ux%u, flags 0x%x, near %.9g, far %.9g: its prepare %s\n",
            frame, scene->sw, scene->sh, flags, projection.near_plane, projection.far_plane,
            recorded == VK_REC_DIRECT_OK ? "is recorded" : "was not recorded");
    if (recorded != VK_REC_DIRECT_OK)
        upscaler_outcome(frame, 0, TEMPORAL_UPSCALER_STOPPED,
                         recorded == VK_REC_DIRECT_REFUSED  ? "a context's prepare failed"
                         : recorded == VK_REC_DIRECT_FAILED ? "a submission did not run as recorded"
                                                            : "the frame's command buffer could not be used");
}

/* The hook of a frame that was drawn with jitter, after the vector pass: the scene colour, the depth and the
   motion image are handed to the library, which writes its private image, and our write-back puts that picture
   into the scene colour before its first reader. Whatever stops short of that leaves the frame as drawn. */
static void upscaler_resolve(u32 frame, VkCommandBuffer cb, Surf* scene, Surf* depth, VkImageView depth_view,
                             const Constants* pc, const TemporalRect* area, const TemporalViewport* view) {
    const TemporalRectGate* rg = &g_t.rect_gate;
    const TemporalUpscalerColour colour = temporal_upscaler_colour(
        frame, (unsigned)scene->f.vk, scene->layers, scene->base_layer, scene->group != NULL, (int)scene->sw,
        (int)scene->sh);
    const VkComponentMapping identity = {0};
    TemporalUpscalerConfig seen;
    TemporalProjection projection;
    TemporalUpscalerFrame numbers;
    float camera[4];
    u32 flags = 0;
    int fallback = 0;
    /* What the hook sees must be what the frame was decided with: the frame's own colour target in place of the
       predicted one, the depth target, the projection its draws were given (nothing is read from guest memory
       here), the whole target, eight offsets, and a depth that is the projection's own (a viewport depth range
       of 0 to 1). */
    const char* skip = NULL;
    const int below = area && upscaler_below(&rg->now);
    if (!area || !upscaler_admitted(&rg->now))
        skip = "the scene is not the whole target nor below native on the hand-over";
    else if (!temporal_upscaler_used_same(&g_u.used, frame, g_u.decided_projection) ||
             !temporal_upscaler_used_same(&g_u.used_view, frame, g_u.decided_view))
        skip = "a draw took a camera block that was not the decided projection with jitter";
    else if (!upscaler_key(depth, &colour, g_u.used.m, &seen, &projection, &flags, below ? &rg->now.rect : NULL))
        skip = "a part of the configuration is not known at the hook";
    /* the frame is confirmed: a context that completed since the decision is settled against this key, and
       only then is the ready one asked for */
    if (!skip) {
        upscaler_settle(frame, &seen);
        if (g_u.health.off) skip = "the upscaler stopped";
        else if (!temporal_upscaler_hook(g_u.decision, &g_u.decided, &seen, &g_u.contexts) || !g_u.live)
            skip = "the configuration is not the one the frame was decided with";
        else if (g_t.offsets != temporal_upscaler_phases(&seen)) /* the same for every adapter, DLSS's too */
            skip = "the jitter's length is not the phase count of the context's render and output sizes";
        else if (!g_t.depth_zero_to_one || g_t.depth_translate != 0.0f || g_t.depth_scale != 1.0f)
            skip = "the stored depth is not the projection's own";
    }
    if (skip) {
        upscaler_outcome(frame, 1, TEMPORAL_UPSCALER_SKIPPED, skip);
        return;
    }
    /* a cut, as our own resolve finds it: the screen's middle at a depth of ten moved by more than a quarter */
    const float* m = pc->reproject;
    const float w = 0.01f * m[11] + m[15], x = 0.01f * m[8] + m[12], y = 0.01f * m[9] + m[13];
    const int cut = !(w > 0.0f) || fabsf(x) > 0.5f * w || fabsf(y) > 0.5f * w;
    const TemporalRectStamp stamp = {scene->gva,     seen.generation, (int)scene->sw, (int)scene->sh,
                                     (int)scene->sw, (int)scene->sh,  rg->now.rect,   rg->now.fx,
                                     rg->now.fy};
    const double jitter[2] = {(double)g_t.jitter_ndc[0], (double)g_t.jitter_ndc[1]};
    const u64 now_ns = plat_ns();
    const double seconds =
        temporal_upscaler_seconds(now_ns, g_u.evaluated_ns, g_u.evaluated_at + 1 == frame, g_u.nominal, &fallback);
    if (!temporal_upscaler_frame(rg, frame, &stamp, view, jitter, cut, &numbers) ||
        !temporal_upscaler_camera(projection.near_plane, projection.far_plane, projection.fov_y, seconds,
                                  projection.far_infinite, camera)) {
        upscaler_outcome(frame, 1, TEMPORAL_UPSCALER_SKIPPED, "a number for the library did not pass its check");
        return;
    }
    /* D3-3: below native, the hand-over's larger texture is made before the library is called. One that cannot be
       made turns the hand-over off at once (a make failure), and the frame is not evaluated. */
    if (below && !spatial_below_ready(frame, scene, rg)) {
        upscaler_outcome(frame, 1, TEMPORAL_UPSCALER_SKIPPED, "the hand-over's larger texture could not be made");
        return;
    }
    /* Everything of our own that the hand-over needs is made before the library is called: the private image,
       the write-back's pipeline and set, the scene colour's view. One that cannot be made stops the upscaler. */
    if (!g_u.write_pipe || g_u.write_format != scene->f.vk) {
        g_u.write_pipe =
            pipeline_make(scene->f.vk, temporal_upscaled_fs_spv, temporal_upscaled_fs_spv_size, "write-back");
        g_u.write_format = scene->f.vk;
    }
    const VkImageView scene_view = vk_tex_view(scene, VK_IMAGE_VIEW_TYPE_2D, 0, &identity, 0);
    /* A scene larger than the format's limit for these uses: the driver is not asked for such an image. */
    if (!temporal_upscaler_output_fits(scene->sw, scene->sh, g_u.output_max[0], g_u.output_max[1])) {
        LOG("arceus-upscaler: the private output cannot be made: the scene is %ux%u, and the device's limit for "
            "an image of its format and uses is %ux%u\n", scene->sw, scene->sh, g_u.output_max[0],
            g_u.output_max[1]);
        upscaler_outcome(frame, 1, TEMPORAL_UPSCALER_STOPPED, "the write-back's image, pipeline, set or view");
        return;
    }
    /* made anew when its uses changed (image_ensure keeps one of the same format and size) */
    if (g_u.output.img && g_u.output_made_usage != g_u.output_usage) {
        vk_defer_image(g_u.output.img, g_u.output.mem, g_u.output.view, VK_NULL_HANDLE);
        g_u.output = (Image){0};
    }
    const VkImageUsageFlags output_usage = g_u.output_usage ? g_u.output_usage : UPSCALED_USAGE;
    const int imaged = image_ensure(&g_u.output, UPSCALED_FORMAT, scene->sw, scene->sh, output_usage,
                                    "upscaler's output");
    if (imaged) g_u.output_made_usage = output_usage;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (imaged && g_u.write_pipe && scene_view) {
        const VkImageView views[SETS_BINDINGS] = {g_u.output.view, g_u.output.view, g_u.output.view, g_u.output.view,
                                                  g_u.output.view, g_u.output.view, g_u.output.view};
        const VkSampler samplers[SETS_BINDINGS] = {g_t.point, g_t.point, g_t.point, g_t.point,
                                                   g_t.point, g_t.point, g_t.point};
        set = set_make(views, samplers);
    }
    if (!set) {
        upscaler_outcome(frame, 1, TEMPORAL_UPSCALER_STOPPED, "the write-back's image, pipeline, set or view");
        return;
    }
    /* DL2 point 3: the two-channel copy, after the vectors and before the evaluate. Its format's uses and extents
       are asked before it is made; the motion image itself is not changed. */
    if (g_u.two_channel) {
        VkImageFormatProperties has;
        const VkImageUsageFlags uses = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        const int fits = VK_OK(vkGetPhysicalDeviceImageFormatProperties(g_vk.phys, MOTION2_FORMAT, VK_IMAGE_TYPE_2D,
                                                                        VK_IMAGE_TILING_OPTIMAL, uses, 0, &has)) &&
                         g_t.motion.w <= has.maxExtent.width && g_t.motion.h <= has.maxExtent.height &&
                         g_t.motion.w <= g_vk.props.limits.maxFramebufferWidth &&
                         g_t.motion.h <= g_vk.props.limits.maxFramebufferHeight;
        if (fits && !g_u.motion2_pipe)
            g_u.motion2_pipe = pipeline_make(MOTION2_FORMAT, temporal_motion2_fs_spv, temporal_motion2_fs_spv_size,
                                             "two-channel vectors");
        VkDescriptorSet copy = VK_NULL_HANDLE;
        if (fits && g_u.motion2_pipe &&
            image_ensure(&g_u.motion2, MOTION2_FORMAT, g_t.motion.w, g_t.motion.h, uses, "two-channel vectors")) {
            VkImageView views[SETS_BINDINGS];
            VkSampler samplers[SETS_BINDINGS];
            for (int i = 0; i < SETS_BINDINGS; i++) views[i] = g_t.motion.view, samplers[i] = g_t.point;
            copy = set_make(views, samplers);
        }
        if (!copy) {
            upscaler_outcome(frame, 1, TEMPORAL_UPSCALER_STOPPED, "the two-channel vectors' image, pass or set");
            return;
        }
        /* its last use was the library's, a frame ago or earlier: every command, reading and writing */
        vk_barrier(cb, g_u.motion2.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, UPSCALER_WROTE,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        fullscreen(cb, g_u.motion2.view, g_u.motion2.w, g_u.motion2.h, g_u.motion2_pipe, copy, pc, NULL);
        vk_barrier(cb, g_u.motion2.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    const Image* motion = upscaler_motion();
    const SnxUpscalerFrame handed = {
        .colour = {scene->img, scene_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, scene->f.vk, scene->sw, scene->sh},
        .depth = {depth->img, depth_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, depth->f.vk, depth->sw, depth->sh},
        .motion = {motion->img, motion->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, motion->format, motion->w,
                   motion->h},
        .output = {g_u.output.img, g_u.output.view, VK_IMAGE_LAYOUT_GENERAL, g_u.output.format, g_u.output.w,
                   g_u.output.h},
        .render_x = numbers.render.x,
        .render_y = numbers.render.y,
        .render_w = (u32)numbers.render.w,
        .render_h = (u32)numbers.render.h,
        .jitter = {(float)numbers.jitter[0], (float)numbers.jitter[1]},
        .vector_scale = {(float)numbers.vector_scale[0], (float)numbers.vector_scale[1]},
        /* the reset's causes add up: the gate's history and the cut, the time's fallback, a new context */
        .reset = (u32)temporal_upscaler_resets(numbers.reset, fallback, g_u.published, &g_u.evaluated_with),
        .frame_seconds = camera[3],
        .near_plane = camera[0],
        .far_plane = camera[1],
        .fov_y = camera[2]};
    UpscalerCall call = {g_u.live, &handed, scene, depth, frame};
    const int recorded = vk_rec_direct(cb, upscaler_before, upscaler_evaluate, &call);
    /* before any barrier back, write-back or draw: nothing more goes into this command buffer */
    if (recorded == VK_REC_DIRECT_UNKNOWN) upscaler_stop(frame, "evaluate");
    /* On every path out of a call the library was entered in, each of the four images it was handed gets a
       barrier of its own from "written by any command", so that whatever the library wrote is available to
       what comes next: the scene colour, the scene depth, the motion image and the private output. */
    if (recorded == VK_REC_DIRECT_OK || recorded == VK_REC_DIRECT_REFUSED) {
        upscaler_took(cb, scene);
        upscaler_took(cb, depth);
        vk_barrier(cb, motion->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, UPSCALER_WROTE,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, UPSCALER_ANY_ACCESS,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vk_barrier(cb, g_u.output.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, UPSCALER_WROTE,
                   VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    if (recorded != VK_REC_DIRECT_OK) {
        /* no successful output: nothing is written back and the frame stays as drawn. What a library that
           refused may have recorded all the same is covered by the barriers above. */
        /* D3-3: below native a failed evaluation is a frame of class N, eligible and not published, counted */
        if (below && recorded == VK_REC_DIRECT_REFUSED) temporal_spatial_outcome(&g_s.latch, 1, 0);
        if (recorded == VK_REC_DIRECT_REFUSED)
            upscaler_outcome(frame, 1, TEMPORAL_UPSCALER_FAILED, "its evaluate failed");
        else
            upscaler_outcome(frame, 1, TEMPORAL_UPSCALER_STOPPED,
                             recorded == VK_REC_DIRECT_FAILED ? "a submission did not run as recorded"
                                                              : "the frame's command buffer could not be used");
        return;
    }
    /* each channel is held to the largest finite value the scene colour holds (B10G11R11: 11, 11 and 10 bits) */
    Constants wc = *pc;
    wc.depth[0] = wc.depth[1] = temporal_ufloat_largest(6);
    wc.depth[2] = temporal_ufloat_largest(5);
    if (below) {
        /* D3-3: the library's W x H picture written into the larger texture's picture part, then the edge fill
           and D3-2's publication; the scene colour stays as drawn */
        spatial_larger_begin(cb);
        fullscreen(cb, g_s.larger.view, scene->sw, scene->sh, g_u.write_pipe, set, &wc, NULL);
        vk_surf_wrote(scene); /* the same bookkeeping as D3-2's hook: copies of the scene made before it go */
        vk_rt_drawn(scene);
        spatial_fill_publish(cb, scene);
    } else {
        /* the write-back: the library's picture read in a fragment shader, the scene colour written */
        vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        fullscreen(cb, scene->att, scene->sw, scene->sh, g_u.write_pipe, set, &wc, area);
        vk_surf_wrote(scene);
        vk_rt_drawn(scene);
    }
    temporal_rect_wrote(&g_t.rect_gate, frame, &stamp);
    g_u.evaluated_at = frame;
    g_u.evaluated_ns = now_ns;
    upscaler_outcome(frame, 1, TEMPORAL_UPSCALER_EVALUATED, "");
}

/* ------------------------------------------------------------ SNX_ARCEUS_UPSCALER=spatial: our own scaler (D3) */
/* Dormant without its switch: nothing below runs unless g_s.asked is 1, which only spatial_start sets. The rules are
   temporal_spatial.h's. The resolve writes a private picture, which is scaled into the picture part of a larger
   texture. The audited readers are given that texture with the size query's variant. The scene stays as drawn. */

/* the mode goes off for the session: one line. D2 restarts from a reset at the next frame's first draw, before its
   decision (spatial_now): the history and the rectangle's candidate cleared, so that frame seeds (revision 3 c) */
static void spatial_went_off(void) {
    if (g_s.said_off) return;
    g_s.said_off = 1;
    g_vk_watch = NULL;
    /* D3-3: FSR's mode runs no resolve of ours, so a frame below native is never jittered once the hand-over is off
       (research 21:05): upscaler_below refuses it at the gate's decision, which comes after the latch is settled
       at the frame's first draw (spatial_now, which also resets the rectangle's candidate and the history) */
    if (g_u.below)
        LOG("arceus-upscaler: FSR below native is off for the rest of the session (%s): from the next frame on, frames "
            "below native get no jitter and are left as drawn (the rectangle's candidate and history reset); FSR at "
            "full size goes on\n", g_s.latch.why ? g_s.latch.why : "?");
    else
        LOG("arceus-upscaler: spatial is off for the rest of the session (%s): D2 goes on, restarted from a reset\n",
            g_s.latch.why ? g_s.latch.why : "?");
}
static void spatial_failed(const char* why) {
    temporal_spatial_make_failed(&g_s.latch, why);
    spatial_went_off();
}
/* The frame's state, settled when a later frame is first seen: the latch takes the frame's outcome, and an
   off from the next frame takes effect. 1 while the mode is on. */
static int spatial_now(u32 frame) {
    if (!g_s.asked) return 0;
    if (g_s.frame != frame) {
        if (g_s.frame && g_s.fr.recorded) {
            if (g_s.fr.published) g_s.replaced++;
            else g_s.as_drawn++;
        }
        if (g_s.frame) temporal_spatial_frame_end(&g_s.fr, &g_s.latch);
        g_s.fr = (TemporalSpatialFrame){0, 0, 0};
        g_s.frame = frame;
        g_vk_watch = NULL;
        if (!temporal_spatial_frame(&g_s.latch)) spatial_went_off();
        if (g_s.latch.off && !g_s.restarted) {
            g_s.restarted = 1;
            temporal_rect_clear(&g_t.rect_gate);
            g_t.resolved_at = 0;
        }
        if (!(frame % 600))
            LOG("arceus-upscaler: spatial at frame %u: %llu frames replaced, %llu left as drawn (the first: %s), "
                "%llu consumers not replaced (the first: %s)%s\n", frame, (unsigned long long)g_s.replaced,
                (unsigned long long)g_s.as_drawn, g_s.reason[0] ? g_s.reason : "none",
                (unsigned long long)g_s.consumers, g_s.consumer[0] ? g_s.consumer : "none",
                g_s.latch.off ? ", off" : "");
    }
    return !g_s.latch.off;
}
/* The switch's start (from upscaler_start): it needs the anti-aliasing in the rectangle and the fixed resolution,
   and refuses the sharpen and the views, which read or write the scene colour after the resolve (revision 3) */
static void spatial_start(void) {
    const char* fixed = getenv("SNX_ARCEUS_FIXED_RESOLUTION");
    const char* view = getenv("SNX_ARCEUS_TEMPORAL_VIEW");
    const char* sharpen = getenv("SNX_ARCEUS_TEMPORAL_SHARPEN");
    const char* why = g_t.mode != MODE_TAA                  ? "it needs SNX_ARCEUS_TEMPORAL=taa"
                      : !g_t.rect                           ? "it needs SNX_ARCEUS_TEMPORAL_RECT=1"
                      : !fixed || strcmp(fixed, "1")        ? "it needs SNX_ARCEUS_FIXED_RESOLUTION=1"
                      : sharpen && strtof(sharpen, NULL) != 0.0f ? "SNX_ARCEUS_TEMPORAL_SHARPEN is not 0"
                      : view && view[0] && strcmp(view, "0") ? "SNX_ARCEUS_TEMPORAL_VIEW is not 0"
                                                             : NULL;
    if (why) {
        LOG("arceus-upscaler: spatial is not started (%s): the run goes on as without the switch\n", why);
        return;
    }
    g_s.asked = 1;
    LOG("arceus-upscaler: spatial asked for. EXPERIMENTAL: the resolve writes a private picture, scaled to the target "
        "with Catmull-Rom into a larger texture, which the audited readers 21FBFBEC, B46268AD and 8A160A27 are "
        "given. Only at a scale of 1/2, 3/5, 2/3, 3/4 or 4/5, otherwise D2 as without the switch. Its numbers are "
        "of the private picture before the scale\n");
}
/* Two floats of a bank a stage's program reads at offset, as the draw binds them: guest memory, which is cb_get's
   source (render.c) unless a pending compute or graphics result or GPU-owned bytes cover the range, newer on the GPU
   only (buffer.c buf_get_owned). 1 when available. 0 when the program does not read them, the bank is not bound or
   too short, the bytes are not readable, or they are pending or owned: whether the entry needs them is its rule's
   (research 12:50, 14:00, 15:00). out is 0 then, never a stand-in for "not available". */
static int spatial_block(const GpuChannel* ch, int stage, const VkSh* program, int bank, u32 offset, double out[2]) {
    out[0] = out[1] = 0.0;
    if (!program || !(program->si.cbuf_mask >> bank & 1u) || program->si.cbuf_max[bank] < offset + 8) return 0;
    const u64 address = ch->cbind[stage][bank];
    const u32 size = ch->cbind_size[stage][bank];
    if (!address || size < offset + 8) return 0;
    if (vk_compute_pending(address + offset, 8) || (vk_buf_pending_any() && vk_buf_pending(address + offset, 8)) ||
        vk_buf_owned(address + offset, 8))
        return 0;
    const u8* bytes = gmem_host(address + offset, 8);
    if (!bytes) return 0;
    float words[2];
    memcpy(words, bytes, sizeof words);
    out[0] = words[0];
    out[1] = words[1];
    return 1;
}
/* The larger texture of M x N, and with picture the private picture of the scene's size: the format's features and
   each image's limits for its uses asked first. A failure turns the mode off at once (a make failure). */
static int spatial_larger(const Surf* scene, int M, int N, int picture) {
    VkFormatProperties has;
    vkGetPhysicalDeviceFormatProperties(g_vk.phys, scene->f.vk, &has);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                      VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((has.optimalTilingFeatures & need) != need) {
        spatial_failed("the scene colour's format cannot be drawn to, blitted and sampled");
        return 0;
    }
    const VkImageUsageFlags drawn = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    const VkImageUsageFlags filled = drawn | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    /* the device's own limit for each image's format and uses, asked before either is made (astra's O5) */
    VkImageFormatProperties limit[2];
    const int asked = VK_OK(vkGetPhysicalDeviceImageFormatProperties(g_vk.phys, scene->f.vk, VK_IMAGE_TYPE_2D,
                                                                     VK_IMAGE_TILING_OPTIMAL, drawn, 0, &limit[0])) &&
                      VK_OK(vkGetPhysicalDeviceImageFormatProperties(g_vk.phys, scene->f.vk, VK_IMAGE_TYPE_2D,
                                                                     VK_IMAGE_TILING_OPTIMAL, filled, 0, &limit[1]));
    const u32 fb_w = g_vk.props.limits.maxFramebufferWidth, fb_h = g_vk.props.limits.maxFramebufferHeight;
    if (!asked ||
        !temporal_spatial_fits(scene->sw, scene->sh, limit[0].maxExtent.width, limit[0].maxExtent.height, fb_w, fb_h) ||
        !temporal_spatial_fits((u32)M, (u32)N, limit[1].maxExtent.width, limit[1].maxExtent.height, fb_w, fb_h)) {
        spatial_failed("the device takes no private picture or larger texture of these sizes for their uses");
        return 0;
    }
    /* views of another mapping go before their image is made anew */
    if (g_s.larger.img && (g_s.larger.w != (u32)M || g_s.larger.h != (u32)N || g_s.larger.format != scene->f.vk))
        spatial_views_drop();
    if ((picture && !image_ensure(&g_s.picture, scene->f.vk, scene->sw, scene->sh, drawn, "private picture")) ||
        !image_ensure(&g_s.larger, scene->f.vk, (u32)M, (u32)N, filled, "larger")) {
        spatial_failed("the private picture or the larger texture");
        return 0;
    }
    return 1;
}
/* At the hook, before the resolve: whether this frame is replaced, with everything it needs made. A scale not on the
   list leaves the frame to D2 with a line. A failure to make a part turns the mode off at once. */
static int spatial_ready(u32 frame, const Surf* scene, const TemporalRect* area, const TemporalRectGate* rg,
                         VkDescriptorSet* set) {
    static u64 refused;
    int M, N, scale = -1;
    const int W = (int)scene->sw, H = (int)scene->sh;
    if (!spatial_now(frame) || !area) return 0;
    if (!area->x && !area->y) scale = temporal_spatial_scale_listed(W, H, area->w, area->h);
    /* the confirmed fractions are the scale's float, bit for bit (research 14:00) */
    if (scale >= 0 && !temporal_spatial_confirmed(rg->now.fx, rg->now.fy, scale)) scale = -1;
    if (scale < 0 || scene->levels != 1 ||
        !temporal_spatial_size(W, H, area->w, area->h, rg->now.fx, rg->now.fy, &M, &N)) {
        if (refused++ < 4 || refused % 600 == 0)
            LOG("arceus-upscaler: spatial: frame %u is left to D2 (rectangle %d,%d %dx%d of %dx%d at %g x %g, %u "
                "levels: not 1/2, 3/5, 2/3, 3/4 or 4/5 at the origin with that fraction exactly and one level), %llu "
                "such so far\n", frame,
                area->x, area->y, area->w, area->h, W, H, (double)rg->now.fx, (double)rg->now.fy, scene->levels,
                (unsigned long long)refused);
        return 0;
    }
    if (!spatial_larger(scene, M, N, 1)) return 0;
    if (!g_s.scale_pipe || g_s.scale_format != scene->f.vk) {
        g_s.scale_pipe = pipeline_make(scene->f.vk, temporal_spatial_fs_spv, temporal_spatial_fs_spv_size, "scale");
        g_s.scale_format = scene->f.vk;
    }
    VkImageView views[SETS_BINDINGS];
    VkSampler samplers[SETS_BINDINGS];
    for (int i = 0; i < SETS_BINDINGS; i++) {
        views[i] = g_s.picture.view;
        samplers[i] = g_t.point;
    }
    *set = g_s.scale_pipe ? set_make(views, samplers) : VK_NULL_HANDLE;
    if (!*set) {
        spatial_failed("the scale's pipeline or set");
        return 0;
    }
    g_s.W = W;
    g_s.H = H;
    g_s.M = M;
    g_s.N = N;
    /* the fraction the blocks must hold: the float nearest the scale's p / q (research 12:50) */
    g_s.f[0] = g_s.f[1] = temporal_spatial_scale_fraction(scale);
    return 1;
}
/* D3-3: at FSR's hook, a frame below native: its listed scale, the larger texture made for it, the fractions the
   readers' blocks must hold */
static int spatial_below_ready(u32 frame, const Surf* scene, const TemporalRectGate* rg) {
    int M, N;
    const TemporalRectState* n = &rg->now;
    const int k = temporal_spatial_below(n->target_w, n->target_h, n->rect.x, n->rect.y, n->rect.w, n->rect.h, n->fx,
                                         n->fy);
    if (!spatial_now(frame) || k < 0 || scene->levels != 1 || (int)scene->sw != n->target_w ||
        (int)scene->sh != n->target_h ||
        !temporal_spatial_size(n->target_w, n->target_h, n->rect.w, n->rect.h, n->fx, n->fy, &M, &N))
        return 0;
    if (!spatial_larger(scene, M, N, 0)) return 0;
    g_s.W = n->target_w;
    g_s.H = n->target_h;
    g_s.M = M;
    g_s.N = N;
    g_s.f[0] = g_s.f[1] = temporal_spatial_scale_fraction(k);
    return 1;
}
/* The larger texture before its picture part is drawn: last read by the frame before's readers and filled by
   blits, both done first */
static void spatial_larger_begin(VkCommandBuffer cb) {
    vk_barrier(cb, g_s.larger.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLED_STAGES | VK_PIPELINE_STAGE_2_BLIT_BIT, 0,
               VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}
/* After the picture part was drawn: the edge fill, and the frame's publication with what the scene colour is now
   (its write sequence after the hook's own bookkeeping), the watch armed until the present */
static void spatial_fill_publish(VkCommandBuffer cb, const Surf* scene) {
    /* the edge fill reads and writes the same image: in the general layout */
    vk_barrier(cb, g_s.larger.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
               VK_IMAGE_LAYOUT_GENERAL);
    TemporalSpatialRegion fill[3];
    const int count = temporal_spatial_fill(g_s.W, g_s.H, g_s.M, g_s.N, fill);
    VkImageBlit blits[3];
    for (int i = 0; i < count; i++)
        blits[i] = (VkImageBlit){.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                 .srcOffsets = {{fill[i].from[0], fill[i].from[1], 0},
                                                {fill[i].from[2], fill[i].from[3], 1}},
                                 .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                 .dstOffsets = {{fill[i].to[0], fill[i].to[1], 0}, {fill[i].to[2], fill[i].to[3], 1}}};
    if (count)
        rec_CmdBlitImage(cb, g_s.larger.img, VK_IMAGE_LAYOUT_GENERAL, g_s.larger.img, VK_IMAGE_LAYOUT_GENERAL,
                         (u32)count, blits, VK_FILTER_NEAREST);
    vk_barrier(cb, g_s.larger.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT,
               VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_SAMPLED_STAGES,
               VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g_s.scene = scene;
    g_s.image = scene->img;
    g_s.wseq = scene->wseq;
    g_s.gva = scene->gva;
    g_s.bytes = vk_surf_guest_size(scene);
    g_s.fr.recorded = 1;
    g_vk_watch = scene; /* armed until the present */
}
/* After the resolve wrote the private picture: the scale into the picture part, the edge fill, and the frame's
   publication with what the scene colour is now (its write sequence after the hook's own bookkeeping) */
static void spatial_record(VkCommandBuffer cb, const Surf* scene, const TemporalRect* area, VkDescriptorSet set,
                           const Constants* pc) {
    vk_barrier(cb, g_s.picture.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    spatial_larger_begin(cb);
    Constants sc = *pc;
    sc.size[0] = (float)area->w;
    sc.size[1] = (float)area->h;
    sc.size[2] = (float)g_s.W;
    sc.size[3] = (float)g_s.H;
    fullscreen(cb, g_s.larger.view, (u32)g_s.W, (u32)g_s.H, g_s.scale_pipe, set, &sc, NULL);
    spatial_fill_publish(cb, scene);
}

/* The watch (research 11:25): a use of the scene colour's Surf while armed. One from the binding being made is
   judged with what that binding was given. */
void vk_temporal_watch(const Surf* s, int kind) {
    static const char* const what[5] = {"a read of the scene colour", "a write of the scene colour",
                                        "a write of the scene colour", "the scene colour's end",
                                        "a texture over the scene colour's memory"};
    const u32 frame = g_vk_present + 1;
    if (!g_s.asked || g_s.frame != frame || !g_s.fr.recorded || kind < 0 || kind > VK_WATCH_OVERLAP ||
        (s != g_s.scene && kind != VK_WATCH_OVERLAP)) {
        if (g_s.frame != frame) g_vk_watch = NULL;
        return;
    }
    /* another surface bound as a texture: a consumer only when it lies over the scene colour's memory (the scene
       made anew or parked since the hook, a crop of it, another view of its bytes) */
    if (kind == VK_WATCH_OVERLAP &&
        (!s || s == g_s.scene || !temporal_spatial_overlaps(s->gva, vk_surf_guest_size(s), g_s.gva, g_s.bytes)))
        return;
    const int binding = g_s.binding_on && kind == VK_WATCH_READ;
    const int use = temporal_spatial_use(&g_s.fr, &g_s.latch, binding && g_s.binding_listed,
                                         binding ? g_s.binding_given : -1, what[kind]);
    if (use == TEMPORAL_SPATIAL_FAULT) {
        g_s.faults++;
        spatial_went_off();
    } else if (use == TEMPORAL_SPATIAL_CONSUMER) {
        if (!g_s.consumers++)
            snprintf(g_s.consumer, sizeof g_s.consumer, "%s at frame %u", what[kind], frame);
        if (g_s.consumers <= 4)
            LOG("arceus-upscaler: spatial: frame %u, %s after the hook: a consumer not replaced, off from the next "
                "frame\n", frame, what[kind]);
    }
}
void vk_temporal_spatial_binding(int on, int listed, int given) {
    g_s.binding_on = on;
    g_s.binding_listed = listed;
    g_s.binding_given = given;
}
/* the renderer found its own substitution broken: off at once */
void vk_temporal_spatial_fault(const char* what) {
    g_s.faults++;
    LOG("arceus-upscaler: spatial: %s (a fault of ours)\n", what);
    spatial_failed(what);
}
/* the larger texture's views with another mapping go with the image they were made of */
static void spatial_views_drop(void) {
    for (int i = 0; i < g_s.nviews; i++)
        vk_defer_image(VK_NULL_HANDLE, VK_NULL_HANDLE, g_s.views[i].view, VK_NULL_HANDLE);
    g_s.nviews = 0;
    g_s.views_of = VK_NULL_HANDLE;
}
static VkComponentSwizzle spatial_swizzle(VkComponentSwizzle s, VkComponentSwizzle own) {
    return s == VK_COMPONENT_SWIZZLE_IDENTITY ? own : s;
}
/* A view of the larger texture with exactly the game view's component mapping (research 19:40, astra's O2), made
   once a mapping and kept with its image. NULL when it cannot be made: a make failure, the mode off at once. */
VkImageView vk_temporal_spatial_view_for(const VkComponentMapping* map) {
    if (!g_s.larger.img) return VK_NULL_HANDLE;
    if (g_s.views_of != g_s.larger.img) spatial_views_drop();
    const VkComponentMapping m = {spatial_swizzle(map->r, VK_COMPONENT_SWIZZLE_R),
                                  spatial_swizzle(map->g, VK_COMPONENT_SWIZZLE_G),
                                  spatial_swizzle(map->b, VK_COMPONENT_SWIZZLE_B),
                                  spatial_swizzle(map->a, VK_COMPONENT_SWIZZLE_A)};
    for (int i = 0; i < g_s.nviews; i++)
        if (!memcmp(&g_s.views[i].map, &m, sizeof m)) return g_s.views[i].view;
    VkImageViewCreateInfo vci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                 .image = g_s.larger.img,
                                 .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                 .format = g_s.larger.format,
                                 .components = m,
                                 .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkImageView view = VK_NULL_HANDLE;
    if (g_s.nviews >= (int)(sizeof g_s.views / sizeof g_s.views[0]) ||
        !VK_OK(vkCreateImageView(g_vk.dev, &vci, NULL, &view))) {
        spatial_failed("the larger texture's view with the game view's component mapping");
        return VK_NULL_HANDLE;
    }
    g_s.views[g_s.nviews].map = m;
    g_s.views[g_s.nviews++].view = view;
    g_s.views_of = g_s.larger.img;
    return view;
}
/* A binding of texture t by fragment program fs0 at handle (words), its draw described in d by the renderer: what it
   is given. One decision for both the texture and the variant (revision 3). listed: an entry of the list. */
int vk_temporal_spatial_decide(const GpuChannel* ch, const VkSh* vs, const VkSh* fs0, u32 handle, const Surf* t,
                               TemporalSpatialDraw* d, int* listed) {
    const u32 frame = g_vk_present + 1;
    *listed = 0;
    if (!fs0 || !spatial_now(frame) || !g_s.fr.recorded) return TEMPORAL_SPATIAL_AS_DRAWN;
    if (t != g_s.scene) return TEMPORAL_SPATIAL_AS_DRAWN;
    int k = temporal_spatial_entry(fs0->hash, handle);
    /* for the line: the program's own entry when its binding is another (research 15:40) */
    int own = k;
    for (int e = 0; own < 0 && e < TEMPORAL_SPATIAL_ENTRIES; e++)
        if (temporal_spatial_entries[e].fs == fs0->hash) own = e;
    if (k >= 0 && (t->img != g_s.image || t->wseq != g_s.wseq)) {
        temporal_spatial_unknown(&g_s.latch, "the scene colour changed after the hook");
        k = -1;
    }
    *listed = k >= 0;
    int verdict = TEMPORAL_SPATIAL_NOT_LISTED;
    if (k >= 0) {
        d->vs = vs ? vs->hash : 0;
        d->vertex_f_ok = spatial_block(ch, ST_VS, vs, TEMPORAL_SCENE_BANK, TEMPORAL_SCENE_FRACTION, d->vertex_f);
        d->vertex_origin_ok = spatial_block(ch, ST_VS, vs, 1, 0, d->vertex_origin);
        d->fragment_f_ok = spatial_block(ch, ST_FS, fs0, TEMPORAL_SCENE_BANK, TEMPORAL_SCENE_FRACTION, d->fragment_f);
        d->divisor_ok = spatial_block(ch, ST_FS, fs0, TEMPORAL_SCENE_BANK, 0x794, d->divisor);
        /* any failure of class U, a required value not available included, whatever else fails: a reader not on the
           list, the mode off from the next frame. Only the fraction and the coverage leave it listed (d08b624c) */
        if (temporal_spatial_classify(k, d, g_s.f, &verdict) < 0) {
            static char why[160]; /* the latch keeps the first reason: written only while it has none */
            if (!g_s.latch.off && !g_s.latch.off_next)
                snprintf(why, sizeof why, "a reader not on the list: %s", temporal_spatial_text(verdict));
            temporal_spatial_unknown(&g_s.latch, why);
            k = -1;
            *listed = 0;
        }
    }
    /* every condition that failed, with its class, while the frame is not yet decided or the first few times */
    if (own >= 0 && verdict != TEMPORAL_SPATIAL_TAKEN) {
        static u64 refusals;
        if (refusals++ < 4 || refusals % 600 == 0) {
            if (k < 0) { /* the conditions were not asked: ask them of the program's own entry, for the line */
                d->vs = vs ? vs->hash : 0;
                d->vertex_f_ok =
                    spatial_block(ch, ST_VS, vs, TEMPORAL_SCENE_BANK, TEMPORAL_SCENE_FRACTION, d->vertex_f);
                d->vertex_origin_ok = spatial_block(ch, ST_VS, vs, 1, 0, d->vertex_origin);
                d->fragment_f_ok =
                    spatial_block(ch, ST_FS, fs0, TEMPORAL_SCENE_BANK, TEMPORAL_SCENE_FRACTION, d->fragment_f);
                d->divisor_ok = spatial_block(ch, ST_FS, fs0, TEMPORAL_SCENE_BANK, 0x794, d->divisor);
            }
            const uint32_t failed = temporal_spatial_failed(own, d, g_s.f);
            char list[900];
            size_t at = 0;
            list[0] = 0;
            /* a binding that is not the entry's is itself a condition of class U (reviewer 04:42) */
            if (k < 0 && handle != temporal_spatial_entries[own].handle)
                at += (size_t)snprintf(list, sizeof list, "its binding %x is not the entry's %x (class U)", handle,
                                       temporal_spatial_entries[own].handle);
            for (int r = 1; r < TEMPORAL_SPATIAL_REASONS && at + 160 < sizeof list; r++)
                if (failed >> r & 1)
                    at += (size_t)snprintf(list + at, sizeof list - at, "%s%s (class %s)", at ? "; " : "",
                                           temporal_spatial_text(r), temporal_spatial_class_n(r) ? "N" : "U");
            LOG("arceus-upscaler: spatial: frame %u, fragment program %llx at binding %x (its entry's binding %x): "
                "refused, %s. Every condition that fails against its entry: %s, %llu such so far\n", frame,
                (unsigned long long)fs0->hash, handle, temporal_spatial_entries[own].handle,
                k < 0 ? "not on the list" : "listed", list[0] ? list : "none", (unsigned long long)refusals);
        }
    }
    const int first = !g_s.fr.decided && k >= 0;
    const int given = temporal_spatial_draw(&g_s.fr, &g_s.latch, k, verdict);
    if (first && verdict != TEMPORAL_SPATIAL_TAKEN && !g_s.reason[0])
        snprintf(g_s.reason, sizeof g_s.reason, "frame %u, program %llx: %s", frame, (unsigned long long)fs0->hash,
                 temporal_spatial_text(verdict));
    if (k < 0) {
        static u64 unknown;
        if (unknown++ < 4 || unknown % 600 == 0)
            LOG("arceus-upscaler: spatial: frame %u, a reader not on the list: fragment program %llx, vertex program "
                "%llx, binding %x, %s: as drawn, off from the next frame, %llu such so far\n", frame,
                (unsigned long long)fs0->hash, (unsigned long long)(vs ? vs->hash : 0), handle,
                g_s.fr.published ? "the frame was published" : "the frame was not published",
                (unsigned long long)unknown);
        if (!g_s.consumers++)
            snprintf(g_s.consumer, sizeof g_s.consumer, "fragment program %llx, vertex %llx, binding %x",
                     (unsigned long long)fs0->hash, (unsigned long long)(vs ? vs->hash : 0), handle);
    }
    return given;
}

/* The switch, read once with the anti-aliasing's own (temporal_on). It needs the anti-aliasing and is refused
   with rejected vectors. An adapter that is not there, that names needs of the instance, or whose needs of the
   device are not all in the record of what the game's device was made with (upscaler_host.c), is not available:
   one line, and the run goes on as without the switch. */
static void upscaler_start(void) {
    const char* name = getenv("SNX_ARCEUS_UPSCALER");
    const char* target = getenv("SNX_ARCEUS_TARGET_FPS");
    g_u.asked = 0;
    if (!name || !name[0] || !strcmp(name, "0") || !strcmp(name, "off")) return;
    if (!strcmp(name, "spatial")) {
        spatial_start();
        return;
    }
    /* a name the self-test alone knows: one line, and nothing of it is looked up, loaded, asked or started */
    if (vk_upscaler_not_in_game(name)) return;
    const int start = temporal_upscaler_start(g_t.mode == MODE_TAA, g_t.reject);
    if (start != TEMPORAL_UPSCALER_START_OK) {
        LOG("arceus-upscaler: %s is not started: %s\n", name,
            start == TEMPORAL_UPSCALER_START_NOT_TAA
                ? "it needs SNX_ARCEUS_TEMPORAL=taa"
                : "SNX_ARCEUS_TEMPORAL_REJECT=1 is set, and an upscaler has no rejected vectors");
        return;
    }
    const SnxUpscalerApi* api = vk_upscaler_adapter(name);
    g_u.two_channel = !strcmp(name, "dlss"); /* DLSS takes the vectors in two channels (DL2 point 3) */
    if (g_u.two_channel) {
        /* SNX_ARCEUS_DLSS_JITTER_SIGN, the module's experiment (research 18:45): said here as the module reads it */
        const char* forced = getenv("SNX_ARCEUS_DLSS_JITTER_SIGN");
        int is_forced = 0;
        static const char* const forcing[8] = {"--", "-+", "+-", "++", "mm", "mp", "pm", "pp"};
        for (int k = 0; forced && k < 8; k++) is_forced |= !strcmp(forced, forcing[k]);
        if (is_forced)
            LOG("arceus-upscaler: dlss in the game (DL2): DLAA at full size, the vectors in two channels. The jitter's "
                "sign %s is forced by experiment (SNX_ARCEUS_DLSS_JITTER_SIGN), not chosen by the protocol\n", forced);
        else
            LOG("arceus-upscaler: dlss in the game (DL2): DLAA at full size, the vectors in two channels. The jitter's "
                "sign (+1, +1), chosen by the user's look in the game (forced -- shook)\n");
    }
    SnxUpscalerExtensions instance = {0};
    SnxUpscalerDeviceNeeds device = {0};
    const char* why = NULL;
    if (!api || !api->instance_extensions || !api->device_extensions || !api->create || !api->prepare ||
        !api->evaluate || !api->destroy)
        why = "no such adapter, or its library is not there";
    else if (api->abi != SNX_UPSCALER_ABI) /* before anything of it is called */
        why = "the adapter is of another version of the contract";
    else if (api->instance_extensions(&instance) != SNX_UPSCALER_OK ||
             api->device_extensions(g_vk.instance, g_vk.phys, vkGetInstanceProcAddr, &device) != SNX_UPSCALER_OK)
        why = "its needs of the instance and the device could not be asked";
    else if (instance.count && !vk_upscaler_instance_met(api)) /* added when the instance was made (DL2) */
        why = "it names needs of the instance that the game's instance was not made with";
    else if (!snx_needs_none(device.extensions.count, device.features, device.core)) {
        /* asked again here, and held against what the game's device was made with: every name and every bit */
        const int met = vk_upscaler_device_met(api, name, device.extensions.names, device.extensions.count,
                                               device.features, device.core);
        if (met != SNX_NEEDS_OK) why = snx_needs_text(met);
    }
    /* The private output's format, for the type, tiling, uses and flags image_ensure makes it with. The largest
       size it answers is kept: the scene's size is held against it before the image is made. */
    VkImageFormatProperties has;
    if (!why && vkGetPhysicalDeviceImageFormatProperties(g_vk.phys, UPSCALED_FORMAT, VK_IMAGE_TYPE_2D,
                                                         VK_IMAGE_TILING_OPTIMAL, UPSCALED_USAGE, 0,
                                                         &has) != VK_SUCCESS)
        why = "the device has no image of four 32 bit floats that is both a storage and a sampled image";
    else if (!why) {
        g_u.output_max[0] = has.maxExtent.width;
        g_u.output_max[1] = has.maxExtent.height;
    }
    if (why) {
        LOG("arceus-upscaler: %s is not available (%s): the run goes on as without the switch\n", name, why);
        return;
    }
    const double fps = target ? strtod(target, NULL) : 0.0;
    g_u.api = api;
    g_u.nominal = isfinite(fps) && fps > 0.0 ? 1.0 / fps : 0.0;
    g_u.was_rect = g_t.rect;
    g_u.was_view = g_t.view;
    g_u.was_extras = g_t.rect_extras_off;
    g_u.asked = 1;
    /* the rectangle's gate decides the frames, whatever its own switch says, and only the whole target is taken */
    g_t.rect = 1;
    g_t.view = 0;
    g_t.rect_extras_off = 1;
    LOG("arceus-upscaler: %s asked for. EXPERIMENTAL: its self-test's result is FAIL (the edge case's budgets and "
        "case 4's comparison with a plainly scaled frame are not met), and nothing about its quality is claimed. "
        "At full size only, in frames whose scene is the whole target\n", api->name ? api->name : name);
    LOG("arceus-upscaler: our own resolve, sharpen, history, numbers and views do not run with it. Without effect: "
        "SNX_ARCEUS_TEMPORAL_HISTORY, _CLAMP, _BLEND, _NEAREST, _LUMA, _SHARPEN, _VIEW and _RECT\n");
    /* D3-3: with the fixed resolution, frames below native at a listed scale go to FSR on D3-2's hand-over, whose
       lines say "spatial" (the hand-over's name); the scene scale itself is the opt-in */
    const char* fixed = getenv("SNX_ARCEUS_FIXED_RESOLUTION");
    g_u.below = fixed && !strcmp(fixed, "1") && !strcmp(name, "fsr"); /* D3-3 is FSR's (reviewer's note) */
    if (g_u.below) {
        g_s.asked = 1;
        LOG("arceus-upscaler: %s below native (D3-3, EXPERIMENTAL): frames at a scene scale of 1/2, 3/5, 2/3, 3/4 or "
            "4/5 are rendered by it at the rectangle and handed to the game's readers in a larger texture (the "
            "hand-over's lines say spatial); none of our numbers are made for them\n",
            api->name ? api->name : name);
    }
}

void vk_temporal_before(u64 fs0_hash) {
    if (!temporal_on() || !g_t.reader || fs0_hash != g_t.reader) return;
    const u32 frame = g_vk_present + 1;
    if (g_t.ran_at == frame) return;
    /* our passes are reached in this frame, whatever comes of them (the rectangle's gate counts the frames drawn
       with jitter in which they are not) */
    if (g_t.rect) temporal_rect_hooked(&g_t.rect_gate, frame);
    Surf *scene = g_t.scene_rt, *depth = g_t.depth_rt;
    /* With an upscaler this frame's colour target is kept for the next frame's decision, by value, with the frame
       it was seen in. One that is layered or part of an array is kept as having no layer the upscaler takes, so
       the next decision refuses jitter. In the frame it is first seen in, drawn with jitter, the evaluate is
       skipped: the frame is left as drawn once, which is not a failure of our passes. */
    if (upscaler_on() && scene && g_t.written_at == frame) {
        g_u.colour = temporal_upscaler_colour(frame, (unsigned)scene->f.vk, scene->layers, scene->base_layer,
                                              scene->group != NULL, (int)scene->sw, (int)scene->sh);
        if (!g_u.colour.layers && upscaler_jittered(frame)) {
            upscaler_guard(frame);
            upscaler_outcome(frame, 1, TEMPORAL_UPSCALER_SKIPPED, "the scene colour is layered or part of an array");
            return;
        }
    }
    if (g_t.camera_at != frame || g_t.written_at != frame || !scene || !depth || !g_t.have_before ||
        scene->layers != 1 || scene->group || depth->group || scene->sw != depth->sw || scene->sh != depth->sh) {
        if (g_t.skipped++ < 4 || g_t.skipped % 1800 == 0)
            LOG("arceus-temporal: skipped (camera %d, scene written %d, previous matrix %d), %llu so far\n",
                g_t.camera_at == frame, g_t.written_at == frame, g_t.have_before, (unsigned long long)g_t.skipped);
        rect_fail(frame, "the scene, its camera or the camera of the frame before is not known");
        return;
    }
    g_t.ran_at = frame;
    /* With an upscaler only the recorder's mark is read here. A context that completed is settled further down,
       when this frame's own configuration is confirmed (upscaler_warm, upscaler_resolve). */
    if (upscaler_on()) upscaler_guard(frame);
    /* Under the rectangle's gate nothing of ours runs unless the frame was accepted at its first draw and
       confirmed by the scene block since, with the G-buffer's viewport the one decided on: no vectors, no
       resolve, no sharpen, no numbers, no view, and no history for the frame after. */
    const TemporalRectGate* rg = &g_t.rect_gate;
    const TemporalViewport hook_view = {g_t.viewport.x, g_t.viewport.y, g_t.viewport.width, g_t.viewport.height};
    const int rect_ok = g_t.rect && temporal_rect_steady(rg, frame) && rg->now.target_w == (int)scene->sw &&
                        rg->now.target_h == (int)scene->sh && temporal_viewport_same(&hook_view, &rg->now.viewport);
    if (g_t.rect && !rect_ok) {
        if (rect_line(++g_t.rect_left))
            LOG("arceus-temporal-rect: frame left as drawn (%s), %llu so far\n",
                rg->decided_at != frame                 ? "not decided"
                : !rg->accepted                         ? "gated at its first draw"
                : rg->state == TEMPORAL_FRAME_DECIDED   ? "no draw confirmed it before our passes"
                : rg->state == TEMPORAL_FRAME_SEEN      ? "confirmed with a new fraction"
                : rg->state == TEMPORAL_FRAME_NONE      ? "found wrong after its decision"
                                                        : "the G-buffer's viewport or the target is another",
                (unsigned long long)g_t.rect_left);
        /* A confirmed frame refused here for its viewport or target was drawn with jitter and is not resolved.
           That is a disagreement found late: the start begins again, and only a row of such frames stops the
           gate (temporal_rect_refused). */
        if (temporal_rect_steady(rg, frame)) temporal_rect_refused(&g_t.rect_gate, frame);
        if (upscaler_on() && upscaler_jittered(frame))
            upscaler_outcome(frame, 1, TEMPORAL_UPSCALER_SKIPPED, "the gate did not take the frame at the hook");
        return;
    }
    /* with an upscaler a frame without jitter has none of our passes: at most a context is made for it */
    if (upscaler_on() && !upscaler_jittered(frame)) {
        upscaler_warm(frame, scene, depth, rect_ok);
        return;
    }
    const TemporalRect* area = rect_ok ? &rg->now.rect : NULL;
    if (!layouts_make()) {
        rect_fail(frame, "the passes' layouts");
        return;
    }
    if (!g_t.motion_pipe)
        g_t.motion_pipe = pipeline_make(MOTION_FORMAT, temporal_motion_fs_spv, temporal_motion_fs_spv_size, "motion");
    const int probe_view = g_t.mode == MODE_PROBE && g_t.view;
    if (probe_view && (!g_t.view_pipe || g_t.view_format != scene->f.vk)) {
        g_t.view_pipe = pipeline_make(scene->f.vk, temporal_view_fs_spv, temporal_view_fs_spv_size, "view");
        g_t.view_format = scene->f.vk;
    }
    const VkImageUsageFlags target = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    const VkImageUsageFlags copy = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    /* under the rectangle's gate a view that cannot be made is off for the session, and the rest goes on */
    if (g_t.rect && probe_view && !g_t.view_pipe) rect_extras_off("the view's pipeline");
    if (!g_t.motion_pipe || (!g_t.rect && probe_view && !g_t.view_pipe) ||
        !image_ensure(&g_t.motion, MOTION_FORMAT, scene->sw, scene->sh, target, "motion")) {
        rect_fail(frame, "the vector pass's pipeline or image");
        return;
    }
    if (!g_t.stats_pipe)
        g_t.stats_pipe = pipeline_make(STATS_FORMAT, temporal_stats_fs_spv, temporal_stats_fs_spv_size, "numbers");
    /* the frame's copies are for our own resolve and numbers: an upscaler has neither */
    if (!upscaler_on() && (!image_ensure(&g_t.colour[0], scene->f.vk, scene->sw, scene->sh, copy, "colour") ||
                           !image_ensure(&g_t.colour[1], scene->f.vk, scene->sw, scene->sh, copy, "colour"))) {
        rect_fail(frame, "the frame's copies");
        return;
    }
    const VkComponentMapping identity = {0};
    const VkImageView depth_view = vk_tex_view(depth, VK_IMAGE_VIEW_TYPE_2D, 0, &identity, 0);
    if (!depth_view) {
        rect_fail(frame, "the depth's view");
        return;
    }

    Constants pc = {.viewport = {g_t.viewport.x, g_t.viewport.y, g_t.viewport.width, g_t.viewport.height},
                    .depth = {g_t.depth_translate, g_t.depth_scale, 0, 0},
                    .size = {(float)scene->sw, (float)scene->sh, (float)g_t.view, 40.0f}};
    vk_temporal_multiply(g_t.inverse, g_t.before, pc.reproject);
    {
        double jx, jy;
        frame_jitter(frame, &jx, &jy);
        pc.jitter[0] = (float)jx;
        pc.jitter[1] = (float)jy;
        pc.jitter[2] = rect_ok ? 1.0f : 0.0f; /* the passes keep to the viewport's rectangle */
    }
    if (!(pc.depth[1] != 0.0f) || !(pc.viewport[2] != 0.0f) || !(pc.viewport[3] != 0.0f)) {
        rect_fail(frame, "a viewport or depth range without a size");
        return;
    }
    /* Under the rectangle's gate, what this frame's pictures are made from: the images, the sizes, the rectangle
       and the fraction. The frame's copy and the history are those of the frame before only if theirs is the
       same (temporal_rect_stamp_same). */
    TemporalRectStamp stamp = {0};
    if (rect_ok)
        stamp = (TemporalRectStamp){scene->gva,     rect_making(scene), (int)scene->sw, (int)scene->sh,
                                    (int)scene->sw, (int)scene->sh,     rg->now.rect,   rg->now.fx,
                                    rg->now.fy};

    vk_render_end();
    VkCommandBuffer cb = vk_cmd();
    /* with SNX_ARCEUS_TEMPORAL_GPUTIME: the first mark of the bracket around our passes here, -1 without */
    gputime_leftover(&g_t.time_hook);
    gputime_leftover(&g_t.time_numbers);
    if (gputime_on()) gputime_configure(scene, area);
    g_t.time_hook = gputime_begin(cb, TEMPORAL_GPU_HOOK);
    /* objects that moved were drawn into their own target this frame: the vector pass takes their vectors from it */
    const int objects = g_t.objects && g_t.object_at == frame && g_t.object.view && g_t.object.w == scene->sw &&
                        g_t.object.h == scene->sh && (g_t.rect || g_t.gate.mismatch_at != frame) &&
                        g_t.object_failed_at != frame && pairs_hold(frame);
    /* the vectors: depth sampled, the motion image written */
    vk_surf_use(cb, depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    /* with an upscaler the motion image was handed to a library a frame ago: its last use is every command's */
    vk_barrier(cb, g_t.motion.img, VK_IMAGE_ASPECT_COLOR_BIT,
               upscaler_on() ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
               upscaler_on() ? UPSCALER_WROTE : 0, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    {
        Constants mc = pc;
        /* the bound: an eighth of the scene's height, which under the rectangle's gate is the rectangle's */
        mc.depth[2] = area ? (float)temporal_rect_reject_bound(g_t.reject, area)
                           : (float)temporal_reject_bound(g_t.reject, (double)scene->sh);
        mc.depth[3] = objects ? 1.0f : 0.0f;
        if (objects)
            vk_barrier(cb, g_t.object.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        const VkImageView views[SETS_BINDINGS] = {depth_view, objects ? g_t.object.view : depth_view, depth_view,
                                                  depth_view, depth_view,                             depth_view,
                                                  depth_view};
        const VkSampler samplers[SETS_BINDINGS] = {g_t.point, g_t.point, g_t.point, g_t.point,
                                                   g_t.point, g_t.point, g_t.point};
        const VkDescriptorSet set = set_make(views, samplers);
        if (!set) {
            rect_fail(frame, "the vector pass's set");
            return;
        }
        fullscreen(cb, g_t.motion.view, g_t.motion.w, g_t.motion.h, g_t.motion_pipe, set, &mc, area);
    }
    vk_barrier(cb, g_t.motion.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g_t.runs++;
    if (g_t.runs <= 2 || g_t.runs % 1800 == 0)
        LOG("arceus-temporal: run %llu at present %u, scene %ux%u, viewport %g,%g %gx%g, depth = %g + %g x z (%s), "
            "view %d\n",
            (unsigned long long)g_t.runs, g_vk_present, scene->sw, scene->sh, (double)g_t.viewport.x,
            (double)g_t.viewport.y, (double)g_t.viewport.width, (double)g_t.viewport.height,
            (double)g_t.depth_translate, (double)g_t.depth_scale,
            g_t.depth_zero_to_one ? "zero to one" : "minus one to one", g_t.view);

    /* With an upscaler the frame goes to the library here, and nothing of our own resolve follows: no copy, no
       resolve, no sharpen, no history, no numbers and no view. The bracket of the GPU time ends around it. */
    if (upscaler_on()) {
        upscaler_resolve(frame, cb, scene, depth, depth_view, &pc, area, &hook_view);
        gputime_end(cb, &g_t.time_hook);
        return;
    }
    /* this frame's colour is copied aside: the numbers and the error view compare it with last frame's */
    Image* now = &g_t.colour[!g_t.newest];
    Image* before = &g_t.colour[g_t.newest];
    /* the copy of the frame before, for the numbers and the error view: under the rectangle's gate only one made
       from the same as this frame's */
    const int same_making = g_t.rect_raw_at + 1 == frame && temporal_rect_stamp_same(&g_t.rect_raw, &stamp);
    const int history = g_t.history_at + 1 == frame && (!g_t.rect || same_making);
    {
        const VkImageCopy region = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, scene->base_layer, 1},
                                    .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                    .extent = {scene->sw, scene->sh, 1}};
        vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT);
        vk_barrier(cb, now->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        rec_CmdCopyImage(cb, scene->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, now->img,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        vk_barrier(cb, now->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                   VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        g_t.newest = !g_t.newest;
        g_t.history_at = frame;
        g_t.rect_raw = stamp;
        g_t.rect_raw_at = frame;
    }
    const int taa = g_t.mode == MODE_TAA;
    const VkSampler samplers[SETS_BINDINGS] = {g_t.point, g_t.point, g_t.linear, g_t.point,
                                               g_t.point, g_t.point, g_t.point};
    /* the frame before as it was drawn, for the numbers: with jitter its edges differ from this frame's */
    const VkImageView last_frame = history ? before->view : now->view;
    VkImageView numbers_before = last_frame;
    /* the picture shown a frame ago, for the numbers: the resolved one with anti-aliasing on */
    VkImageView shown_before = VK_NULL_HANDLE;
    int numbers = history, sharpened = 0, spatial = 0;
    /* A frame without jitter (jitter_decide) or whose G-buffer viewport is not the one decided on is left as drawn:
       no resolve, no sharpen, no numbers, and no history for the frame after it. */
    const int steady = g_t.rect ? rect_ok : temporal_gate_steady(&g_t.gate, frame);
    if (taa && !steady) {
        if (g_t.unsteady++ < 4 || g_t.unsteady % 600 == 0)
            LOG("arceus-temporal: frame left as drawn (the viewport), %llu so far\n",
                (unsigned long long)g_t.unsteady);
        numbers = 0;
    } else if (taa && g_t.view != 4) {
        /* the resolve: the scene colour and the new history are written, the old history and this frame's copy
           read. No history is used on the first frame, after a gap, after a change of size or a camera cut. */
        const VkFormat formats[2] = {scene->f.vk, HISTORY_FORMAT};
        if (!g_t.resolve_pipe || g_t.resolve_format != scene->f.vk) {
            g_t.resolve_pipe =
                pipeline_make_n(formats, 2, temporal_resolve_fs_spv, temporal_resolve_fs_spv_size, "resolve");
            g_t.resolve_format = scene->f.vk;
        }
        if (g_t.history[0].w != scene->sw || g_t.history[0].h != scene->sh) g_t.resolved_at = 0;
        if (!g_t.resolve_pipe ||
            !image_ensure(&g_t.history[0], HISTORY_FORMAT, scene->sw, scene->sh, target, "history") ||
            !image_ensure(&g_t.history[1], HISTORY_FORMAT, scene->sw, scene->sh, target, "history")) {
            rect_fail(frame, "the resolve's pipeline or the history's images");
            return;
        }
        /* history images made anew are another making */
        if (rect_ok) stamp.generation = rect_making(scene);
        Image* kept = &g_t.history[g_t.history_newest];
        Image* made = &g_t.history[!g_t.history_newest];
        /* a cut: the screen's middle at a depth of ten moved by more than a quarter of the screen */
        const float* m = pc.reproject;
        const float w = 0.01f * m[11] + m[15], x = 0.01f * m[8] + m[12], y = 0.01f * m[9] + m[13];
        const int cut = !(w > 0.0f) || fabsf(x) > 0.5f * w || fabsf(y) > 0.5f * w;
        /* under the rectangle's gate the history is the frame before's only with the same stamp */
        const int had = g_t.rect ? temporal_rect_history(rg, frame, &stamp) : g_t.resolved_at + 1 == frame;
        const int usable = had && !cut;
        if (!usable && (g_t.resets++ < 4 || g_t.resets % 600 == 0))
            LOG("arceus-temporal: history not used (%s), %llu times so far\n", cut ? "camera cut" : "no frame before",
                (unsigned long long)g_t.resets);
        Constants rc = pc;
        rc.depth[2] = (float)g_t.options;
        rc.depth[3] = usable ? 0.0f : 1.0f;
        rc.size[3] = FRAME_WEIGHT;
        const VkImageView views[SETS_BINDINGS] = {g_t.motion.view, now->view, usable ? kept->view : now->view,
                                                  depth_view, last_frame, last_frame, last_frame};
        const VkDescriptorSet set = set_make(views, samplers);
        if (!set) {
            rect_fail(frame, "the resolve's set");
            return;
        }
        /* with SNX_ARCEUS_UPSCALER=spatial the resolve writes the private picture, the scene colour stays as drawn */
        VkDescriptorSet scale_set = VK_NULL_HANDLE;
        spatial = spatial_ready(frame, scene, area, rg, &scale_set);
        vk_barrier(cb, made->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if (spatial)
            vk_barrier(cb, g_s.picture.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                       VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        else
            vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        fullscreen2(cb, spatial ? g_s.picture.view : scene->att, made->view, scene->sw, scene->sh, g_t.resolve_pipe,
                    set, &rc, area);
        vk_barrier(cb, made->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        /* the sharpen: the resolved picture read from the new history, the scene colour written again. Not with
           the weight view, which is no picture. */
        if (g_t.sharpen > 0.0f && g_t.view != 3) {
            if (!g_t.sharpen_pipe || g_t.sharpen_format != scene->f.vk) {
                g_t.sharpen_pipe =
                    pipeline_make(scene->f.vk, temporal_sharpen_fs_spv, temporal_sharpen_fs_spv_size, "sharpen");
                g_t.sharpen_format = scene->f.vk;
            }
            const VkImageView sharpen_views[SETS_BINDINGS] = {made->view, made->view, made->view, made->view,
                                                              made->view, made->view, made->view};
            const VkDescriptorSet sharpen_set = g_t.sharpen_pipe ? set_make(sharpen_views, samplers) : VK_NULL_HANDLE;
            if (!sharpen_set && g_t.rect) {
                /* a sharpen that was asked for and cannot be made is off for the session, said once: the
                   resolved picture and its history, written before it, stand */
                LOG("arceus-temporal-rect: the sharpen is off for the rest of the session (its %s could not be "
                    "made)\n", g_t.sharpen_pipe ? "set" : "pipeline");
                g_t.sharpen = 0.0f;
            }
            if (sharpen_set) {
                Constants sc = pc;
                sc.size[3] = g_t.sharpen;
                /* a second write of the scene colour after the resolve's: ordered behind it */
                vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
                fullscreen(cb, scene->att, scene->sw, scene->sh, g_t.sharpen_pipe, sharpen_set, &sc, area);
                sharpened = 1;
            }
        }
        /* the same bookkeeping in both: copies of the scene made before the hook are out of date */
        vk_surf_wrote(scene);
        vk_rt_drawn(scene);
        if (spatial) spatial_record(cb, scene, area, scale_set, &pc);
        g_t.history_newest = !g_t.history_newest;
        g_t.resolved_at = frame;
        if (rect_ok) temporal_rect_wrote(&g_t.rect_gate, frame, &stamp);
        /* the numbers then compare the frame with what the resolve made of it */
        numbers_before = made->view;
        shown_before = usable ? kept->view : made->view;
        numbers = 1;
        pc.depth[2] = 1.0f;
    } else if (taa) {
        /* the jittered frame alone: no history to measure, the frame to frame number stays */
        numbers_before = now->view;
        pc.depth[2] = 1.0f;
    }

    /* the bracket around our passes ends here, and an empty one follows it as a baseline */
    gputime_end(cb, &g_t.time_hook);
    TemporalGpuHandle empty = gputime_begin(cb, TEMPORAL_GPU_EMPTY);
    gputime_end(cb, &empty);
    /* without a frame before (the first one, or after a gap) the frame is compared with itself */
    /* the probe's numbers also look at the pixels that took an object's vector */
    if (!taa && objects) {
        shown_before = g_t.object.view;
        pc.depth[3] = 1.0f;
    }
    /* A frame with a long move line has its numbers taken too, when no regular run is within STATS_SPACING runs: the
       buffer is read one run of the numbers later and the copy must be finished by then. */
    const u64 phase = g_t.runs % STATS_EVERY;
    const int moved_far = !taa && objects && g_t.long_logged_at == g_vk_present + 1 && phase >= STATS_SPACING &&
                          phase + STATS_SPACING <= STATS_EVERY;
    int stats_due = numbers && !g_t.rect_extras_off && (phase == 0 || moved_far);
    /* the picture as shown, for its gradient: the resolve's, or the scene colour itself after a sharpen */
    VkImageView final_view = numbers_before;
    if (sharpened && stats_due) {
        const VkImageView scene_view = vk_tex_view(scene, VK_IMAGE_VIEW_TYPE_2D, 0, &identity, 0);
        if (scene_view) {
            vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            final_view = scene_view;
        } else if (g_t.rect) {
            /* the sharpened picture cannot be read: no numbers of another picture in its place */
            rect_extras_off("the sharpened picture's view");
            stats_due = 0;
        }
    }
    const VkImageView views[SETS_BINDINGS] = {
        g_t.motion.view, now->view, numbers_before, depth_view, last_frame,
        shown_before ? shown_before : numbers_before, final_view};
    const VkDescriptorSet set = set_make(views, samplers);
    if (!set) {
        rect_extras_off("their set");
        return;
    }

    /* the numbers: every STATS_EVERY runs the small image is drawn and copied to memory, and the copy made that
       many runs ago, long finished, is read and logged */
    const int stats_made = stats_due && g_t.stats_pipe && stats_ready() &&
                           image_ensure(&g_t.stats, STATS_FORMAT, 2 * STATS_W, STATS_H,
                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                        "numbers");
    if (stats_due && !stats_made) rect_extras_off("the numbers' pipeline, buffer or image");
    /* The memory may not be coherent: what the GPU wrote is fetched before it is read. A fetch that fails logs
       nothing. Under the rectangle's gate it also ends the numbers, and none are drawn or kept in this frame. */
    int stats_write = stats_made;
    if (stats_made && g_t.stats_run) {
        if (VK_OK(vmaInvalidateAllocation(g_vk.vma, g_t.stats_memory, 0, VK_WHOLE_SIZE))) stats_log();
        else if (g_t.rect) {
            rect_extras_off("the read back of the numbers");
            stats_write = 0;
        }
    }
    /* the numbers and the view, where a frame has them, in a bracket of their own */
    const int view_due = !taa && g_t.view && !(g_t.rect && g_t.view == 2 && !history);
    g_t.time_numbers = stats_write || view_due ? gputime_begin(cb, TEMPORAL_GPU_NUMBERS) : -1;
    if (stats_write) {
        const VkBufferImageCopy out = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                       .imageExtent = {2 * STATS_W, STATS_H, 1}};
        vk_barrier(cb, g_t.stats.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, 0,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        fullscreen(cb, g_t.stats.view, 2 * STATS_W, STATS_H, g_t.stats_pipe, set, &pc, NULL);
        vk_barrier(cb, g_t.stats.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        rec_CmdCopyImageToBuffer(cb, g_t.stats.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_t.stats_buffer, 1, &out);
        /* the copy's writes made available to the host, which reads the buffer a run of the numbers later */
        const VkBufferMemoryBarrier2 read = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                                             .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                             .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                             .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
                                             .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
                                             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                             .buffer = g_t.stats_buffer,
                                             .size = VK_WHOLE_SIZE};
        const VkDependencyInfo host = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &read};
        rec_CmdPipelineBarrier2(cb, &host);
        g_t.stats_run = g_t.runs;
        g_s.stats_private = spatial;
        memcpy(g_t.stats_drawn, g_t.drawn, sizeof g_t.stats_drawn);
        g_t.stats_draws = g_t.object_draws;
        g_t.stats_height = area ? (u32)area->h : scene->sh;
    }
    if (!view_due) {
        /* no view: in taa, with none asked for, or under the rectangle's gate the error view without a frame
           before made from the same */
        gputime_end(cb, &g_t.time_numbers);
        return;
    }
    /* a view of the probe, drawn over the scene */
    vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    fullscreen(cb, scene->att, scene->sw, scene->sh, g_t.view_pipe, set, &pc, area);
    vk_surf_wrote(scene);
    vk_rt_drawn(scene);
    gputime_end(cb, &g_t.time_numbers);
}
