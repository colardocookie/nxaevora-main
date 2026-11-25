/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "common/cpu/recompiler/llvm/objects_pack.h"
#include "common/utilities/common.h"
#include "common/utilities/endian.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char* snx_objects_pack(const SnxLinkInput* inputs, size_t count, size_t* size) {
    size_t total = 16;
    for (size_t i = 0; i < count; ++i) total += 8 + inputs[i].size;
    unsigned char* data = (unsigned char*)malloc(total);
    if (!data) return NULL;
    memcpy(data, "snx-objs", 8);
    snx_st_le64(data + 8, count);
    for (size_t i = 0, at = 16; i < count; ++i) {
        snx_st_le64(data + at, inputs[i].size);
        memcpy(data + at + 8, inputs[i].data, inputs[i].size);
        at += 8 + inputs[i].size;
    }
    *size = total;
    return data;
}

SnxLinkInput* snx_objects_unpack(const unsigned char* data, size_t size, const char* name, size_t* count) {
    size_t at = 16;
    if (size < 16 || memcmp(data, "snx-objs", 8)) return NULL;
    size_t n = (size_t)snx_ld_le64(data + 8);
    if (!n || n > size / 8) return NULL;
    SnxLinkInput* inputs = (SnxLinkInput*)calloc(n, sizeof(*inputs));
    if (!inputs) return NULL;
    for (size_t i = 0; i < n; ++i) {
        uint64_t length;
        if (size - at < 8 || (length = snx_ld_le64(data + at)) > size - at - 8) {
            free(inputs);
            return NULL;
        }
        inputs[i].data = data + at + 8;
        inputs[i].size = (size_t)length;
        inputs[i].name = name;
        at += 8 + (size_t)length;
    }
    *count = n;
    return inputs;
}
