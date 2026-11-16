/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* snx_launcher_name: the names the program opens the menu under when started bare; any other is a game's copy */
#include "common/utilities/common.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(snx_launcher_name("nxaevora-engine") && snx_launcher_name("nxaevora-engine-switch"));
    assert(snx_launcher_name("ScarletNX-x86_64") && snx_launcher_name("NXAEVORA-ENGINE"));
    assert(!snx_launcher_name("scarletnx") && !snx_launcher_name("scarletnx-switch"));
    assert(!snx_launcher_name("MyGame") && !snx_launcher_name("scarlet") && !snx_launcher_name(""));
    assert(!snx_launcher_name("nxaevora-enginegame") && !snx_launcher_name("my nxaevora-engine"));
    puts("launcher names passed");
    return 0;
}
