/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* A module's relocatable objects in one file, for the host that loads them with the JIT loader (code_loader.h) instead
   of linking them: "snx-objs", their count, then each one's size and bytes, sizes and count as 8-byte little-endian
   numbers. The unit cache keeps one (tests/unit_cache.c) */
#ifndef SNX_RECOMPILER_LLVM_OBJECTS_PACK_H
#define SNX_RECOMPILER_LLVM_OBJECTS_PACK_H
#include "common/cpu/recompiler/llvm/link.h"

/* The pack of count inputs (their data and size), malloc'd, or NULL when out of memory */
unsigned char* snx_objects_pack(const SnxLinkInput* inputs, size_t count, size_t* size);
/* The inputs of the pack at data (size bytes), whose data point into it, malloc'd with their count in *count; each
   name is name. NULL when it is not a pack, is cut short or is out of memory */
SnxLinkInput* snx_objects_unpack(const unsigned char* data, size_t size, const char* name, size_t* count);

#endif
