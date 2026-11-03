/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_CMDLIST_H
#define SNX_NVN_CMDLIST_H
#include "nx/gpu/nvn_params.h"
#include <stddef.h>
#include <stdint.h>

/* The host's command lists (docs/nvn-design.md, "3c Host design decisions", decisions 3 to 5). A command buffer records
   ops into an NvnOpList. EndRecording publishes the list in an NvnListStore and gives the game a handle. The list is
   immutable after that. Submit and Call hold a reference, so a list lives until its last reference goes. The module
   has no locking: the owner serializes the calls, and a reader of a list holds a reference. */

typedef enum {
    NVN_OP_NONE = 0,
    NVN_OP_SET_TEXTURE_POOL,
    NVN_OP_SET_SAMPLER_POOL,
    NVN_OP_SET_RENDER_TARGETS,
    NVN_OP_SET_VIEWPORT,
    NVN_OP_SET_SCISSOR,
    NVN_OP_CLEAR_COLOR,
    NVN_OP_BARRIER,
    NVN_OP_CALL,
    NVN_OP_COUNT
} NvnOpCode;

#define NVN_OP_MAX_PAYLOAD   512u /* bytes; the largest payload is NvnOpRenderTargets */
#define NVN_OP_ALIGN         8u
#define NVN_RENDER_TARGETS_MAX 8u
#define NVN_VIEWPORTS_MAX    16u /* viewports and scissors, as the engine has */
#define NVN_CALL_DEPTH_MAX   8u  /* nested CALL ops at replay */

/* a pool or a texture is named by address and generation (nvn_objtab) and read at replay */
typedef struct { uint64_t pool; uint32_t generation, reserved; } NvnOpPool;
typedef struct { uint32_t index; float x, y, width, height; } NvnOpViewport;
typedef struct { uint32_t index; int32_t x, y, width, height; } NvnOpScissor;
typedef struct { uint32_t index, mask; uint32_t color[4]; } NvnOpClearColor; /* the float bits as the game gave them */
typedef struct { uint32_t bits, reserved; } NvnOpBarrier;
typedef struct { uint64_t handle; } NvnOpCall;
typedef struct {
    uint64_t texture; /* 0 for no target */
    uint32_t generation, has_view;
    NvnTextureView view; /* copied at record time, valid when has_view */
} NvnOpTarget;
typedef struct {
    uint32_t count, reserved;
    NvnOpTarget color[NVN_RENDER_TARGETS_MAX];
    NvnOpTarget depth;
} NvnOpRenderTargets;

typedef struct { uint32_t code, bytes; } NvnOpHeader; /* bytes: the payload size, a multiple of NVN_OP_ALIGN */
typedef struct {
    uint8_t* data;
    size_t used, cap;
    uint32_t ops;
} NvnOpList;

/* Appends one op; the payload is padded with zero bytes to NVN_OP_ALIGN. 0 when bytes is over NVN_OP_MAX_PAYLOAD, the
   code is not an op code, or memory is out of. The list is unchanged then. */
int nvn_oplist_append(NvnOpList* list, NvnOpCode code, const void* payload, uint32_t bytes);
/* The op at *pos, then *pos moves to the next one. 0 at the end of the list. */
int nvn_oplist_next(const NvnOpList* list, size_t* pos, NvnOpHeader* header, const void** payload);
void nvn_oplist_free(NvnOpList* list);

typedef struct {
    NvnOpList list;
    uint32_t generation, refs; /* refs 0: the slot is free */
} NvnListSlot;
typedef struct {
    NvnListSlot* slot;
    uint32_t count, cap;
    uint32_t* free_index;
    uint32_t free_count, free_cap;
    uint32_t next_generation;
    uint32_t leaked; /* references that an out of memory stopped from going (the lists stay) */
} NvnListStore;

/* Takes the memory of *list (it is empty after) and makes the handle, with one reference. 0 when memory is out of; *list
   is unchanged then. A handle is never 0, has bit 0 clear, and names the slot and a generation. */
uint64_t nvn_list_publish(NvnListStore* store, NvnOpList* list);
/* The list of a live handle, NULL for any other value. */
const NvnOpList* nvn_list_find(const NvnListStore* store, uint64_t handle);
/* 1 and one more reference for a live handle, else 0. */
int nvn_list_ref(NvnListStore* store, uint64_t handle);
/* One reference less. The last one frees the list and gives up the references of its CALL ops. A handle that is not
   live is ignored. */
void nvn_list_unref(NvnListStore* store, uint64_t handle);
uint32_t nvn_list_live(const NvnListStore* store);
void nvn_list_store_free(NvnListStore* store);

#endif
