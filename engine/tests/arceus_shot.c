/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The screenshot's hash and file (src/nx/gpu/vk/shot.h) against the earlier code of surf_save, kept here as it was:
   for surfaces of several sizes the same hash and the same bytes. */
#include "nx/gpu/vk/shot.h"
#include <assert.h>
#include <string.h>

static uint64_t earlier_hash(const uint8_t* px, uint32_t w, uint32_t h) {
    uint64_t hash = 1469598103934665603ull;
    for (uint64_t i = 0; i < (uint64_t)w * h; i++) hash = snx_fnv_step(hash, snx_ld_le32(px + i * 4));
    return hash;
}
static int earlier_write(FILE* f, const uint8_t* px, uint32_t w, uint32_t h) {
    int ok = 1;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint64_t i = 0; i < (uint64_t)w * h && ok; i++) {
        uint8_t rgb[3] = {px[i * 4], px[i * 4 + 1], px[i * 4 + 2]};
        ok = fwrite(rgb, 1, 3, f) == 3;
    }
    return ok;
}
static size_t file_bytes(FILE* f, uint8_t* out, size_t size) {
    rewind(f);
    return fread(out, 1, size, f);
}
int main(void) {
    /* around the chunk of 1024 pixels too: one less, exactly one, one more, and several */
    static const uint32_t sizes[][2] = {{1, 1}, {3, 2}, {17, 5}, {64, 48}, {1920, 3}, {1023, 1}, {1024, 1}, {1025, 1},
                                        {32, 96}};
    for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        const uint32_t w = sizes[k][0], h = sizes[k][1];
        uint8_t* px = malloc((size_t)w * h * 4);
        for (size_t i = 0; i < (size_t)w * h * 4; i++) px[i] = (uint8_t)(i * 131 + k * 7 + (i >> 9));
        assert(shot_hash(px, w, h) == earlier_hash(px, w, h));
        FILE* a = tmpfile();
        FILE* b = tmpfile();
        assert(a && b);
        assert(shot_write_ppm(a, px, w, h) && earlier_write(b, px, w, h));
        fflush(a);
        fflush(b);
        const size_t size = (size_t)w * h * 3 + 64;
        uint8_t* fa = malloc(size);
        uint8_t* fb = malloc(size);
        const size_t na = file_bytes(a, fa, size), nb = file_bytes(b, fb, size);
        assert(na == nb && na > (size_t)w * h * 3 && !memcmp(fa, fb, na));
        fclose(a);
        fclose(b);
        free(fa);
        free(fb);
        free(px);
    }
    assert(shot_hash(NULL, 0, 0) == SHOT_HASH_BASIS);
    /* astra's note on d8c06cfb: the copy's size is checked before it is allocated */
    size_t n = 0;
    assert(shot_bytes(1920, 1080, &n) && n == (size_t)1920 * 1080 * 4);
    if (sizeof(size_t) == 4) assert(!shot_bytes(65536, 65536, &n));
    assert(shot_bytes(0, 0, &n) && n == 0);

    puts("arceus_shot: all checks passed");
    return 0;
}
