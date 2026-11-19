/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The body of picture.comp: a decoded picture's planes (staged as FFmpeg gives them, rows of exactly their width: Y
   width x height, U and V width/2 x height/2) laid out as sceAvcdecDecode writes its picture into the game's buffer,
   one 32-bit word of it an invocation. GLSL that is also C++ (tests/glsl/glsl.hpp): tests/glsl/vita_picture_glsl.cpp
   runs it against a byte by byte reference.

   The layout: Y in rows of pitch bytes, height of them; then the chroma, height/2 rows: interleaved in rows of pitch
   bytes, V first when vu (SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER, which games sample as YVU420P2), or U's plane
   then V's in rows of pitch/2 (YUV420_RASTER). The bytes of a row past the picture are zero. pitch is a multiple of
   16 (the decoder's frames are 16-aligned), so a word never straddles two rows of either plane: its row and column
   are computed once. */

uint staged(uint at) { return (src[at >> 2u] >> ((at & 3u) << 3u)) & 0xffu; }

void main() {
    uint w = gl_GlobalInvocationID.x;
    if (w >= words) return;
    uint o = w << 2u, v = 0u;
    if (o < luma) { /* Y */
        uint row = o / pitch, x = o - row * pitch;
        for (uint b = 0u; b < 4u; b++)
            if (x + b < width) v |= staged(row * width + x + b) << (b << 3u);
    } else if (packed != 0u) { /* VU (or UV) pairs */
        uint c = o - luma, row = c / pitch, x = c - row * pitch, cw = width >> 1u;
        for (uint b = 0u; b < 4u; b++) {
            uint col = x + b;
            if (col >= width) continue;
            /* the pair's first byte is V's when vu */
            uint plane = ((col & 1u) ^ vu) != 0u ? v_at : u_at;
            v |= staged(plane + row * cw + (col >> 1u)) << (b << 3u);
        }
    } else { /* U's plane, then V's */
        uint cpitch = pitch >> 1u, cplane = cpitch * (height >> 1u), c = o - luma;
        uint plane = c >= cplane ? v_at : u_at;
        if (c >= cplane) c -= cplane;
        uint row = c / cpitch, x = c - row * cpitch, cw = width >> 1u;
        for (uint b = 0u; b < 4u; b++)
            if (x + b < cw) v |= staged(plane + row * cw + x + b) << (b << 3u);
    }
    dst[w] = v;
}
