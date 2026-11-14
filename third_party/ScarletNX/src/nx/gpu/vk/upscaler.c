/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The upscaler interface (upscaler.h) is not used yet. This file only makes the build read the header, so that it
   stays a header that compiles. It defines nothing. */
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/vk/upscaler.h"

_Static_assert(SNX_UPSCALER_ABI >= 1, "the ABI number counts from 1");
_Static_assert(sizeof(((SnxUpscalerFrame*)0)->jitter) == 2 * sizeof(float), "the jitter is two floats");
_Static_assert(sizeof(((SnxUpscalerFrame*)0)->vector_scale) == 2 * sizeof(float), "the vector's scale is two floats");
