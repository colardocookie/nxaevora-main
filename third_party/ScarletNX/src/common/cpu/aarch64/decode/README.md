<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# The A64 decoder's sources

`a64.decode` is QEMU's `target/arm/tcg/a64.decode`, unmodified, from QEMU commit
`a19f59a72d96056d9a8f48cf6c77f5787491e414` (2026-10-16, version 11.1.50). It is LGPL-2.1-or-later (Copyright (c) 2023
Linaro, Ltd), its notice at the top of the file. To update it, copy the file again from a newer QEMU and change the
commit here. `sve.decode` and `sme.decode` can join it the same way: `tools/a64dec` reads several files.

`a64.profile` is ScarletNX's own (GPL-3.0-or-later): one line per name of the `.decode` files with its flow kind, its
predicates and the FEAT_* features it needs, the features' order and the target sets. Its header describes the
format.

`tools/a64dec` (`cmake/a64dec.cmake`) generates `a64_decode.h` and `a64_decode.inc` from both into the build folder.
The generated files are derived from `a64.decode` and carry its license. `tests/a64_decode_test.c` checks them.
