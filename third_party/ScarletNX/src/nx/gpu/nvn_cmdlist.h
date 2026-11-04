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
    NVN_OP_SET_STENCIL,
    NVN_OP_SET_BLEND_COLOR,
    NVN_OP_SET_ALPHA_REF,
    NVN_OP_BIND_TEXTURE,
    NVN_OP_BIND_BUFFER,
    NVN_OP_BIND_STATE,
    NVN_OP_COUNT
} NvnOpCode;

#define NVN_OP_MAX_PAYLOAD   512u /* bytes; the largest payload is NvnOpRenderTargets */
#define NVN_OP_ALIGN         8u
#define NVN_RENDER_TARGETS_MAX 8u
#define NVN_VIEWPORTS_MAX    16u /* viewports and scissors, as the engine has */
#define NVN_CALL_DEPTH_MAX   8u  /* nested CALL ops at replay */
#define NVN_BIND_STAGE_MAX   5u  /* stages 0 to 5; compute is the last valid stage */
#define NVN_BIND_SLOT_MAX    128u /* bind slots of one stage */
#define NVN_UNIFORM_SLOT_MAX 32u /* the library masks slot+3 to 5 bits, so a higher slot would alias */
#define NVN_STORAGE_SLOT_MAX NVN_BIND_SLOT_MAX
#define NVN_VERTEX_SLOT_MAX  16u
#define NVN_XFB_SLOT_MAX     4u

/* a pool or a texture is named by address and generation (nvn_objtab) and read at replay */
typedef struct { uint64_t pool; uint32_t generation, reserved; } NvnOpPool;
typedef struct { uint32_t index; float x, y, width, height; } NvnOpViewport;
typedef struct { uint32_t index; int32_t x, y, width, height; } NvnOpScissor;
typedef struct { uint32_t index, mask; uint32_t color[4]; } NvnOpClearColor; /* the float bits as the game gave them */
typedef struct { uint32_t bits, reserved; } NvnOpBarrier;
typedef enum {
    NVN_STENCIL_REF = 0,
    NVN_STENCIL_VALUE_MASK,
    NVN_STENCIL_MASK
} NvnStencilKind;
/* face and value are raw: the replay masks them (face bit 0 front, bit 1 back). */
typedef struct { uint32_t which, face, value, reserved; } NvnOpStencil;
typedef struct { uint32_t color[4]; } NvnOpBlendColor; /* the float bits as the game gave them */
typedef struct { uint32_t bits, reserved; } NvnOpAlphaRef;
typedef struct { uint64_t handle; } NvnOpCall;
typedef struct { uint32_t stage, slot; uint64_t handle; } NvnOpBindTexture;
typedef struct { uint32_t kind, stage, slot, reserved; uint64_t addr, size; } NvnOpBindBuffer;
typedef struct { uint32_t kind, words; uint32_t w[NVN_STATE_WORDS_MAX]; } NvnOpBindState;
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
    NvnListSlot** slot; /* each slot is its own allocation, so a pointer from nvn_list_find stays valid while the list lives */
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
