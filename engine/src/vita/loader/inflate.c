/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* RFC 1951's decoder: canonical Huffman codes decoded a bit at a time by their counts per length (as the RFC's own
   example does), which is fast enough for a VPK extracted once */
#include "vita/loader/inflate.h"
#include <stdint.h>
#include <string.h>

typedef struct {
    const unsigned char* in;
    size_t in_size, at;
    uint32_t bits;
    unsigned count;
    unsigned char* out;
    size_t out_size, written;
    bool bad;
} Stream;
typedef struct {
    uint16_t count[16];  /* codes of each length */
    uint16_t symbol[288]; /* the symbols by code */
} Huffman;

static unsigned bits(Stream* s, unsigned n) {
    while (s->count < n) {
        if (s->at == s->in_size) {
            s->bad = true;
            return 0;
        }
        s->bits |= (uint32_t)s->in[s->at++] << s->count;
        s->count += 8;
    }
    unsigned value = s->bits & ((1u << n) - 1);
    s->bits >>= n;
    s->count -= n;
    return value;
}
/* false for an over-subscribed set; an incomplete one is allowed (a single distance code) */
static bool build(Huffman* h, const unsigned char* lengths, unsigned n) {
    uint16_t offsets[16];
    memset(h->count, 0, sizeof h->count);
    for (unsigned i = 0; i < n; ++i) ++h->count[lengths[i]];
    h->count[0] = 0;
    int left = 1;
    for (unsigned len = 1; len < 16; ++len) {
        left = left * 2 - h->count[len];
        if (left < 0) return false;
    }
    offsets[1] = 0;
    for (unsigned len = 1; len < 15; ++len) offsets[len + 1] = (uint16_t)(offsets[len] + h->count[len]);
    for (unsigned i = 0; i < n; ++i)
        if (lengths[i]) h->symbol[offsets[lengths[i]]++] = (uint16_t)i;
    return true;
}
static int decode(Stream* s, const Huffman* h) {
    int code = 0, first = 0, index = 0;
    for (unsigned len = 1; len < 16; ++len) {
        code |= (int)bits(s, 1);
        if (s->bad) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    s->bad = true;
    return -1;
}
static const uint16_t length_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                         31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t distance_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                           193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static bool codes(Stream* s, const Huffman* lengths, const Huffman* distances) {
    for (;;) {
        int symbol = decode(s, lengths);
        if (symbol < 0) return false;
        if (symbol < 256) {
            if (s->written == s->out_size) return false;
            s->out[s->written++] = (unsigned char)symbol;
            continue;
        }
        if (symbol == 256) return true;
        symbol -= 257;
        if (symbol >= 29) return false;
        size_t length = length_base[symbol] + bits(s, length_extra[symbol]);
        int d = decode(s, distances);
        if (d < 0 || d >= 30) return false;
        size_t distance = distance_base[d] + bits(s, distance_extra[d]);
        if (s->bad || distance > s->written || length > s->out_size - s->written) return false;
        for (size_t i = 0; i < length; ++i, ++s->written) s->out[s->written] = s->out[s->written - distance];
    }
}
static bool fixed(Stream* s) {
    unsigned char lengths[288 + 30];
    unsigned i = 0;
    for (; i < 144; ++i) lengths[i] = 8;
    for (; i < 256; ++i) lengths[i] = 9;
    for (; i < 280; ++i) lengths[i] = 7;
    for (; i < 288; ++i) lengths[i] = 8;
    for (; i < 288 + 30; ++i) lengths[i] = 5;
    Huffman l, d;
    return build(&l, lengths, 288) && build(&d, lengths + 288, 30) && codes(s, &l, &d);
}
static bool dynamic(Stream* s) {
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    unsigned char lengths[288 + 32] = {0};
    unsigned nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->bad || nlen > 286 || ndist > 30) return false;
    for (unsigned i = 0; i < ncode; ++i) lengths[order[i]] = (unsigned char)bits(s, 3);
    Huffman code, l, d;
    if (s->bad || !build(&code, lengths, 19)) return false;
    unsigned at = 0;
    while (at < nlen + ndist) {
        int symbol = decode(s, &code);
        if (symbol < 0) return false;
        if (symbol < 16) {
            lengths[at++] = (unsigned char)symbol;
            continue;
        }
        unsigned repeat, value = 0;
        if (symbol == 16) {
            if (!at) return false;
            value = lengths[at - 1];
            repeat = 3 + bits(s, 2);
        } else
            repeat = symbol == 17 ? 3 + bits(s, 3) : 11 + bits(s, 7);
        if (s->bad || at + repeat > nlen + ndist) return false;
        while (repeat--) lengths[at++] = (unsigned char)value;
    }
    if (!lengths[256]) return false;
    return build(&l, lengths, nlen) && build(&d, lengths + nlen, ndist) && codes(s, &l, &d);
}
bool vita_inflate(const unsigned char* in, size_t in_size, unsigned char* out, size_t out_size) {
    Stream s = {.in = in, .in_size = in_size, .out = out, .out_size = out_size};
    unsigned last;
    do {
        last = bits(&s, 1);
        unsigned type = bits(&s, 2);
        if (s.bad) return false;
        if (type == 0) {
            s.bits = 0, s.count = 0; /* to the byte */
            if (s.in_size - s.at < 4) return false;
            unsigned length = s.in[s.at] | (unsigned)s.in[s.at + 1] << 8;
            unsigned check = s.in[s.at + 2] | (unsigned)s.in[s.at + 3] << 8;
            s.at += 4;
            if ((length ^ 0xffffu) != check || length > s.in_size - s.at || length > s.out_size - s.written)
                return false;
            memcpy(s.out + s.written, s.in + s.at, length);
            s.at += length, s.written += length;
        } else if (type == 1) {
            if (!fixed(&s)) return false;
        } else if (type == 2) {
            if (!dynamic(&s)) return false;
        } else
            return false;
    } while (!last);
    return s.written == out_size;
}
bool vita_zlib_inflate(const unsigned char* in, size_t in_size, unsigned char* out, size_t out_size) {
    /* CMF (deflate, window up to 32K), FLG without a preset dictionary, the check over both */
    if (in_size < 6 || (in[0] & 15) != 8 || (in[0] >> 4) > 7 || (in[1] & 0x20) || ((in[0] << 8) | in[1]) % 31)
        return false;
    return vita_inflate(in + 2, in_size - 2, out, out_size);
}
