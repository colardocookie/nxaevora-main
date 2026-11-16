/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* dma_ref.c: a launch of the DMA copy engine done on the CPU in guest memory, the reference the tests hold the
   renderer's dma_copy to */
#ifndef SNX_DMA_REF_H
#define SNX_DMA_REF_H
#include "nx/gpu/gpu.h"

void dma_copy_ref(const GpuDmaOp* o);

#endif
