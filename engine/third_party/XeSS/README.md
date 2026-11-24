<!-- SPDX-License-Identifier: MIT -->
# XeSS SDK headers

Two headers of Intel's XeSS SDK, copied without any change from https://github.com/intel/xess at commit `de0fb9c`
("Update license to MIT for public headers only"). They declare the C interface of XeSS Super Resolution and its
Vulkan entry points, which the runtime's XeSS adapter calls through the SDK's own library.

| File here | File in the SDK |
| --- | --- |
| `inc/xess/xess.h` | `inc/xess/xess.h` |
| `inc/xess/xess_vk.h` | `inc/xess/xess_vk.h` |

The folder is kept as in the SDK because `xess_vk.h` includes `xess.h` by relative path.

Licence. Each of the two files names the MIT licence in its first lines, and the SDK keeps its notice in
`inc/MIT.txt`, which `LICENSE.txt` here is, unchanged. The SDK's library, `libxess.dll`, is under Intel's own licence
and is not part of this project: the user supplies it beside the program. Nothing else of the SDK is here. This
project's own style rules (line length, comments, line endings) do not apply to these files.
