/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The variant hash must stay byte-for-byte the cache's hash_runs: the AOT build stores the
   cache's hashes and the runtime recomputes them over the current guest code. */
#include "common/cpu/recompiler/variants.h"
#include <stdio.h>

static int failures;

static void check(int condition, const char* what) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static int words_at(void* user, SnxU64 address, SnxU32* word) {
    const SnxU32* words = (const SnxU32*)user;
    if (address == 0x1000) {
        *word = words[0];
        return 0;
    }
    if (address == 0x1004) {
        *word = words[1];
        return 0;
    }
    return -1;
}

static int missing(void* user, SnxU64 address, SnxU32* word) {
    (void)user;
    (void)address;
    (void)word;
    return -1;
}

int main(void) {
    static const SnxU32 code[] = {0x91000400u, 0xD65F03C0u};
    const SnxVariantRun run = {0x1000, 2};
    SnxU64 h1 = 1, h2 = 2;

    check(snx_variant_hash(words_at, (void*)code, &run, 1, &h1, &h2) == 0, "hash succeeds");
    check(h1 == 0x36AE1F9CA1AEB4ADULL, "first hash matches hash_runs");
    check(h2 == 0x64112330FD33A024ULL, "second hash matches hash_runs");

    h1 = 1;
    h2 = 2;
    check(snx_variant_hash(missing, NULL, &run, 1, &h1, &h2) != 0, "missing word fails");
    check(h1 == 0 && h2 == 0, "failure leaves zero hashes");

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("variants: ok");
    return 0;
}
