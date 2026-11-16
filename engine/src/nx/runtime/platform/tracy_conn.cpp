/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The one piece of the C++ client the C API keeps private: the connection's id, and whether a server is attached.
   A GPU emission's epoch is keyed on it (tracy.c's GPU track): a new connection replays the deferred contexts,
   so its events may not be mixed with the last connection's. TracyCIsConnected reads the client's own data,
   which a run that never started one has none of, so the C gate decides whether to reach it at all. */
extern "C" {
#include "nx/runtime/platform/tracy.h"
}
#include <tracy/Tracy.hpp>

extern "C" uint64_t snx_tracy_connection_id(void) {
#ifdef TRACY_ON_DEMAND
    return tracy::GetProfiler().ConnectionId();
#else
    return 1;
#endif
}

extern "C" int snx_tracy_attached(void) {
    if (!snx_tracy_gate_open()) return 0;
    return TracyIsConnected;
}
