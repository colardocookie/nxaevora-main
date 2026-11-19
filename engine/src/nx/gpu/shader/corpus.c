/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The shader corpus (shader_corpus): one file per translated program, <dir>/<key>_<stage>.snxs, holding a header,
   the ShaderInfo the translation started from and the program's code, for tools/snx_shaderc.c. */
#include "nx/gpu/shader/shader.h"
#include "common/utilities/common.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CORPUS_VERSION 1u
#define CORPUS_MAX     (1u << 20) /* the largest program window a file may claim */
/* the ShaderInfo before txq_fixed, with its padding: a corpus saved then still loads */
#define CORPUS_INFO_OLD                                                                                              \
    ((offsetof(ShaderInfo, txq_fixed) + _Alignof(ShaderInfo) - 1) / _Alignof(ShaderInfo) * _Alignof(ShaderInfo))
_Static_assert(CORPUS_INFO_OLD < sizeof(ShaderInfo), "the older record is shorter");
/* and the ShaderInfo before tex_txq_mask and tex_offset_size_mask (D3-2 revision 4) */
#define CORPUS_INFO_MID                                                                                              \
    ((offsetof(ShaderInfo, tex_txq_mask) + _Alignof(ShaderInfo) - 1) / _Alignof(ShaderInfo) * _Alignof(ShaderInfo))
/* and the one with tex_txq_mask before tex_offset_size_mask (767cb91e): its mask is kept */
#define CORPUS_INFO_TXQ                                                                                              \
    ((offsetof(ShaderInfo, tex_offset_size_mask) + _Alignof(ShaderInfo) - 1) / _Alignof(ShaderInfo) *              \
     _Alignof(ShaderInfo))
_Static_assert(CORPUS_INFO_OLD < CORPUS_INFO_MID && CORPUS_INFO_MID < CORPUS_INFO_TXQ &&
                   CORPUS_INFO_TXQ < sizeof(ShaderInfo),
               "the records grow");

typedef struct {
    char magic[4]; /* "SNXS" */
    u32 version, info_size, stage, len, max_bytes;
} CorpusHeader;

static void corpus_header(CorpusHeader* h, u32 len, u32 max_bytes, int stage) {
    memset(h, 0, sizeof *h);
    memcpy(h->magic, "SNXS", 4);
    h->version = CORPUS_VERSION;
    h->info_size = (u32)sizeof(ShaderInfo);
    h->stage = (u32)stage;
    h->len = len;
    h->max_bytes = max_bytes;
}

/* whether path already holds this program whole: a process that died while writing leaves a short file */
static int corpus_complete(const char* path, const CorpusHeader* want) {
    CorpusHeader have;
    long size;
    int ok;
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    ok = fread(&have, 1, sizeof have, f) == sizeof have && !memcmp(&have, want, sizeof have) &&
         !fseek(f, 0, SEEK_END) && (size = ftell(f)) >= 0 &&
         (unsigned long)size == sizeof have + sizeof(ShaderInfo) + want->len;
    fclose(f);
    return ok;
}

int shader_corpus_save(const char* dir, const u8* code, u32 len, u32 max_bytes, int stage, const ShaderInfo* si,
                       u64 key) {
    CorpusHeader h;
    char path[1024];
    FILE* f;
    int ok;
    if (!dir || !code || !si || len > max_bytes || stage < ST_VS || stage > ST_CS) return 0;
    corpus_header(&h, len, max_bytes, stage);
    if (snprintf(path, sizeof path, "%s/%016llx_%d.snxs", dir, (unsigned long long)key, stage) >= (int)sizeof path)
        return 0;
    snx_path_native(path);
    if (corpus_complete(path, &h)) return 1;
    f = fopen(path, "wb");
    if (!f) return 0;
    ok = fwrite(&h, 1, sizeof h, f) == sizeof h && fwrite(si, 1, sizeof *si, f) == sizeof *si &&
         fwrite(code, 1, len, f) == len;
    if (fclose(f)) ok = 0;
    if (!ok) remove(path); /* a cut-off file would only be rejected later */
    return ok;
}

/* The capture holds the length the runtime found, with the jump tables of its constant buffers, which can't be read
   offline: *max_bytes is that length, so a scan there doesn't run past it. */
u8* shader_corpus_load(const char* path, u32* max_bytes, int* stage, ShaderInfo* si) {
    CorpusHeader h;
    ShaderInfo info;
    u8* code = NULL;
    FILE* f;
    if (!path || !max_bytes || !stage || !si) return NULL;
    f = fopen(path, "rb");
    if (!f) return NULL;
    memset(&info, 0, sizeof info);
    /* an older record is read whole and only then are the new fields zeroed: its padding may lie where they are */
    if (fread(&h, 1, sizeof h, f) == sizeof h && !memcmp(h.magic, "SNXS", 4) && h.version == CORPUS_VERSION &&
        (h.info_size == sizeof(ShaderInfo) || h.info_size == CORPUS_INFO_TXQ || h.info_size == CORPUS_INFO_MID ||
         h.info_size == CORPUS_INFO_OLD) &&
        h.stage <= ST_CS &&
        h.len <= h.max_bytes && h.max_bytes <= CORPUS_MAX && fread(&info, 1, h.info_size, f) == h.info_size) {
        if (h.info_size == CORPUS_INFO_OLD) memset(info.txq_fixed, 0, sizeof info.txq_fixed);
        if (h.info_size != sizeof(ShaderInfo)) info.tex_offset_size_mask = 0;
        if (h.info_size != sizeof(ShaderInfo) && h.info_size != CORPUS_INFO_TXQ) info.tex_txq_mask = 0;
        code = (u8*)calloc(h.max_bytes ? h.max_bytes : 1, 1);
        if (code && fread(code, 1, h.len, f) != h.len) {
            free(code);
            code = NULL;
        }
    }
    fclose(f);
    if (!code) return NULL;
    *si = info;
    *max_bytes = h.len;
    *stage = (int)h.stage;
    return code;
}
