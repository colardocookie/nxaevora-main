/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* vic_ref.c: the VIC composition on the CPU, into j's planes in guest memory: the reference the tests hold the
   shader (vic.comp) to */
#ifndef SNX_VIC_REF_H
#define SNX_VIC_REF_H
#include "nx/gpu/video/vic.h"

void vic_compose_ref(const VicJob* j);

#endif
