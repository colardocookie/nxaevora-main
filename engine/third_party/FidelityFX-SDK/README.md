<!-- SPDX-License-Identifier: MIT -->
# FidelityFX SDK API headers

Four headers of AMD's FidelityFX SDK, copied without any change from
https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK at tag `v2.4.0` (commit `7539ae8`, "AMD FSR SDK 2.4.0").
They declare the SDK's C interface, which the runtime calls through the SDK's own library.

| File here | File in the SDK |
| --- | --- |
| `api/include/ffx_api.h` | `Kits/FidelityFX/api/include/ffx_api.h` |
| `api/include/ffx_api_types.h` | `Kits/FidelityFX/api/include/ffx_api_types.h` |
| `api/include/vk/ffx_api_vk.h` | `Kits/FidelityFX/api/include/vk/ffx_api_vk.h` |
| `upscalers/include/ffx_upscale.h` | `Kits/FidelityFX/upscalers/include/ffx_upscale.h` |

The folders are kept as in the SDK because the headers include each other by relative path.

Licence. Each of the four files carries the MIT notice in its first lines, and the SDK's
`Kits/FidelityFX/docs/license.md` lists each of them among the files under that notice. `LICENSE.txt` here is that
notice, taken from `ffx_api.h`. This project's own style rules (line length, comments) do not apply to these files.

Not here, and never in this repository: the SDK's libraries (`amd_fidelityfx_loader_vk.dll`,
`amd_fidelityfx_upscaler_vk.dll`). The SDK says an application must use the signed ones it ships. The user puts
them beside the program.
