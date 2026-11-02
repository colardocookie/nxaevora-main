/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* A test that crashes on Windows ends without a word of its own, ctest saying only SegFault. Installed, the crash says
   its exception and the frames unwound from it, each as its module and offset, and llvm-symbolizer names the test's
   own frames by function and line when the PATH has it (MSYS2's CLANG64 does) */
#ifndef SNX_TEST_WIN32_CRASH_H
#define SNX_TEST_WIN32_CRASH_H
void snx_test_crash_said(void); /* at main's start */
#endif
