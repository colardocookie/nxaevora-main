/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors
   Ported from Eden (src/video_core/cdma_pusher.h, src/video_core/host1x/control.h):
   SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
   SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
   SPDX-FileCopyrightText: 2021 yuzu Emulator Project
   SPDX-FileCopyrightText: 2021 Skyline Team and Contributors */
/* The host1x command stream a channel's command buffers hold, as host1x.c's run_words decodes it: the command header
   (Eden's ChCommandHeader), the submission modes (ChSubmissionMode), the classes a stream addresses (ChClassId), the
   THI methods every engine class has (ThiMethod) and the host class's own methods (Control::Method).

   Method numbers are word indices (a register's byte offset / 4), as the command header carries them */
#ifndef SNX_HOST1X_REGS_H
#define SNX_HOST1X_REGS_H
#include "common/cpu/aarch64/cpu.h"

/* ChCommandHeader: value in bits 0-15, method offset in 16-27, submission mode in 28-31 */
static inline u32 ch_header_value(u32 c) { return c & 0xFFFFu; }
static inline u32 ch_header_method_offset(u32 c) { return (c >> 16) & 0xFFFu; }
static inline u32 ch_header_submission_mode(u32 c) { return c >> 28; }

/* ChSubmissionMode. Restart and Gather (5, 6) and the rest aren't decoded: they end the buffer */
enum {
    CH_SUBMISSION_SET_CLASS = 0,        /* value: class in bits 6-15, a mask of 6 methods from the offset in 0-5 */
    CH_SUBMISSION_INCREMENTING = 1,     /* value: a count of words, to offset, offset + 1, ... */
    CH_SUBMISSION_NON_INCREMENTING = 2, /* value: a count of words, all to offset */
    CH_SUBMISSION_MASK = 3,             /* value: a mask of 16 methods from the offset, a word for each bit set */
    CH_SUBMISSION_IMMEDIATE = 4         /* value: the data itself, to offset */
};
/* SetClass's value field: the class, and the methods it writes right away. Eden reads the same bits */
static inline u32 ch_set_class_id(u32 c) { return (ch_header_value(c) >> 6) & 0x3FFu; }
static inline u32 ch_set_class_mask(u32 c) { return ch_header_value(c) & 0x3Fu; }
#define CH_SET_CLASS_MASK_BITS 6u
#define CH_MASK_BITS           16u
/* Immediate's data is all 16 bits of the value, as the Linux host1x driver's host1x_opcode_imm builds it (Eden keeps
   the low 12) */
static inline u32 ch_immediate_data(u32 c) { return ch_header_value(c); }

/* ChClassId: the classes run here. A channel starts in its engine's */
enum {
    CH_CLASS_CONTROL = 0x01, /* host1x itself: syncpoint waits */
    CH_CLASS_GRAPHICS_VIC = 0x5D,
    CH_CLASS_NVDEC = 0xF0
};

/* ThiMethod: an engine class's methods reach its registers through these two. SetMethod0 selects the engine method
   (a word index), SetMethod1 writes it */
enum { THI_SET_METHOD0 = 0x10, THI_SET_METHOD1 = 0x11 };

/* Control::Method: the host class. WaitSyncpt's argument is the syncpoint in bits 24-31 and a 24-bit threshold;
   WaitSyncpt32 waits for the syncpoint its argument names to reach what LoadSyncptPayload32 stored */
enum { CONTROL_WAIT_SYNCPT = 0x08, CONTROL_LOAD_SYNCPT_PAYLOAD_32 = 0x4E, CONTROL_WAIT_SYNCPT_32 = 0x50 };
static inline u32 control_wait_syncpt_id(u32 v) { return v >> 24; }
static inline u32 control_wait_syncpt_threshold(u32 v) { return v & 0xFFFFFFu; }
#define CONTROL_WAIT_SYNCPT_THRESHOLD_BITS 24u

#endif
