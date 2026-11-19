// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// What DLSS's module records around NGX's evaluation (UPSCALER-DESIGN.md, DL1b revision 3), with no NGX type: a
// barrier for each of the four images to the layout NGX is given it in, NGX's call, and after a success the same back.
// After a failure nothing more: what NGX recorded and left is not known, and the answer is UNKNOWN.
#ifndef SNX_DLSS_RECORD_H
#define SNX_DLSS_RECORD_H
#include <vulkan/vulkan_core.h>

namespace snx_dlss {
enum { RECORD_OK = 0, RECORD_UNKNOWN = 3 }; // the interface's SNX_UPSCALER_OK and SNX_UPSCALER_UNKNOWN
// One image as the module hands it: the aspects its barrier covers (both of a combined depth and stencil image), the
// layout it is in at the call and the one NGX is given it in (shader read only for the inputs, general for the output)
struct Image {
    VkImage image;
    VkImageAspectFlags aspect;
    VkImageLayout given, mapped;
};
// The four barriers, colour, depth, motion and output, in one call of vkCmdPipelineBarrier (the self-test's device has
// no synchronization2): every command and memory write before, every command and memory read and write after.
// Recorded also where the two layouts are the same. back: from the mapped layouts to the given ones.
inline void record_barriers(PFN_vkCmdPipelineBarrier record, VkCommandBuffer cb, const Image image[4], int back) {
    VkImageMemoryBarrier b[4];
    for (int k = 0; k < 4; k++) {
        b[k] = VkImageMemoryBarrier{};
        b[k].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b[k].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        b[k].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b[k].oldLayout = back ? image[k].mapped : image[k].given;
        b[k].newLayout = back ? image[k].given : image[k].mapped;
        b[k].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[k].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[k].image = image[k].image;
        b[k].subresourceRange = VkImageSubresourceRange{image[k].aspect, 0, VK_REMAINING_MIP_LEVELS, 0,
                                                        VK_REMAINING_ARRAY_LAYERS};
    }
    record(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 4,
           b);
}
// One evaluation: the barriers, NGX's call (1 when NGX answered with success), and the barriers back after a success
// only. RECORD_UNKNOWN after a failure: the runtime must not submit the command buffer.
inline int record_evaluation(PFN_vkCmdPipelineBarrier record, VkCommandBuffer cb, const Image image[4],
                             int (*call)(void* user), void* user) {
    record_barriers(record, cb, image, 0);
    if (!call(user)) return RECORD_UNKNOWN;
    record_barriers(record, cb, image, 1);
    return RECORD_OK;
}
} // namespace snx_dlss
#endif
