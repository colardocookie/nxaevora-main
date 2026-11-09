/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Where the Lua 5.3 fixtures put things in scratch memory, for the stubs that hand out objects of the same pool */
#ifndef RECON_FIXTURES_LUA53_H
#define RECON_FIXTURES_LUA53_H

#define OFF_STATE_A 0x0000
#define OFF_STATE_B 0x0400
#define OFF_GLOBAL 0x0800
#define OFF_CI_A 0x0A00
#define OFF_CI_B 0x0A80
#define OFF_CLOSURE 0x0B00
#define OFF_STACK_A 0x1000
#define OFF_STACK_B 0x4000
#define OFF_OBJECTS 0x8000 /* the pool that collectable values point into */
#define OBJECT_SIZE 0x80
#define OBJECT_COUNT 64

#endif
