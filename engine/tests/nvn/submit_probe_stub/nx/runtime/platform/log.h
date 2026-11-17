/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The submit probe test's stand-in: the window line, kept */
#pragma once
#include <stdio.h>
extern char g_test_line[4096];
#define LOG(...) snprintf(g_test_line, sizeof g_test_line, __VA_ARGS__)
