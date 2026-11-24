/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_VK_UPSCALER_PROTOCOL_H
#define SNX_VK_UPSCALER_PROTOCOL_H
/* XESS-DESIGN.md revision 2, R1: an adapter's call that may submit work of its own (its prepare, with the trait
   submits_at_init) runs between two waits for the device to be idle, on the thread and under the lock the caller
   holds. Pure: the waits and the call are the caller's, so that every outcome is tested on any host. */

enum {
    SNX_INIT_READY,      /* both waits and the call succeeded: the context may be used */
    SNX_INIT_NOT_CALLED, /* the wait before failed: the call was not made, the upscaler is off */
    SNX_INIT_FAILED,     /* the call failed and the wait after succeeded: what it made may be destroyed, off */
    SNX_INIT_FATAL       /* the wait after failed: completion is not proven, nothing is called or freed again */
};
typedef struct {
    int (*wait)(void* user); /* 1 when the device is idle */
    int (*call)(void* user); /* 1 when the adapter's call succeeded */
    void* user;
} SnxInitSteps;
/* The two waits are made only around a call: a failed wait before means no call and no second wait. */
static inline int snx_init_protocol(const SnxInitSteps* s) {
    if (!s->wait(s->user)) return SNX_INIT_NOT_CALLED;
    const int done = s->call(s->user);
    if (!s->wait(s->user)) return SNX_INIT_FATAL;
    return done ? SNX_INIT_READY : SNX_INIT_FAILED;
}
static inline const char* snx_init_text(int outcome) {
    switch (outcome) {
        case SNX_INIT_READY: return "ready";
        case SNX_INIT_NOT_CALLED: return "not called: the device was not idle before it";
        case SNX_INIT_FAILED: return "failed, with the device idle after it";
        default: return "the device was not idle after it: its end is not known";
    }
}
#endif
