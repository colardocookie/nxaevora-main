/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* What the Debug window shows: the game's frame times and the per-frame costs the renderer sees, and the machine's
   load, temperature, power and memory. The frame numbers come from the present (renderer.c). The machine's come from
   a thread that polls once a second, and only while the window is open (debug_stats_hardware_demand). A value
   that the machine does not tell is -1 */
#ifndef SNX_DEBUG_STATS_H
#define SNX_DEBUG_STATS_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DEBUG_STATS_FRAMES = 120 }; /* the frames the graph keeps */

typedef struct {
    float frame_ms[DEBUG_STATS_FRAMES]; /* the oldest first */
    int frame_count;
    float fps, frame_last_ms, frame_max_ms; /* over the kept frames, -1 with fewer than two */
    /* means over the last frames, -1 with none */
    float prep_ms;         /* the time of a present up to the submit: the render thread's cost of showing a frame */
    float cpu_ms;          /* the process's CPU time, all threads, for each frame */
    float pipeline_wait_ms; /* the draws' wait for a pipeline that was not ready */
    float texture_uploads; /* uploads from guest memory for each frame */
    /* the machine: percent, degrees C, watts, megabytes, megahertz */
    int gpu_load, gpu_temp, gpu_power;
    int vram_used_mb, vram_total_mb, vram_clock_mhz;
    int cpu_load, cpu_temp, cpu_power;
    int ram_used_mb, ram_total_mb, game_ram_mb;
} DebugStats;

/* a game's present at now_ns: prep_ns is the time from the start of showing it; the two counters are the renderer's
   totals so far (vk_pipeline_stats' wait in ms, vk_tex_stats' uploads). Under vk_lock */
void debug_stats_frame(uint64_t now_ns, uint64_t prep_ns, uint64_t pipeline_wait_ms, uint64_t texture_uploads);
void debug_stats_get(DebugStats* out); /* any thread */
/* the window is open (called at every frame it draws): poll the machine once a second. gpu_uuid is the rendering
   Vulkan device's UUID (16 bytes), which picks the NVIDIA device. NULL when the window is closed: the polling stops */
void debug_stats_hardware_demand(const uint8_t* gpu_uuid);

#ifdef __cplusplus
}
#endif
#endif
