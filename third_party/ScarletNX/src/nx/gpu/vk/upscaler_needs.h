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

/* The one feature structure the host accepts from an adapter: the compute shader derivatives features (the KHR
   and the NV extension share it), by its type number and its two bits. Any other type is refused: a core feature
   structure, a promoted alias such as the 16 bit float features, and anything that is no feature structure. */
#define SNX_NEEDS_DERIVATIVES 1000201000
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
/* the bits of that structure, by value: asked for by an adapter, had by a physical device, or turned on */
typedef struct {
    int have; /* the structure is there */
    int quads, linear;
} SnxNeedsBits;

enum {
    SNX_NEEDS_OK,
    SNX_NEEDS_TYPE,        /* a structure of a type the host does not accept */
    SNX_NEEDS_REPEATED,    /* the accepted type twice in one chain */
    SNX_NEEDS_LONG,        /* the chain does not end within SNX_NEEDS_NODES structures */
    SNX_NEEDS_BIT,         /* a bit the adapter asks for is not one the physical device has */
    SNX_NEEDS_NAME,        /* an extension the adapter names is not in the physical device's list */
    SNX_NEEDS_LIST,        /* the physical device's list of extensions was not read whole */
    SNX_NEEDS_ROOM,        /* more names than the host keeps, or a name too long to keep */
    SNX_NEEDS_OWNER,       /* a bit is asked for and VK_KHR_compute_shader_derivatives is not named */
    SNX_NEEDS_ADAPTER,     /* the record is of another adapter, or there is none */
    SNX_NEEDS_NOT_RECORDED /* an extension or a bit that is asked for is not in the record */
};

/* The adapter's chain, read only and never written or linked: the bits it asks for, copied by value. A chain
   without a structure is OK with nothing asked. The first fault is the answer. */
static inline int snx_needs_chain(const void* chain, SnxNeedsBits* asked) {
    int nodes = 0;
    *asked = (SnxNeedsBits){0, 0, 0};
    const unsigned char* at = chain;
    while (at) {
        int32_t type;
        const void* next;
        uint32_t quads, linear;
        if (++nodes > SNX_NEEDS_NODES) return SNX_NEEDS_LONG;
        memcpy(&type, at + offsetof(SnxNeedsNode, type), sizeof type);
        memcpy(&next, at + offsetof(SnxNeedsNode, next), sizeof next);
        if (type != SNX_NEEDS_DERIVATIVES) return SNX_NEEDS_TYPE;
        if (asked->have) return SNX_NEEDS_REPEATED;
        memcpy(&quads, at + offsetof(SnxNeedsDerivatives, quads), sizeof quads);
        memcpy(&linear, at + offsetof(SnxNeedsDerivatives, linear), sizeof linear);
        *asked = (SnxNeedsBits){1, quads != 0, linear != 0};
        at = next;
    }
    return SNX_NEEDS_OK;
}
/* each bit asked for against the host's own question to the physical device: one that is not there refuses */
static inline int snx_needs_bits(const SnxNeedsBits* asked, const SnxNeedsBits* has) {
    if (!asked->have) return SNX_NEEDS_OK;
    if (!has->have || (asked->quads && !has->quads) || (asked->linear && !has->linear)) return SNX_NEEDS_BIT;
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

/* An adapter that asks for a bit of the structure names VK_KHR_compute_shader_derivatives: a feature structure
   is not put into a device's chain without its extension enabled. The NV extension of the same structure is
   not taken as that: a configuration nobody examined. */
static inline int snx_needs_owner(const char* const* names, uint32_t count, const SnxNeedsBits* asked) {
    if (!asked->have) return SNX_NEEDS_OK;
    for (uint32_t i = 0; i < count; i++)
        if (names && names[i] && !strcmp(names[i], "VK_KHR_compute_shader_derivatives")) return SNX_NEEDS_OK;
    return SNX_NEEDS_OWNER;
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
} SnxNeedsRecord;
static inline int snx_needs_record(SnxNeedsRecord* r, const char* adapter, const char* const* names, uint32_t count,
                                   const SnxNeedsBits* on) {
    memset(r, 0, sizeof *r);
    if (!adapter || strlen(adapter) >= sizeof r->adapter || count > SNX_NEEDS_NAMES) return SNX_NEEDS_ROOM;
    for (uint32_t i = 0; i < count; i++)
        if (!names[i] || strlen(names[i]) > SNX_NEEDS_NAME_LENGTH) return SNX_NEEDS_ROOM;
    memcpy(r->adapter, adapter, strlen(adapter) + 1);
    for (uint32_t i = 0; i < count; i++) memcpy(r->name[i], names[i], strlen(names[i]) + 1);
    r->names = count;
    r->on = *on;
    r->made = 1;
    return SNX_NEEDS_OK;
}
/* The start of the upscaler asks the adapter again and holds its answer against the record: the same adapter,
   every extension it names in the record, and every bit it asks for on in the record. Anything else means the
   device was not made for what the adapter needs. */
static inline int snx_needs_met(const SnxNeedsRecord* r, const char* adapter, const char* const* names,
                                uint32_t count, const void* chain) {
    SnxNeedsBits asked;
    if (!r->made || !adapter || strcmp(r->adapter, adapter)) return SNX_NEEDS_ADAPTER;
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
    return SNX_NEEDS_OK;
}
/* a refusal in words, for the one line the switch prints */
static inline const char* snx_needs_text(int why) {
    switch (why) {
        case SNX_NEEDS_OK: return "met";
        case SNX_NEEDS_TYPE: return "its chain holds a structure of a type the host does not accept";
        case SNX_NEEDS_REPEATED: return "its chain holds the accepted structure twice";
        case SNX_NEEDS_LONG: return "its chain does not end within eight structures";
        case SNX_NEEDS_BIT: return "a feature bit it asks for is not one the physical device has";
        case SNX_NEEDS_NAME: return "an extension it names is not in the physical device's list";
        case SNX_NEEDS_LIST: return "the physical device's list of extensions could not be read whole";
        case SNX_NEEDS_ROOM: return "it names more than the host keeps, or a name too long to keep";
        case SNX_NEEDS_OWNER: return "it asks for a feature bit and does not name VK_KHR_compute_shader_derivatives";
        case SNX_NEEDS_ADAPTER: return "the game's device was not made with the needs of this adapter";
        default: return "it names an extension or a feature bit the game's device was not made with";
    }
}
/* 1 when an adapter names nothing: it then needs no record, and runs on the device as the game makes it */
static inline int snx_needs_none(uint32_t count, const void* chain) { return !count && !chain; }
#endif
