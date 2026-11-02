/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* src/common/utilities/endian.h: guest values are little-endian in memory on every host, the _be forms big-endian,
   and SNX_HOST_BE matches the host the test runs on (under qemu-ppc64, a big-endian one) */
#include "common/utilities/endian.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    static const unsigned char le[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    unsigned char b[8];
    uint32_t probe = 1;
    int host_be = *(unsigned char*)&probe == 0;

    assert(SNX_HOST_BE == host_be);
    assert(snx_ld_le16(le) == 0x0201u);
    assert(snx_ld_le32(le) == 0x04030201ul);
    assert(snx_ld_le64(le) == (((uint64_t)0x08070605ul << 32) | 0x04030201ul));
    assert(snx_ld_be16(le) == 0x0102u);
    assert(snx_ld_be32(le) == 0x01020304ul);

    memset(b, 0, sizeof b);
    snx_st_le64(b, snx_ld_le64(le));
    assert(!memcmp(b, le, 8));
    snx_st_le32(b, 0x0d0c0b0aul);
    assert(b[0] == 0x0a && b[3] == 0x0d && b[4] == 0x05);
    snx_st_le16(b + 6, 0x2211u);
    assert(b[6] == 0x11 && b[7] == 0x22);
    snx_st_be32(b, 0x0d0c0b0aul);
    assert(b[0] == 0x0d && b[3] == 0x0a);
    snx_st_be16(b, 0x2211u);
    assert(b[0] == 0x22 && b[1] == 0x11);

    /* the value conversion is its own inverse, and the identity on a little-endian host */
    assert(snx_le32(snx_le32(0x12345678ul)) == 0x12345678ul);
    {
        uint64_t v = ((uint64_t)0x01234567ul << 32) | 0x89abcdeful;
        assert(snx_le64(snx_le64(v)) == v);
    }
    assert(host_be ? snx_le16(0x1234u) == 0x3412u : snx_le16(0x1234u) == 0x1234u);
    printf("endian: %s host\n", host_be ? "big-endian" : "little-endian");
    return 0;
}
