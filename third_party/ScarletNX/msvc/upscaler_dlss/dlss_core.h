// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// NGX's lifetimes for DLSS's module (UPSCALER-DESIGN.md, DL1b revision 2), with no NGX type: NGX is the module's and
// is started once for a device, at its first context. Contexts come and go without stopping it. It is stopped only
// by the table's shutdown, and only when no context of the device is left. The module calls these under its lock.
#ifndef SNX_DLSS_CORE_H
#define SNX_DLSS_CORE_H
#include <stdint.h>
#include <stdio.h>

namespace snx_dlss {
enum { CORE_OK = 0, CORE_REFUSED = 1, CORE_FAILED = 2 }; // the interface's SNX_UPSCALER_OK, _REFUSED, _FAILED
// What NGX is to the core: each call 1 when it succeeded. say gets one line.
struct CoreOps {
    void* user;
    int (*start)(void* user, uint64_t device);
    int (*stop)(void* user, uint64_t device);
    int (*release)(void* user, void* context); // a context's feature and parameters
    void (*say)(void* user, const char* line);
};
enum { DEVICE_FREE, DEVICE_STARTED, DEVICE_START_FAILED, DEVICE_SHUT };
const int kDevices = 4;
struct Device {
    uint64_t handle;
    int state;
    uint32_t contexts; // begun and not ended
    int latched;       // a release that failed since the start: shutdown answers FAILED
};
struct Core {
    CoreOps ops;
    Device device[kDevices];
};

inline void core_say(Core* c, const char* what, uint64_t device, uint32_t n) {
    char line[256];
    snprintf(line, sizeof line, "arceus-upscaler: dlss: %s (device %llx, %u contexts)", what,
             (unsigned long long)device, n);
    if (c->ops.say) c->ops.say(c->ops.user, line);
}
// the device's record, made when make is set and there is room, else nullptr
inline Device* core_device(Core* c, uint64_t handle, int make) {
    for (Device& d : c->device)
        if (d.state != DEVICE_FREE && d.handle == handle) return &d;
    if (!make) return nullptr;
    for (Device& d : c->device)
        if (d.state == DEVICE_FREE) {
            d = Device{handle, DEVICE_FREE, 0, 0};
            return &d;
        }
    return nullptr;
}
// Before a context is made for the device: NGX is started for it the first time, never again. CORE_OK counts the
// context, which core_end ends. A start that failed, or a device shut down, refuses from then on.
inline int core_begin(Core* c, uint64_t handle) {
    Device* d = core_device(c, handle, 1);
    if (!d) {
        core_say(c, "no context: the module keeps no more devices", handle, 0);
        return CORE_REFUSED;
    }
    if (d->state == DEVICE_START_FAILED || d->state == DEVICE_SHUT) {
        core_say(c, d->state == DEVICE_SHUT ? "no context: NGX was shut down for the device"
                                            : "no context: NGX could not be started for the device",
                 handle, d->contexts);
        return CORE_REFUSED;
    }
    if (d->state == DEVICE_FREE) {
        if (!c->ops.start(c->ops.user, handle)) {
            d->state = DEVICE_START_FAILED;
            core_say(c, "NGX could not be started for the device: no context", handle, 0);
            return CORE_FAILED;
        }
        d->state = DEVICE_STARTED;
        core_say(c, "NGX started for the device", handle, 0);
    }
    d->contexts++;
    return CORE_OK;
}
// A context begun is ended: its feature and parameters released when there are any (context not nullptr). A
// release that fails is latched for the device's shutdown. NGX stays started.
inline void core_end(Core* c, uint64_t handle, void* context) {
    Device* d = core_device(c, handle, 0);
    if (!d || d->state != DEVICE_STARTED || !d->contexts) {
        core_say(c, "a context was ended that was not begun", handle, d ? d->contexts : 0);
        return;
    }
    if (context && !c->ops.release(c->ops.user, context)) {
        d->latched = 1;
        core_say(c, "a context's feature or parameters could not be released", handle, d->contexts);
    }
    d->contexts--;
}
// The table's shutdown: only with no context of the device left, else refused with nothing called. CORE_FAILED
// when NGX's stop failed or a release since its start did.
inline int core_shutdown(Core* c, uint64_t handle) {
    Device* d = core_device(c, handle, 0);
    if (!d || d->state == DEVICE_START_FAILED) return CORE_OK; // nothing of NGX was started for it
    if (d->state == DEVICE_SHUT) {
        core_say(c, "NGX was already shut down for the device", handle, 0);
        return CORE_FAILED;
    }
    if (d->contexts) {
        core_say(c, "NGX is not shut down: contexts of the device are left", handle, d->contexts);
        return CORE_FAILED;
    }
    const int stopped = c->ops.stop(c->ops.user, handle);
    const int ok = stopped && !d->latched;
    d->state = DEVICE_SHUT;
    core_say(c, ok        ? "NGX shut down for the device"
                : stopped ? "NGX shut down for the device, after a release that failed"
                          : "NGX's shutdown for the device failed",
             handle, 0);
    return ok ? CORE_OK : CORE_FAILED;
}
} // namespace snx_dlss
#endif
