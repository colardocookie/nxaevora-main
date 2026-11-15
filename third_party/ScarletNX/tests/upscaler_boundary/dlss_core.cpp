// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// NGX's lifetimes in DLSS's module (msvc/upscaler_dlss/dlss_core.h) against a stand-in of NGX that counts its calls,
// with no SDK: the orders revision 2 of DL1b names. Built by this build's compiler and, where Visual Studio is found,
// by Microsoft's: given that copy's path, this one runs it as well. Nothing of the C++ library is used. The numbers
// handed to NGX (dlss_eval.cpp) are run from here too.
#undef NDEBUG /* the checks are asserts, in a Release build too */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dlss_core.h"

namespace {
struct Stand {
    int starts, stops, releases, lines;
    int start_ok, stop_ok, release_ok;
};
int start(void* user, uint64_t) {
    Stand* s = static_cast<Stand*>(user);
    s->starts++;
    return s->start_ok;
}
int stop(void* user, uint64_t) {
    Stand* s = static_cast<Stand*>(user);
    s->stops++;
    return s->stop_ok;
}
int release(void* user, void*) {
    Stand* s = static_cast<Stand*>(user);
    s->releases++;
    return s->release_ok;
}
void say(void* user, const char* line) {
    static_cast<Stand*>(user)->lines++;
    printf("%s\n", line);
}
struct Fixture {
    Stand stand;
    snx_dlss::Core core;
};
void fresh(Fixture* f) {
    memset(f, 0, sizeof *f);
    f->stand.start_ok = f->stand.stop_ok = f->stand.release_ok = 1;
    f->core.ops = snx_dlss::CoreOps{&f->stand, start, stop, release, say};
}
const uint64_t kDevice = 0xD0, kOther = 0xD1;
int a_context, b_context; // what a context's feature stands for: only its address is used
}  // namespace

int dlss_eval_cases(); /* dlss_eval.cpp */

int main(int argc, char** argv) {
    using namespace snx_dlss;
    dlss_eval_cases();
    Fixture f;
    // one context: NGX started at its begin, released at its end, stopped at the shutdown
    fresh(&f);
    assert(core_begin(&f.core, kDevice) == CORE_OK && f.stand.starts == 1);
    core_end(&f.core, kDevice, &a_context);
    assert(f.stand.releases == 1 && f.stand.stops == 0);
    assert(core_shutdown(&f.core, kDevice) == CORE_OK && f.stand.stops == 1);
    // two contexts and one shutdown: one start, two releases, one stop
    fresh(&f);
    assert(core_begin(&f.core, kDevice) == CORE_OK && core_begin(&f.core, kDevice) == CORE_OK);
    assert(f.stand.starts == 1);
    core_end(&f.core, kDevice, &a_context);
    core_end(&f.core, kDevice, &b_context);
    assert(core_shutdown(&f.core, kDevice) == CORE_OK && f.stand.stops == 1 && f.stand.releases == 2);
    // the old retired while the new prepares: NGX stays started, nothing is stopped
    fresh(&f);
    assert(core_begin(&f.core, kDevice) == CORE_OK);  // the old
    assert(core_begin(&f.core, kDevice) == CORE_OK);  // the new, preparing
    core_end(&f.core, kDevice, &a_context);           // the old retired
    assert(f.stand.stops == 0 && f.stand.starts == 1 && f.core.device[0].contexts == 1);
    core_end(&f.core, kDevice, &b_context);
    assert(core_shutdown(&f.core, kDevice) == CORE_OK && f.stand.stops == 1);
    // a failed prepare: its context is ended like any other, and the shutdown is clean
    fresh(&f);
    assert(core_begin(&f.core, kDevice) == CORE_OK);
    core_end(&f.core, kDevice, &a_context);
    assert(core_shutdown(&f.core, kDevice) == CORE_OK);
    // a context that never had a feature (its creation failed after the begin): nothing released
    fresh(&f);
    assert(core_begin(&f.core, kDevice) == CORE_OK);
    core_end(&f.core, kDevice, nullptr);
    assert(f.stand.releases == 0 && core_shutdown(&f.core, kDevice) == CORE_OK);
    // shutdown with a context left: refused, nothing called, then done when the context is ended
    fresh(&f);
    assert(core_begin(&f.core, kDevice) == CORE_OK);
    assert(core_shutdown(&f.core, kDevice) == CORE_FAILED && f.stand.stops == 0 && f.core.device[0].contexts == 1);
    core_end(&f.core, kDevice, &a_context);
    assert(core_shutdown(&f.core, kDevice) == CORE_OK && f.stand.stops == 1);
    // a release that failed is latched: the shutdown still stops NGX and answers FAILED
    fresh(&f);
    f.stand.release_ok = 0;
    assert(core_begin(&f.core, kDevice) == CORE_OK);
    core_end(&f.core, kDevice, &a_context);
    assert(core_shutdown(&f.core, kDevice) == CORE_FAILED && f.stand.stops == 1);
    // a stop that failed answers FAILED
    fresh(&f);
    f.stand.stop_ok = 0;
    assert(core_begin(&f.core, kDevice) == CORE_OK);
    core_end(&f.core, kDevice, &a_context);
    assert(core_shutdown(&f.core, kDevice) == CORE_FAILED);
    // after the shutdown: no context begins, nothing is started again, a second shutdown is refused
    fresh(&f);
    assert(core_begin(&f.core, kDevice) == CORE_OK);
    core_end(&f.core, kDevice, &a_context);
    assert(core_shutdown(&f.core, kDevice) == CORE_OK);
    assert(core_begin(&f.core, kDevice) == CORE_REFUSED && f.stand.starts == 1);
    assert(core_shutdown(&f.core, kDevice) == CORE_FAILED && f.stand.stops == 1);
    // a start that failed: no context, never tried again, and a shutdown has nothing to stop
    fresh(&f);
    f.stand.start_ok = 0;
    assert(core_begin(&f.core, kDevice) == CORE_FAILED && f.stand.starts == 1);
    assert(core_begin(&f.core, kDevice) == CORE_REFUSED && f.stand.starts == 1);
    assert(core_shutdown(&f.core, kDevice) == CORE_OK && f.stand.stops == 0);
    // devices apart: each its own start and stop, a shutdown of one leaves the other
    fresh(&f);
    assert(core_begin(&f.core, kDevice) == CORE_OK && core_begin(&f.core, kOther) == CORE_OK && f.stand.starts == 2);
    core_end(&f.core, kDevice, &a_context);
    assert(core_shutdown(&f.core, kDevice) == CORE_OK && f.stand.stops == 1);
    assert(core_shutdown(&f.core, kOther) == CORE_FAILED && f.stand.stops == 1); // its context is left
    core_end(&f.core, kOther, &b_context);
    assert(core_shutdown(&f.core, kOther) == CORE_OK && f.stand.stops == 2);
    // more devices than the module keeps: refused, nothing started
    fresh(&f);
    for (int d = 0; d < kDevices; d++) assert(core_begin(&f.core, 0x100 + (uint64_t)d) == CORE_OK);
    assert(core_begin(&f.core, 0x200) == CORE_REFUSED && f.stand.starts == kDevices);
    // an end without a begin, or for a device never seen: a line, nothing released
    fresh(&f);
    core_end(&f.core, kDevice, &a_context);
    assert(f.stand.releases == 0 && f.stand.lines == 1);
    puts("dlss_core: ok");
    if (argc == 2) { // the copy built by Microsoft's compiler, run as well
        char command[4200];
        snprintf(command, sizeof command, "\"%s\"", argv[1]);
        const int status = system(command);
        printf("dlss_core: the copy %s %s\n", argv[1], status == 0 ? "passed" : "FAILED");
        return status == 0 ? 0 : 1;
    }
    return 0;
}
