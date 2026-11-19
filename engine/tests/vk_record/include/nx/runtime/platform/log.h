/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* A stand-in for runtime/platform/log.h, for tests/vk_record alone: the log's lines go to stderr, and a fatal
   error ends the fixture as a failure. */
#ifndef SNX_VK_RECORD_LOG_H
#define SNX_VK_RECORD_LOG_H
#include <stdio.h>
#include <stdlib.h>
#define WARN(...) fprintf(stderr, __VA_ARGS__)
#define LOG(...)  fprintf(stderr, __VA_ARGS__)
#define LOG_LIMIT(n, ...) fprintf(stderr, __VA_ARGS__)
#define fatal(...) (fprintf(stderr, __VA_ARGS__), fprintf(stderr, "\n"), abort())
#endif
