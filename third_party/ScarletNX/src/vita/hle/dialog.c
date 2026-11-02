/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* SceCommonDialog: the system's dialogs, drawn over the game. One is open at a time: its Init opens it (BUSY while
   another is open), its GetStatus says RUNNING until the player is done with it, then FINISHED, its GetResult gives
   what came of it, and its Term closes it. A game waits for FINISHED, so a dialog the runtime doesn't model would
   stall it: the ones modeled here finish as they open, with what the console's would end with when nothing needs
   the player. The trophy setup dialog (the trophies' files made ready) succeeds. The network check dialog finds no
   network (the runtime gives a game none: vita/hle/net.c), and the player backs out of its message. A finished
   dialog keeps the others out until its Term: a game that opens the network check as it boots and closes it from
   its menus waits that long for its trophy setup, retrying its Init while it answers BUSY */
#include "vita/hle/hle.h"
#include <string.h>

#define SCE_COMMON_DIALOG_ERROR_BUSY         0x80020401u
#define SCE_COMMON_DIALOG_ERROR_NULL         0x80020402u
#define SCE_COMMON_DIALOG_ERROR_NOT_RUNNING  0x80020404u
#define SCE_COMMON_DIALOG_ERROR_NOT_FINISHED 0x80020410u
#define SCE_COMMON_DIALOG_ERROR_NOT_IN_USE   0x80020411u

enum { STATUS_NONE, STATUS_RUNNING, STATUS_FINISHED };
enum { RESULT_OK, RESULT_USER_CANCELED, RESULT_ABORTED };
enum { DIALOG_NONE, DIALOG_TROPHY_SETUP, DIALOG_NET_CHECK };
/* each dialog's result struct: its SceCommonDialogResult first, the rest zero (SceNpTrophySetupDialogResult,
   SceNetCheckDialogResult with psnModeSucceeded false) */
#define RESULT_SIZE 0x84u

static uint32_t init(VitaProcess* p, VitaThread* t, uint32_t kind, uint32_t result) {
    if (!vita_arg(p, t, 0)) return SCE_COMMON_DIALOG_ERROR_NULL;
    if (p->dialog.kind) return SCE_COMMON_DIALOG_ERROR_BUSY;
    p->dialog.kind = kind, p->dialog.status = STATUS_FINISHED, p->dialog.result = result;
    return 0;
}
static uint32_t status(VitaProcess* p, uint32_t kind) {
    return p->dialog.kind == kind ? p->dialog.status : STATUS_NONE;
}
static uint32_t result(VitaProcess* p, VitaThread* t, uint32_t kind) {
    uint32_t out = vita_arg(p, t, 0);
    if (p->dialog.kind != kind) return SCE_COMMON_DIALOG_ERROR_NOT_IN_USE;
    if (p->dialog.status != STATUS_FINISHED) return SCE_COMMON_DIALOG_ERROR_NOT_FINISHED;
    if (!out) return SCE_COMMON_DIALOG_ERROR_NULL;
    memset(vita_ptr(p, out), 0, RESULT_SIZE);
    vita_write32(p, out, p->dialog.result);
    return 0;
}
static uint32_t abort_dialog(VitaProcess* p, uint32_t kind) {
    if (p->dialog.kind != kind) return SCE_COMMON_DIALOG_ERROR_NOT_IN_USE;
    if (p->dialog.status != STATUS_RUNNING) return SCE_COMMON_DIALOG_ERROR_NOT_RUNNING;
    p->dialog.status = STATUS_FINISHED, p->dialog.result = RESULT_ABORTED;
    return 0;
}
static uint32_t term(VitaProcess* p, uint32_t kind) {
    if (p->dialog.kind != kind) return SCE_COMMON_DIALOG_ERROR_NOT_IN_USE;
    if (p->dialog.status != STATUS_FINISHED) return SCE_COMMON_DIALOG_ERROR_NOT_FINISHED;
    p->dialog.kind = DIALOG_NONE, p->dialog.status = STATUS_NONE;
    return 0;
}

static uint32_t trophy_setup_init(VitaProcess* p, VitaThread* t) { return init(p, t, DIALOG_TROPHY_SETUP, RESULT_OK); }
static uint32_t trophy_setup_status(VitaProcess* p, VitaThread* t) {
    (void)t;
    return status(p, DIALOG_TROPHY_SETUP);
}
static uint32_t trophy_setup_result(VitaProcess* p, VitaThread* t) { return result(p, t, DIALOG_TROPHY_SETUP); }
static uint32_t trophy_setup_abort(VitaProcess* p, VitaThread* t) {
    (void)t;
    return abort_dialog(p, DIALOG_TROPHY_SETUP);
}
static uint32_t trophy_setup_term(VitaProcess* p, VitaThread* t) {
    (void)t;
    return term(p, DIALOG_TROPHY_SETUP);
}
static uint32_t net_check_init(VitaProcess* p, VitaThread* t) {
    return init(p, t, DIALOG_NET_CHECK, RESULT_USER_CANCELED);
}
static uint32_t net_check_status(VitaProcess* p, VitaThread* t) {
    (void)t;
    return status(p, DIALOG_NET_CHECK);
}
static uint32_t net_check_result(VitaProcess* p, VitaThread* t) { return result(p, t, DIALOG_NET_CHECK); }
static uint32_t net_check_abort(VitaProcess* p, VitaThread* t) {
    (void)t;
    return abort_dialog(p, DIALOG_NET_CHECK);
}
static uint32_t net_check_term(VitaProcess* p, VitaThread* t) {
    (void)t;
    return term(p, DIALOG_NET_CHECK);
}
static uint32_t zero(VitaProcess* p, VitaThread* t) {
    (void)p, (void)t;
    return 0;
}

const VitaHle vita_hle_dialog[] = {
    {0x90530f2fu, "sceCommonDialogUpdate", zero},
    {0xbecd35c8u, "sceCommonDialogSetConfigParam", zero},
    {0x9e2c02c9u, "sceNpTrophySetupDialogInit", trophy_setup_init},
    {0xc3a59547u, "sceNpTrophySetupDialogGetStatus", trophy_setup_status},
    {0xe37069d5u, "sceNpTrophySetupDialogGetResult", trophy_setup_result},
    {0xdc346979u, "sceNpTrophySetupDialogAbort", trophy_setup_abort},
    {0xa81082ddu, "sceNpTrophySetupDialogTerm", trophy_setup_term},
    {0xa38a4a0du, "sceNetCheckDialogInit", net_check_init},
    {0x8027292au, "sceNetCheckDialogGetStatus", net_check_status},
    {0xb05fce9eu, "sceNetCheckDialogGetResult", net_check_result},
    {0x2d8edf09u, "sceNetCheckDialogAbort", net_check_abort},
    {0x8be51c15u, "sceNetCheckDialogTerm", net_check_term},
    {0, NULL, NULL},
};
