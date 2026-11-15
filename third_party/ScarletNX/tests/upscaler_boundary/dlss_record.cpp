// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// What DLSS's module records around NGX's evaluation (msvc/upscaler_dlss/dlss_record.h), against a stand-in of
// vkCmdPipelineBarrier that keeps each call: four barriers before with the stated scopes, NGX's call after them, four
// back after a success, none after a failure and UNKNOWN, the command buffer and NGX's user passed through. Run by
// dlss_core.cpp.
#undef NDEBUG /* the checks are asserts, in a Release build too */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dlss_record.h"

namespace {
struct Call {
    int ngx_before; // NGX's calls made before this one: the order of barriers and the call
    VkPipelineStageFlags src, dst;
    VkDependencyFlags dependency;
    uint32_t memory, buffers, images;
    VkImageMemoryBarrier barrier[4];
};
Call g_call[8];
int g_calls, g_ngx;
VkCommandBuffer g_cb; // the stand-in command buffer every barrier call must be given
int g_user;           // what NGX's call must be given as its user
VKAPI_ATTR void VKAPI_CALL record(VkCommandBuffer cb, VkPipelineStageFlags src, VkPipelineStageFlags dst,
                                  VkDependencyFlags dependency, uint32_t memory, const VkMemoryBarrier*,
                                  uint32_t buffers, const VkBufferMemoryBarrier*, uint32_t images,
                                  const VkImageMemoryBarrier* barrier) {
    assert(g_calls < 8 && images <= 4 && cb == g_cb);
    Call& c = g_call[g_calls++];
    c = Call{g_ngx, src, dst, dependency, memory, buffers, images, {}};
    memcpy(c.barrier, barrier, images * sizeof *barrier);
}
/* NGX's call: after exactly the one call of the barriers before */
int ngx_succeeds(void* user) {
    assert(g_calls == 1 && user == &g_user);
    return ++g_ngx, 1;
}
int ngx_fails(void* user) {
    assert(g_calls == 1 && user == &g_user);
    return ++g_ngx, 0;
}
// one call of four barriers with the stated scopes, from the layouts of `from` to those of `to`
void check_call(const Call& c, const snx_dlss::Image image[4], int back) {
    assert(c.src == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT && c.dst == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    assert(!c.dependency && !c.memory && !c.buffers && c.images == 4);
    for (int k = 0; k < 4; k++) {
        const VkImageMemoryBarrier& b = c.barrier[k];
        assert(b.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER && !b.pNext);
        assert(b.srcAccessMask == VK_ACCESS_MEMORY_WRITE_BIT);
        assert(b.dstAccessMask == (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT));
        assert(b.oldLayout == (back ? image[k].mapped : image[k].given));
        assert(b.newLayout == (back ? image[k].given : image[k].mapped));
        assert(b.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED && b.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
        assert(b.image == image[k].image && b.subresourceRange.aspectMask == image[k].aspect);
        assert(b.subresourceRange.baseMipLevel == 0 && b.subresourceRange.levelCount == VK_REMAINING_MIP_LEVELS);
        assert(b.subresourceRange.baseArrayLayer == 0 && b.subresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS);
    }
}
}  // namespace

int dlss_record_cases() {
    using namespace snx_dlss;
    VkImage handles[4];
    for (int k = 0; k < 4; k++) { /* stand-in handles: only compared */
        const uint64_t value = 0x1000 + (uint64_t)k;
        memcpy(&handles[k], &value, sizeof handles[k]);
    }
    const Image image[4] = {
        {handles[0], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {handles[1], VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {handles[2], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, /* already in the mapped layout: a barrier all the same */
        {handles[3], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL}};
    const uint64_t cb_value = 0xCB;
    memcpy(&g_cb, &cb_value, sizeof g_cb);
    const VkCommandBuffer cb = g_cb;
    // a success: four before, NGX once, four back
    g_calls = g_ngx = 0;
    assert(record_evaluation(record, cb, image, ngx_succeeds, &g_user) == RECORD_OK);
    assert(g_calls == 2 && g_ngx == 1);
    assert(g_call[0].ngx_before == 0 && g_call[1].ngx_before == 1); /* before the call, and back after it */
    check_call(g_call[0], image, 0);
    check_call(g_call[1], image, 1);
    // a failure: four before, NGX once, nothing after, UNKNOWN
    g_calls = g_ngx = 0;
    assert(record_evaluation(record, cb, image, ngx_fails, &g_user) == RECORD_UNKNOWN);
    assert(g_calls == 1 && g_ngx == 1 && g_call[0].ngx_before == 0);
    check_call(g_call[0], image, 0);
    puts("dlss_record: ok");
    return 0;
}
