/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The encrypted Switch dumps the recompile driver and the driver refuse: an NSP, an XCI, a DNSP and a DXCI, in any
   case; a game folder and an NRO are inputs. Both call snx_refuse_encrypted_dump. */
#include "common/utilities/common.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
static void check(int good, const char* expression, int line) {
    ++checks;
    if (!good) {
        fprintf(stderr, "dump_input.c:%d: check failed: %s\n", line, expression);
        exit(1);
    }
}
#define CHECK(value) check((value) != 0, #value, __LINE__)

int main(void) {
    CHECK(snx_refuse_encrypted_dump("x.nsp"));
    CHECK(snx_refuse_encrypted_dump("X.XCI"));
    CHECK(snx_refuse_encrypted_dump("x.dnsp"));
    CHECK(snx_refuse_encrypted_dump("x.dxci"));
    CHECK(snx_refuse_encrypted_dump("a/b/Game.NsP"));
    CHECK(!snx_refuse_encrypted_dump("game"));
    CHECK(!snx_refuse_encrypted_dump("game/"));
    CHECK(!snx_refuse_encrypted_dump("game.nro"));
    CHECK(!snx_refuse_encrypted_dump("game.nso"));
    printf("PASS %u checks: encrypted dumps are refused, a game folder and an NRO are inputs\n", checks);
    return 0;
}
