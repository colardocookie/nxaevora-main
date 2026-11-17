/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* nvdec_ref.c: an NVDEC picture written into guest memory on the CPU, the reference the tests hold the renderer's
   nvdec_write to */
#ifndef SNX_NVDEC_REF_H
#define SNX_NVDEC_REF_H
#include "nx/gpu/gpu.h"

void nvdec_write_ref(const NvdecPic* p);

#endif
