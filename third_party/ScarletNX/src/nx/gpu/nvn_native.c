/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* nvn_native=1: the host answers the NVN functions that the game resolves through nvnBootstrapLoader and
   nvnDeviceGetProcAddress. Each resolved address gets a host handler in its dispatch slot, so the game's NVN library
   never runs for a call through the table. A function without a handler gets the missing-function handler: it logs the
   first call and returns 0, so one run lists every missing function (docs/nvn-design.md, 2-3 and 2-6).
   The loader hooks are the ones of gpu/nvn_trace.c, one level below it: nvn_native_init runs first, and the trace
   wraps what the slots then hold, so it counts the host calls too. */
#include "nx/gpu/nvn_native.h"
#include "nx/gpu/nvn_trace.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/options.h"
#include "nx/runtime/rtapi.h"
#include "nx/hle/kernel/kernel.h"
#include "nx/memory/mem.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include <stdlib.h>
#include <string.h>

/* The host handlers by NVN function name. Later steps add their entries here (G1 to G5 of docs/nvn-design.md, 2-6). */
typedef struct {
    const char* name;
    GuestFn handler;
} NativeProc;
static const NativeProc native_procs[] = {
    {NULL, NULL},
};

typedef struct {
    u64 addr, calls;
    GuestFn handler;
    int missing; /* the handler is the missing-function handler */
    char name[128];
} NativeSlot;
#define NATIVE_SLOTS 1024 /* the game resolves 523 addresses */
static NativeSlot slots[NATIVE_SLOTS];
static u32 nslots;
/* Registration takes the lock and publishes a slot (addr, release ordering) once it is filled in. Calls read the slots
   without the lock. */
static PMutex slots_lock = PMUTEX_INIT;
static GuestFn bootstrap_original, getproc_original;
static u64 getproc_addr; /* the address of nvnDeviceGetProcAddress, hooked */
static int active;
static void getproc_native(CPU* c, u64 entry);

static u32 slot_hash(u64 addr) { return (u32)((addr >> 2) * 2654435761u) & (NATIVE_SLOTS - 1); }
static NativeSlot* slot_find(u64 addr) {
    for (u32 n = 0, i = slot_hash(addr); n < NATIVE_SLOTS; n++, i = (i + 1) & (NATIVE_SLOTS - 1)) {
        u64 a = __atomic_load_n(&slots[i].addr, __ATOMIC_ACQUIRE);
        if (a == addr) return &slots[i];
        if (!a) return NULL;
    }
    return NULL;
}

static void missing_handler(CPU* c, u64 entry) {
    NativeSlot* s = slot_find(entry);
    if (!s) fatal("nvn-native: the missing-function handler runs for an address it never registered (%llx)\n",
                  (unsigned long long)entry);
    if (!__atomic_fetch_add(&s->calls, 1, __ATOMIC_RELAXED)) /* the first call: what the game asked for */
        LOG("nvn-native: not implemented: %s (x0=%llx x1=%llx x2=%llx x3=%llx), returns 0\n", s->name,
            (unsigned long long)c->x[0], (unsigned long long)c->x[1], (unsigned long long)c->x[2],
            (unsigned long long)c->x[3]);
    c->x[0] = 0;
}

static GuestFn handler_for(const char* name, int* missing) {
    for (const NativeProc* p = native_procs; p->name; p++)
        if (!strcmp(p->name, name)) {
            *missing = 0;
            return p->handler;
        }
    *missing = 1;
    return missing_handler;
}

/* The handler of the function that the game resolved as name at addr */
static void install(const char* name, u64 addr) {
    if (addr < g_code_lo || addr >= g_code_hi || (addr & 3)) {
        LOG_LIMIT(8, "nvn-native: %s resolved to %llx, which is not a recompiled function\n", name,
                  (unsigned long long)addr);
        return;
    }
    pm_lock(&slots_lock);
    NativeSlot* s = slot_find(addr);
    if (addr == getproc_addr) { /* the lookup function keeps its hook */
        pm_unlock(&slots_lock);
        return;
    }
    if (s) {
        /* identical functions folded to one address: their calls count under the first name */
        if (strcmp(s->name, name)) LOG_LIMIT(8, "nvn-native: %s shares its address with %s\n", name, s->name);
    } else if (nslots < NATIVE_SLOTS) {
        u32 i = slot_hash(addr);
        while (slots[i].addr) i = (i + 1) & (NATIVE_SLOTS - 1);
        s = &slots[i];
        memcpy(s->name, name, strnlen(name, sizeof s->name - 1) + 1);
        s->handler = handler_for(s->name, &s->missing);
        __atomic_store_n(&s->addr, addr, __ATOMIC_RELEASE);
        nslots++;
        __atomic_store_n(&g_disp[(addr - g_code_lo) / 4], s->handler, __ATOMIC_RELEASE);
    } else {
        fatal("nvn-native: more than %u resolved NVN functions\n", NATIVE_SLOTS);
    }
    pm_unlock(&slots_lock);
}

/* nvnDeviceGetProcAddress(device, name). The library's own function runs for the lookup: it returns the address, and
   the host puts its handler there. Open (docs/nvn-design.md, 2-3, item 6): if it reads the device object, which the
   library never initialized in this mode. The first run shows it. */
static void getproc_native(CPU* c, u64 entry) {
    char name[128];
    int valid = nvn_guest_name(c->x[1], name);
    GuestFn original = __atomic_load_n(&getproc_original, __ATOMIC_ACQUIRE);
    if (!original) fatal("nvn-native: the nvnDeviceGetProcAddress hook runs before its original is known\n");
    original(c, entry);
    if (valid && c->x[0]) install(name, c->x[0]);
}

static void bootstrap_native(CPU* c, u64 entry) {
    char name[128];
    int valid = nvn_guest_name(c->x[0], name); /* nvnBootstrapLoader(name) */
    bootstrap_original(c, entry);
    if (!valid || !c->x[0]) return;
    if (strcmp(name, "nvnDeviceGetProcAddress")) {
        install(name, c->x[0]);
        return;
    }
    u64 resolver = c->x[0];
    if (resolver < g_code_lo || resolver >= g_code_hi || (resolver & 3)) return;
    pm_lock(&slots_lock);
    GuestFn* slot = &g_disp[(resolver - g_code_lo) / 4];
    if (*slot && *slot != getproc_native) {
        __atomic_store_n(&getproc_original, *slot, __ATOMIC_RELEASE);
        getproc_addr = resolver;
        __atomic_store_n(slot, getproc_native, __ATOMIC_RELEASE);
    }
    pm_unlock(&slots_lock);
}

void nvn_native_init(void) {
    if (!g_settings.nvn_native) return;
    u64 entry = kernel_find_symbol_exact("nvnBootstrapLoader");
    if (!entry || entry < g_code_lo || entry >= g_code_hi || (entry & 3)) {
        WARN("nvn-native: bootstrap loader symbol is unavailable\n");
        return;
    }
    GuestFn* slot = &g_disp[(entry - g_code_lo) / 4];
    if (!*slot) {
        WARN("nvn-native: bootstrap loader has no recompiled entry\n");
        return;
    }
    bootstrap_original = *slot;
    *slot = bootstrap_native; /* before any guest thread runs */
    active = 1;
    LOG("nvn-native: on, the host answers the NVN functions that the game resolves (%u handlers)\n",
        (u32)(sizeof native_procs / sizeof native_procs[0] - 1));
}

int nvn_native_active(void) { return active; }

void nvn_native_report(void) {
    if (!active) return;
    pm_lock(&slots_lock);
    u32 missing = 0, called = 0;
    for (u32 i = 0; i < NATIVE_SLOTS; i++) {
        if (!slots[i].addr) continue;
        u64 calls = __atomic_load_n(&slots[i].calls, __ATOMIC_RELAXED);
        if (!slots[i].missing) continue; /* a real handler: the trace counts its calls */
        missing++;
        if (!calls) continue;
        called++;
        LOG("nvn-native: missing %s %llu\n", slots[i].name, (unsigned long long)calls);
    }
    LOG("nvn-native: %u resolved addresses, %u without a handler, %u of them called\n", nslots, missing, called);
    pm_unlock(&slots_lock);
}
