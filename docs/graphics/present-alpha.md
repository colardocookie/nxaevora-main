<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# The HUD ghosting with Mailbox and Immediate (2026-11-01)

## What the user saw

With `vsync` 2 (Mailbox) or 0 (Immediate), the field HUD of the game left faint copies of itself over the picture: the
markers with their distance ("179") at their older places, the area name, and a haze over the grass. FIFO had none.
A game started in FIFO and switched to Mailbox in the game had none. A game started in Mailbox had them until the
user chose FIFO once, and then they did not return in Mailbox either.

## What was checked

- `imgui=0` and G-Sync off did not change it. A V-Sync change that rebuilds the swapchain with the same mode (F11) did not
  cure it. A `vk_finish` after each present did not cure it (the GPU running ahead is not the cause).
- The runtime's screenshot (`--shot=`, `--shot_every=`) saves the game's render target before the blit. It was clean.
  A Windows screenshot of the window had the ghosts. So they are made after the game's frame, not in it.
- The swapchain was made with `VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR` (the surface offers opaque and pre-multiplied).

## The fix

The game's last render target has an alpha channel that its HUD passes leave below 1, and the blit copies it to the
swapchain image. `vk_ui_draw` (`gpu/vk/ui.c`) now first draws one quad over the whole image with a pipeline that
writes only alpha and has no blend (`g_pipe_alpha`), so the image's alpha is 1 when it is presented. The user ran it and the
ghosts were gone in Mailbox.

## Not known

Why the driver or Windows uses that alpha when the composite alpha is opaque, and only with a mode that is not FIFO
(and why one run in FIFO keeps it away). The fix does not depend on the answer: an opaque picture has no alpha to use.
