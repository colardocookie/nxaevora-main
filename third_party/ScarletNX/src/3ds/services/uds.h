/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_CTR_UDS_H
#define SNX_CTR_UDS_H
#include "3ds/ipc/ctr_ipc.h"
struct SnxCtrUds;
void snx_ctr_uds_settings(const char* data);
void snx_ctr_uds_dispatch(SnxCtrIpc* c);
void snx_ctr_uds_poll(struct SnxCtrProcess* p);
void snx_ctr_uds_close(struct SnxCtrProcess* p);
/* Last end-to-end echo RTT, zero until the peer has answered a probe. */
uint64_t snx_ctr_uds_rtt_ns(const struct SnxCtrProcess* p);
#endif
