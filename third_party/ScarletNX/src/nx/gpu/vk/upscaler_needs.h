/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_VK_UPSCALER_NEEDS_H
#define SNX_VK_UPSCALER_NEEDS_H
/* What the host makes of an upscaler adapter's needs of a device (upscaler.h), for the game's device. Pure: no
   Vulkan header, so that it is tested on any host. A Vulkan structure is read here as bytes: its type, its next
   pointer and the two bits are copied out at the offsets of the stand-ins below into values of our own. No
   object of the adapter's is read through a type of ours. upscaler_host.c asserts the offsets against Vulkan's. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The feature structures the host accepts from an adapter: the compute shader derivatives features (the KHR and
   the NV extension share it), by its type number and its two bits, and since ABI 5 the mutable descriptor type
   features (XeSS), by its one bit. Any other type is refused: a core feature structure, a promoted alias such as
   the 16 bit float features, and anything that is no feature structure. */
#define SNX_NEEDS_DERIVATIVES 1000201000
#define SNX_NEEDS_MUTABLE     1000351000
/* The walk's guard against a chain that does not end. With one accepted type it is never the first refusal: a
   second structure is refused by its type or as a repeat before the count gets there. */
#define SNX_NEEDS_NODES 8
typedef struct {
    int32_t type;
    const void* next;
} SnxNeedsNode;
typedef struct {
    int32_t type;
    const void* next;
    uint32_t quads, linear; /* computeDerivativeGroupQuads, computeDerivativeGroupLinear */
} SnxNeedsDerivatives;
typedef struct {
    int32_t type;
    const void* next;
    uint32_t on; /* mutableDescriptorType */
} SnxNeedsMutable;
/* the bits of those structures, by value: asked for by an adapter, had by a physical device, or turned on */
typedef struct {
    int have; /* the derivatives structure is there */
    int quads, linear;
    int mutable_have, mutable_on; /* the mutable descriptor structure is there, and its bit */
} SnxNeedsBits;

enum {
    SNX_NEEDS_OK,
    SNX_NEEDS_TYPE,        /* a structure of a type the host does not accept */
    SNX_NEEDS_REPEATED,    /* an accepted type twice in one chain */
    SNX_NEEDS_LONG,        /* the chain does not end within SNX_NEEDS_NODES structures */
    SNX_NEEDS_BIT,         /* a bit the adapter asks for is not one the physical device has */
    SNX_NEEDS_NAME,        /* an extension the adapter names is not in the physical device's list */
    SNX_NEEDS_LIST,        /* the physical device's list of extensions was not read whole */
    SNX_NEEDS_ROOM,        /* more names than the host keeps, or a name too long to keep */
    SNX_NEEDS_OWNER,       /* a structure is asked for and the extension it belongs to is not named */
    SNX_NEEDS_CORE_UNKNOWN, /* a core feature bit is asked for that the host does not know */
    SNX_NEEDS_CORE_BIT,     /* a core feature bit that is asked for is not one the physical device has */
    SNX_NEEDS_ADAPTER,     /* the record is of another adapter, or there is none */
    SNX_NEEDS_NOT_RECORDED, /* an extension or a bit that is asked for is not in the record */
    SNX_NEEDS_INSTANCE      /* an instance extension it names was not enabled on the instance (DL2) */
};

/* The adapter's chain, read only and never written or linked: the bits it asks for, copied by value. A chain
   without a structure is OK with nothing asked. The first fault is the answer. */
static inline int snx_needs_chain(const void* chain, SnxNeedsBits* asked) {
    int nodes = 0;
    *asked = (SnxNeedsBits){0, 0, 0, 0, 0};
    const unsigned char* at = chain;
    while (at) {
        int32_t type;
        const void* next;
        uint32_t quads, linear, on;
        if (++nodes > SNX_NEEDS_NODES) return SNX_NEEDS_LONG;
        memcpy(&type, at + offsetof(SnxNeedsNode, type), sizeof type);
        memcpy(&next, at + offsetof(SnxNeedsNode, next), sizeof next);
        if (type == SNX_NEEDS_DERIVATIVES) {
            if (asked->have) return SNX_NEEDS_REPEATED;
            memcpy(&quads, at + offsetof(SnxNeedsDerivatives, quads), sizeof quads);
            memcpy(&linear, at + offsetof(SnxNeedsDerivatives, linear), sizeof linear);
            asked->have = 1;
            asked->quads = quads != 0;
            asked->linear = linear != 0;
        } else if (type == SNX_NEEDS_MUTABLE) {
            if (asked->mutable_have) return SNX_NEEDS_REPEATED;
            memcpy(&on, at + offsetof(SnxNeedsMutable, on), sizeof on);
            asked->mutable_have = 1;
            asked->mutable_on = on != 0;
        } else
            return SNX_NEEDS_TYPE;
        at = next;
    }
    return SNX_NEEDS_OK;
}
/* each bit asked for against the host's own question to the physical device: one that is not there refuses */
static inline int snx_needs_bits(const SnxNeedsBits* asked, const SnxNeedsBits* has) {
    if (asked->have && (!has->have || (asked->quads && !has->quads) || (asked->linear && !has->linear)))
        return SNX_NEEDS_BIT;
    if (asked->mutable_have && (!has->mutable_have || (asked->mutable_on && !has->mutable_on))) return SNX_NEEDS_BIT;
    return SNX_NEEDS_OK;
}
/* Each name the adapter gives against the physical device's list. whole: that list was read to its end with
   success. A list read in part, or not read, refuses whatever it holds. */
static inline int snx_needs_names(const char* const* names, uint32_t count, const char* const* device,
                                  uint32_t device_count, int whole) {
    if (!whole) return SNX_NEEDS_LIST;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t k = 0;
        if (!names || !names[i]) return SNX_NEEDS_NAME;
        while (k < device_count && strcmp(device[k], names[i])) k++;
        if (k == device_count) return SNX_NEEDS_NAME;
    }
    return SNX_NEEDS_OK;
}

/* An adapter that hands a structure names its extension: VK_KHR_compute_shader_derivatives for the derivatives,
   VK_EXT_mutable_descriptor_type for the mutable descriptor type. A feature structure is not put into a device's
   chain without its extension enabled. The NV extension of the derivatives' structure is not taken as that: a
   configuration nobody examined. */
static inline int snx_needs_named(const char* const* names, uint32_t count, const char* name) {
    for (uint32_t i = 0; i < count; i++)
        if (names && names[i] && !strcmp(names[i], name)) return 1;
    return 0;
}
static inline int snx_needs_owner(const char* const* names, uint32_t count, const SnxNeedsBits* asked) {
    if (asked->have && !snx_needs_named(names, count, "VK_KHR_compute_shader_derivatives")) return SNX_NEEDS_OWNER;
    if (asked->mutable_have && !snx_needs_named(names, count, "VK_EXT_mutable_descriptor_type")) return SNX_NEEDS_OWNER;
    return SNX_NEEDS_OK;
}

/* Needs of core feature bits are no structure at all: a plain field of the adapter's answer. Defined: separate
   layouts for the depth and the stencil aspect and buffer device addresses (1.2), and since ABI 5 storage image
   writes without a format (1.0), 8 bit integers in shaders (1.2) and integer dot products (1.3). Whoever makes a
   device turns them on in its own structures after its own question to the physical device, whose answer is has.
   A bit the host does not know refuses, and so does a known one the physical device does not have. */
#define SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL 1u
#define SNX_NEEDS_CORE_BUFFER_DEVICE_ADDRESS 2u
#define SNX_NEEDS_CORE_STORAGE_WRITE_WITHOUT_FORMAT 4u
#define SNX_NEEDS_CORE_SHADER_INT8 8u
#define SNX_NEEDS_CORE_INTEGER_DOT_PRODUCT 16u
#define SNX_NEEDS_CORE_KNOWN                                                                                         \
    (SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL | SNX_NEEDS_CORE_BUFFER_DEVICE_ADDRESS |                                 \
     SNX_NEEDS_CORE_STORAGE_WRITE_WITHOUT_FORMAT | SNX_NEEDS_CORE_SHADER_INT8 | SNX_NEEDS_CORE_INTEGER_DOT_PRODUCT)
static inline int snx_needs_core(uint32_t asked, uint32_t has) {
    if (asked & ~SNX_NEEDS_CORE_KNOWN) return SNX_NEEDS_CORE_UNKNOWN;
    if (asked & ~has) return SNX_NEEDS_CORE_BIT;
    return SNX_NEEDS_OK;
}
/* The instance extensions an adapter names, against the ones the instance was made with for it (DL2): every one
   of them, else SNX_NEEDS_INSTANCE */
static inline int snx_needs_instance(const char* const* asked, uint32_t count, const char* const* made,
                                     uint32_t made_count) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t k = 0;
        if (!asked || !asked[i]) return SNX_NEEDS_INSTANCE;
        while (k < made_count && (!made[k] || strcmp(made[k], asked[i]))) k++;
        if (k == made_count) return SNX_NEEDS_INSTANCE;
    }
    return SNX_NEEDS_OK;
}
/* The bit as a device's own 1.2 structure gets it for one creation: on when the additions are taken and the
   adapter asks for it, else what the maker had. So the creation without the additions has the maker's own. */
static inline int snx_needs_core_bit(int own, int accepted, uint32_t asked, uint32_t bit) {
    return accepted && (asked & bit) ? 1 : own;
}

/* What a device is made with: the count of its extension names and the head of its feature chain. The game's own
   pair is never changed in place. The adapter's additions are made on a copy, and when they are refused or the
   device cannot be made with them, the device is made from the game's own pair. */
typedef struct {
    uint32_t count;
    const void* head;
    int augmented;
} SnxNeedsPlan;
static inline SnxNeedsPlan snx_needs_plan(uint32_t game_count, const void* game_head, int accepted,
                                          uint32_t with_count, const void* with_head) {
    const SnxNeedsPlan game = {game_count, game_head, 0}, with = {with_count, with_head, 1};
    return accepted ? with : game;
}

/* What a device was made with for an adapter, by value: the adapter's name, every extension it named and got
   (also one the game's own list already had), and the bits that were turned on. Written only after a device was
   made with the additions. made 0: none, as on the fallback. */
#define SNX_NEEDS_NAMES 8
#define SNX_NEEDS_NAME_LENGTH 63
typedef struct {
    int made;
    char adapter[32];
    uint32_t names;
    char name[SNX_NEEDS_NAMES][SNX_NEEDS_NAME_LENGTH + 1];
    SnxNeedsBits on;
    uint32_t core; /* the core feature bits that were turned on for the adapter */
} SnxNeedsRecord;
static inline int snx_needs_record(SnxNeedsRecord* r, const char* adapter, const char* const* names, uint32_t count,
                                   const SnxNeedsBits* on, uint32_t core) {
    memset(r, 0, sizeof *r);
    if (!adapter || strlen(adapter) >= sizeof r->adapter || count > SNX_NEEDS_NAMES) return SNX_NEEDS_ROOM;
    for (uint32_t i = 0; i < count; i++)
        if (!names[i] || strlen(names[i]) > SNX_NEEDS_NAME_LENGTH) return SNX_NEEDS_ROOM;
    memcpy(r->adapter, adapter, strlen(adapter) + 1);
    for (uint32_t i = 0; i < count; i++) memcpy(r->name[i], names[i], strlen(names[i]) + 1);
    r->names = count;
    r->on = *on;
    r->core = core;
    r->made = 1;
    return SNX_NEEDS_OK;
}
/* The start of the upscaler asks the adapter again and holds its answer against the record: the same adapter,
   every extension it names in the record, and every bit it asks for on in the record. Anything else means the
   device was not made for what the adapter needs. */
static inline int snx_needs_met(const SnxNeedsRecord* r, const char* adapter, const char* const* names,
                                uint32_t count, const void* chain, uint32_t core) {
    SnxNeedsBits asked;
    if (!r->made || !adapter || strcmp(r->adapter, adapter)) return SNX_NEEDS_ADAPTER;
    if (core & ~SNX_NEEDS_CORE_KNOWN) return SNX_NEEDS_CORE_UNKNOWN;
    const int walked = snx_needs_chain(chain, &asked);
    if (walked != SNX_NEEDS_OK) return walked;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t k = 0;
        if (!names || !names[i]) return SNX_NEEDS_NOT_RECORDED;
        while (k < r->names && strcmp(r->name[k], names[i])) k++;
        if (k == r->names) return SNX_NEEDS_NOT_RECORDED;
    }
    if (asked.have && (!r->on.have || (asked.quads && !r->on.quads) || (asked.linear && !r->on.linear)))
        return SNX_NEEDS_NOT_RECORDED;
    if (asked.mutable_have && (!r->on.mutable_have || (asked.mutable_on && !r->on.mutable_on)))
        return SNX_NEEDS_NOT_RECORDED;
    if (core & ~r->core) return SNX_NEEDS_NOT_RECORDED; /* a core bit asked for that was not turned on */
    return SNX_NEEDS_OK;
}
/* a refusal in words, for the one line the switch prints */
static inline const char* snx_needs_text(int why) {
    switch (why) {
        case SNX_NEEDS_OK: return "met";
        case SNX_NEEDS_TYPE: return "its chain holds a structure of a type the host does not accept";
        case SNX_NEEDS_REPEATED: return "its chain holds an accepted structure twice";
        case SNX_NEEDS_LONG: return "its chain does not end within eight structures";
        case SNX_NEEDS_BIT: return "a feature bit it asks for is not one the physical device has";
        case SNX_NEEDS_NAME: return "an extension it names is not in the physical device's list";
        case SNX_NEEDS_LIST: return "the physical device's list of extensions could not be read whole";
        case SNX_NEEDS_ROOM: return "it names more than the host keeps, or a name too long to keep";
        case SNX_NEEDS_OWNER: return "it hands a feature structure and does not name the extension it belongs to";
        case SNX_NEEDS_CORE_UNKNOWN: return "it asks for a core feature bit the host does not know";
        case SNX_NEEDS_CORE_BIT: return "it asks for a core feature bit that the physical device does not have";
        case SNX_NEEDS_ADAPTER: return "the game's device was not made with the needs of this adapter";
        case SNX_NEEDS_INSTANCE: return "it names an instance extension the game's instance was not made with";
        default: return "it names an extension or a feature bit the game's device was not made with";
    }
}
/* 1 when an adapter names nothing, no extension, no structure and no core bit: it then needs no record, and
   runs on the device as the game makes it. A core bit alone is a need. */
static inline int snx_needs_none(uint32_t count, const void* chain, uint32_t core) {
    return !count && !chain && !core;
}
#endif
