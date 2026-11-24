/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* A stand-in for runtime/options.h, for tests/vk_record alone: the settings gpu/vk/record.c reads. */
#ifndef SNX_VK_RECORD_OPTIONS_H
#define SNX_VK_RECORD_OPTIONS_H
typedef struct {
    int vulkan_recording_thread; /* 1: the frame's commands are recorded on a thread of their own */
    int async_pipelines;
    int profiler; /* 1: the recorder thread names itself for the profiler */
} FixtureSettings;
extern FixtureSettings g_settings;
#endif
