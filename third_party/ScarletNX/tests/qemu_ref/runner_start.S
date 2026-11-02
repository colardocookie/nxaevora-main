/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The QEMU reference runner's entry, its trap into a case and its signal return (runner.c) */
.text
.global _start
.type _start, %function
_start:
    mov x0, sp
    bl snx_qref_main
    brk #0
.size _start, .-_start

/* the handler starts the case from this brk's frame and resumes after it once the case is over, with every
   register as it was here */
.global snx_qref_enter
.type snx_qref_enter, %function
snx_qref_enter:
    brk #0xe7e
    ret
.size snx_qref_enter, .-snx_qref_enter

.global snx_qref_restorer
.type snx_qref_restorer, %function
snx_qref_restorer:
    mov x8, #139 /* rt_sigreturn */
    svc #0
.size snx_qref_restorer, .-snx_qref_restorer
.section .note.GNU-stack,"",%progbits
