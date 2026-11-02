/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* A run's scripted presses as the drivers take them (--script=ms:hex,...): when, in ms from the start of the run, and
   the buttons in hex in the console's own bits, comma between, each held for the driver's press length */
#ifndef SNX_SCRIPT_H
#define SNX_SCRIPT_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t at, mask; /* ms from the start; the console's buttons */
} SnxPress;

/* text's presses into *presses (malloc'd, NULL for none), their masks within buttons (the bits the console takes):
   their count. A press it cannot read ends the script, with a line on stderr after program's name saying how many it
   read and the text it stopped at */
size_t snx_script_parse(const char* program, const char* text, uint32_t buttons, SnxPress** presses);
/* the buttons the presses hold at ms, each for length ms */
uint32_t snx_script_held(const SnxPress* presses, size_t count, double ms, uint32_t length);
#endif
