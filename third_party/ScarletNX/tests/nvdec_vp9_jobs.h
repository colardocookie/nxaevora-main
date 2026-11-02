/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* A libvpx VP9 test vector as the NVDEC jobs a player hands the engine for it, worked out by FFmpeg's VP9 decoder
   (nvdec_vp9_jobs.c), for the vectors test (nvdec_vp9_vectors.c) to feed nvdec.c */
#ifndef SNX_TEST_NVDEC_VP9_JOBS_H
#define SNX_TEST_NVDEC_VP9_JOBS_H
#include <stddef.h>
#include <stdint.h>

#define SNX_VP9J_SETUP    0x100u /* nvdec_vp9_pic_s */
#define SNX_VP9J_PROBS    0xEA0u /* nvdec_vp9EntropyProbs_t */
#define SNX_VP9J_SURFACES 16u

/* One frame of the vector, in decode order */
typedef struct {
    uint8_t setup[SNX_VP9J_SETUP], probs[SNX_VP9J_PROBS];
    uint32_t body, body_len; /* the frame after its two headers: an offset into the vector's data */
    uint32_t width, height;
    uint32_t surface, refs[3]; /* the surface its picture goes to, and Last's, Golden's and AltRef's */
    int existing;              /* show_existing_frame: no job, the picture on surface is shown again */
    int shown;                 /* show_frame */
    int image;                 /* the line of the MD5 list for the image libvpx hands out for it, -1 for none */
    uint8_t md5[16];           /* the reference decoder's picture, its three planes in turn */
} SnxVp9Job;

typedef struct {
    SnxVp9Job* jobs;
    uint8_t* data; /* the vector's file */
    uint32_t count, images, surfaces;
    uint32_t stride, rows; /* every surface's: the luma plane's bytes a row and rows, the chroma's half as many */
    uint32_t hidden, shown_dropped; /* frames never shown, shown frames libvpx hands no image out for */
} SnxVp9Vector;

/* The jobs of the vector at path (IVF or WebM), every image the reference decoder shows checked against libvpx's
   MD5 list at md5_path: 0, or -1 with the reason in err */
int snx_vp9j_load(const char* path, const char* md5_path, SnxVp9Vector* v, char* err, size_t cap);
void snx_vp9j_free(SnxVp9Vector* v);

#endif
