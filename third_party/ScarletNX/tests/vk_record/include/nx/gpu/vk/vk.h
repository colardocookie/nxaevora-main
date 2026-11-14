/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* A stand-in for gpu/vk/vk.h, for tests/vk_record alone: what gpu/vk/record.c needs of Vulkan and the renderer,
   so that the recorder's own file compiles on a host without the Vulkan headers. Handles are opaque, structures
   hold the members record.c names, and every command is a fake that notes itself (fixture.c). A command
   record.c gains and this file lacks does not compile. */
#ifndef SNX_VK_RECORD_STANDIN_H
#define SNX_VK_RECORD_STANDIN_H
#include "common/cpu/aarch64/cpu.h"
#include "common/utilities/endian.h"
#include "nx/runtime/platform/tracy.h"
#include <stdint.h>

#define VK_NULL_HANDLE 0
#define VK_FRAMES 2
#define SNX_VKB_END 64
typedef struct VkCommandBuffer_T* VkCommandBuffer;
typedef struct VkBuffer_T* VkBuffer;
typedef struct VkBufferView_T* VkBufferView;
typedef struct VkImage_T* VkImage;
typedef struct VkImageView_T* VkImageView;
typedef struct VkSampler_T* VkSampler;
typedef struct VkPipeline_T* VkPipeline;
typedef struct VkPipelineLayout_T* VkPipelineLayout;
typedef struct VkDescriptorSet_T* VkDescriptorSet;
typedef struct VkDescriptorSetLayout_T* VkDescriptorSetLayout;
typedef struct VkDescriptorPool_T* VkDescriptorPool;
typedef struct VkQueryPool_T* VkQueryPool;
typedef struct VkQueue_T* VkQueue;
typedef struct VkDevice_T* VkDevice;
typedef struct VkSemaphore_T* VkSemaphore;
typedef struct VkSwapchainKHR_T* VkSwapchainKHR;
typedef struct VkFence_T* VkFence;
typedef uint32_t VkBool32, VkFlags;
typedef uint64_t VkDeviceSize, VkFlags64;
typedef VkFlags VkCullModeFlags, VkStencilFaceFlags, VkImageAspectFlags, VkQueryControlFlags, VkQueryResultFlags;
typedef VkFlags VkShaderStageFlags, VkDependencyFlags, VkRenderingFlags, VkConditionalRenderingFlagsEXT;
typedef VkFlags64 VkPipelineStageFlags2, VkAccessFlags2;
typedef int VkCompareOp, VkFrontFace, VkPrimitiveTopology, VkStencilOp, VkIndexType, VkFilter, VkImageLayout;
typedef int VkPipelineBindPoint, VkStructureType, VkDescriptorType, VkAttachmentLoadOp, VkAttachmentStoreOp;
typedef enum {
    VK_SUCCESS = 0,
    VK_ERROR_DEVICE_LOST = -4,
    VK_ERROR_FRAGMENTED_POOL = -12,
    VK_ERROR_OUT_OF_POOL_MEMORY = -1000069000,
    VK_ERROR_UNKNOWN = -13
} VkResult;
enum {
    VK_PIPELINE_BIND_POINT_GRAPHICS = 0,
    VK_PIPELINE_BIND_POINT_COMPUTE = 1,
    VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT = 1
};
enum {
    VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER = 4,
    VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER = 5,
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER = 6,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER = 7,
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC = 8,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC = 9
};
enum {
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO = 1,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
    VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT,
    VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    VK_STRUCTURE_TYPE_RENDERING_INFO,
    VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
    VK_STRUCTURE_TYPE_SUBMIT_INFO_2
};

typedef struct {
    int32_t x, y;
} VkOffset2D;
typedef struct {
    uint32_t width, height;
} VkExtent2D;
typedef struct {
    VkOffset2D offset;
    VkExtent2D extent;
} VkRect2D;
typedef struct {
    float x, y, width, height, minDepth, maxDepth;
} VkViewport;
typedef struct {
    VkDeviceSize srcOffset, dstOffset, size;
} VkBufferCopy;
typedef struct {
    uint64_t words[9];
} VkImageCopy, VkImageBlit, VkBufferImageCopy;
typedef struct {
    VkImageAspectFlags aspectMask;
    uint32_t baseMipLevel, levelCount, baseArrayLayer, layerCount;
} VkImageSubresourceRange;
typedef union {
    float float32[4];
    int32_t int32[4];
    uint32_t uint32[4];
} VkClearColorValue;
typedef struct {
    float depth;
    uint32_t stencil;
} VkClearDepthStencilValue;
typedef union {
    VkClearColorValue color;
    VkClearDepthStencilValue depthStencil;
} VkClearValue;
typedef struct {
    VkImageAspectFlags aspectMask;
    uint32_t colorAttachment;
    VkClearValue clearValue;
} VkClearAttachment;
typedef struct {
    VkRect2D rect;
    uint32_t baseArrayLayer, layerCount;
} VkClearRect;
typedef struct {
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
} VkCommandBufferBeginInfo;
typedef struct {
    VkStructureType sType;
    const void* pNext;
    VkCommandBuffer commandBuffer;
} VkCommandBufferSubmitInfo;
typedef struct {
    VkStructureType sType;
    const void* pNext;
    VkSemaphore semaphore;
    uint64_t value;
    VkPipelineStageFlags2 stageMask;
} VkSemaphoreSubmitInfo;
typedef struct {
    VkStructureType sType;
    const void* pNext;
    uint32_t waitSemaphoreInfoCount;
    const VkSemaphoreSubmitInfo* pWaitSemaphoreInfos;
    uint32_t commandBufferInfoCount;
    const VkCommandBufferSubmitInfo* pCommandBufferInfos;
    uint32_t signalSemaphoreInfoCount;
    const VkSemaphoreSubmitInfo* pSignalSemaphoreInfos;
} VkSubmitInfo2;
typedef struct {
    VkStructureType sType;
    const void* pNext;
    VkSemaphore semaphore;
    uint64_t value;
} VkSemaphoreSignalInfo;
typedef struct {
    VkStructureType sType;
    const void* pNext;
    uint32_t waitSemaphoreCount;
    const VkSemaphore* pWaitSemaphores;
    uint32_t swapchainCount;
    const VkSwapchainKHR* pSwapchains;
    const uint32_t* pImageIndices;
} VkPresentInfoKHR;
typedef struct {
    VkStructureType sType;
    const void* pNext;
    VkBuffer buffer;
    VkDeviceSize offset;
    VkConditionalRenderingFlagsEXT flags;
} VkConditionalRenderingBeginInfoEXT;
typedef struct {
    VkStructureType sType;
    const void* pNext;
    uint64_t words[4];
} VkMemoryBarrier2;
typedef struct {
    VkStructureType sType;
    const void* pNext;
    uint64_t words[8];
} VkBufferMemoryBarrier2, VkImageMemoryBarrier2;
typedef struct {
    VkStructureType sType;
    const void* pNext;
    VkDependencyFlags dependencyFlags;
    uint32_t memoryBarrierCount;
    const VkMemoryBarrier2* pMemoryBarriers;
    uint32_t bufferMemoryBarrierCount;
    const VkBufferMemoryBarrier2* pBufferMemoryBarriers;
    uint32_t imageMemoryBarrierCount;
    const VkImageMemoryBarrier2* pImageMemoryBarriers;
} VkDependencyInfo;
typedef struct {
    VkStructureType sType;
    const void* pNext;
    VkImageView imageView;
    VkImageLayout imageLayout;
    VkAttachmentLoadOp loadOp;
    VkAttachmentStoreOp storeOp;
    VkClearValue clearValue;
} VkRenderingAttachmentInfo;
typedef struct {
    VkStructureType sType;
    const void* pNext;
    VkRenderingFlags flags;
    VkRect2D renderArea;
    uint32_t layerCount, viewMask, colorAttachmentCount;
    const VkRenderingAttachmentInfo* pColorAttachments;
    const VkRenderingAttachmentInfo* pDepthAttachment;
    const VkRenderingAttachmentInfo* pStencilAttachment;
} VkRenderingInfo;
typedef struct {
    VkSampler sampler;
    VkImageView imageView;
    VkImageLayout imageLayout;
} VkDescriptorImageInfo;
typedef struct {
    VkBuffer buffer;
    VkDeviceSize offset, range;
} VkDescriptorBufferInfo;
typedef struct {
    VkStructureType sType;
    const void* pNext;
    VkDescriptorSet dstSet;
    uint32_t dstBinding, dstArrayElement, descriptorCount;
    VkDescriptorType descriptorType;
    const VkDescriptorImageInfo* pImageInfo;
    const VkDescriptorBufferInfo* pBufferInfo;
    const VkBufferView* pTexelBufferView;
} VkWriteDescriptorSet;
typedef struct {
    VkStructureType sType;
    const void* pNext;
    VkDescriptorPool descriptorPool;
    uint32_t descriptorSetCount;
    const VkDescriptorSetLayout* pSetLayouts;
} VkDescriptorSetAllocateInfo;

/* the renderer's side, as record.c uses it */
typedef struct {
    VkDevice dev;
    VkQueue gfx;
} VkState;
extern VkState g_vk;
void vk_lock(void);
void vk_unlock(void);
int vk_ok_(VkResult r, const char* what, const char* file, int line);
#define VK_OK(call) vk_ok_((call), #call, __FILE__, __LINE__)
void vk_wait_foreground(void);
VkCommandBuffer vk_cmd(void);
u32 vk_frame_slot(void);
VkDescriptorSet vk_desc_alloc(VkDescriptorSetLayout layout);
VkDescriptorPool vk_desc_pool_new(void);
VkPipeline vk_pipeline_resolve(VkPipeline h);
VkPipeline vk_pipeline_now(VkPipeline h, int* pending);
void vk_render_forget_state(void);

/* record.c's own declarations, word for word from the real vk.h */
int vk_rec_submit_failed(void);
void vk_rec_mark_failed(void);
void vk_rec_init(void);
int vk_rec_threaded(void);
u64 vk_rec_behind(void);
void vk_rec_begin(VkCommandBuffer cb);
void vk_rec_submit(VkCommandBuffer cb, const VkSemaphoreSubmitInfo* w, u32 nw, const VkSemaphoreSubmitInfo* s,
                   u32 ns);
void vk_rec_sync(void);
void vk_rec_queue(VkQueue q);
enum { VK_REC_DIRECT_OK, VK_REC_DIRECT_REFUSED, VK_REC_DIRECT_FAILED, VK_REC_DIRECT_NO_BUFFER };
typedef void (*VkRecBefore)(VkCommandBuffer cb, void* user);
typedef int (*VkRecDirect)(VkCommandBuffer cb, void* user);
int vk_rec_direct(VkCommandBuffer cb, VkRecBefore before, VkRecDirect direct, void* user);
VkResult vk_rec_present(VkSwapchainKHR sc, u32 idx, VkSemaphore wait);
VkResult vk_rec_present_result(void);
VkDescriptorSet vk_rec_new_set(VkDescriptorSetLayout layout, u32 n, const VkWriteDescriptorSet* w);
void vk_rec_frame_reset(u32 slot);
u32 vk_rec_frame_sets(u32 slot);
void rec_CmdDraw(VkCommandBuffer cb, u32 vertices, u32 instances, u32 first_vertex, u32 first_instance);
void rec_CmdEndRendering(VkCommandBuffer cb);
void rec_CmdDispatch(VkCommandBuffer cb, u32 x, u32 y, u32 z);

/* Vulkan's calls as fakes. The ones the fixture steers or reads have a function of their own. */
VkResult vkBeginCommandBuffer(VkCommandBuffer cb, const VkCommandBufferBeginInfo* info);
VkResult vkEndCommandBuffer(VkCommandBuffer cb);
VkResult vkQueueSubmit2(VkQueue queue, u32 n, const VkSubmitInfo2* submits, VkFence fence);
VkResult vkSignalSemaphore(VkDevice dev, const VkSemaphoreSignalInfo* info);
VkResult vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info);
VkResult vkAllocateDescriptorSets(VkDevice dev, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* sets);
VkResult vkResetDescriptorPool(VkDevice dev, VkDescriptorPool pool, VkFlags flags);
void vkUpdateDescriptorSets(VkDevice dev, u32 n, const VkWriteDescriptorSet* writes, u32 copies, const void* none);
void vkCmdDraw(VkCommandBuffer cb, u32 vertices, u32 instances, u32 first_vertex, u32 first_instance);
/* every other command: its name on the list of what was made, its arguments taken and looked at by no one */
void fake_command(VkCommandBuffer cb, const char* name, ...);
#define vkCmdBeginConditionalRenderingEXT(cb, ...) \
    fake_command((cb), "vkCmdBeginConditionalRenderingEXT", ##__VA_ARGS__)
#define vkCmdEndConditionalRenderingEXT(cb, ...) fake_command((cb), "vkCmdEndConditionalRenderingEXT", ##__VA_ARGS__)
#define vkCmdBeginRendering(cb, ...) fake_command((cb), "vkCmdBeginRendering", ##__VA_ARGS__)
#define vkCmdEndRendering(cb, ...) fake_command((cb), "vkCmdEndRendering", ##__VA_ARGS__)
#define vkCmdBeginTransformFeedbackEXT(cb, ...) fake_command((cb), "vkCmdBeginTransformFeedbackEXT", ##__VA_ARGS__)
#define vkCmdEndTransformFeedbackEXT(cb, ...) fake_command((cb), "vkCmdEndTransformFeedbackEXT", ##__VA_ARGS__)
#define vkCmdBindTransformFeedbackBuffersEXT(cb, ...) \
    fake_command((cb), "vkCmdBindTransformFeedbackBuffersEXT", ##__VA_ARGS__)
#define vkCmdBindDescriptorSets(cb, ...) fake_command((cb), "vkCmdBindDescriptorSets", ##__VA_ARGS__)
#define vkCmdBindPipeline(cb, ...) fake_command((cb), "vkCmdBindPipeline", ##__VA_ARGS__)
#define vkCmdBindVertexBuffers2(cb, ...) fake_command((cb), "vkCmdBindVertexBuffers2", ##__VA_ARGS__)
#define vkCmdBindIndexBuffer(cb, ...) fake_command((cb), "vkCmdBindIndexBuffer", ##__VA_ARGS__)
#define vkCmdBlitImage(cb, ...) fake_command((cb), "vkCmdBlitImage", ##__VA_ARGS__)
#define vkCmdClearAttachments(cb, ...) fake_command((cb), "vkCmdClearAttachments", ##__VA_ARGS__)
#define vkCmdClearColorImage(cb, ...) fake_command((cb), "vkCmdClearColorImage", ##__VA_ARGS__)
#define vkCmdClearDepthStencilImage(cb, ...) fake_command((cb), "vkCmdClearDepthStencilImage", ##__VA_ARGS__)
#define vkCmdCopyBuffer(cb, ...) fake_command((cb), "vkCmdCopyBuffer", ##__VA_ARGS__)
#define vkCmdCopyBufferToImage(cb, ...) fake_command((cb), "vkCmdCopyBufferToImage", ##__VA_ARGS__)
#define vkCmdCopyImage(cb, ...) fake_command((cb), "vkCmdCopyImage", ##__VA_ARGS__)
#define vkCmdCopyImageToBuffer(cb, ...) fake_command((cb), "vkCmdCopyImageToBuffer", ##__VA_ARGS__)
#define vkCmdCopyQueryPoolResults(cb, ...) fake_command((cb), "vkCmdCopyQueryPoolResults", ##__VA_ARGS__)
#define vkCmdDrawIndexed(cb, ...) fake_command((cb), "vkCmdDrawIndexed", ##__VA_ARGS__)
#define vkCmdDispatch(cb, ...) fake_command((cb), "vkCmdDispatch", ##__VA_ARGS__)
#define vkCmdPipelineBarrier2(cb, ...) fake_command((cb), "vkCmdPipelineBarrier2", ##__VA_ARGS__)
#define vkCmdPushConstants(cb, ...) fake_command((cb), "vkCmdPushConstants", ##__VA_ARGS__)
#define vkCmdBeginQuery(cb, ...) fake_command((cb), "vkCmdBeginQuery", ##__VA_ARGS__)
#define vkCmdEndQuery(cb, ...) fake_command((cb), "vkCmdEndQuery", ##__VA_ARGS__)
#define vkCmdResetQueryPool(cb, ...) fake_command((cb), "vkCmdResetQueryPool", ##__VA_ARGS__)
#define vkCmdWriteTimestamp2(cb, ...) fake_command((cb), "vkCmdWriteTimestamp2", ##__VA_ARGS__)
#define vkCmdSetAttachmentFeedbackLoopEnableEXT(cb, ...) \
    fake_command((cb), "vkCmdSetAttachmentFeedbackLoopEnableEXT", ##__VA_ARGS__)
#define vkCmdSetBlendConstants(cb, ...) fake_command((cb), "vkCmdSetBlendConstants", ##__VA_ARGS__)
#define vkCmdSetCullMode(cb, ...) fake_command((cb), "vkCmdSetCullMode", ##__VA_ARGS__)
#define vkCmdSetDepthBias(cb, ...) fake_command((cb), "vkCmdSetDepthBias", ##__VA_ARGS__)
#define vkCmdSetDepthBiasEnable(cb, ...) fake_command((cb), "vkCmdSetDepthBiasEnable", ##__VA_ARGS__)
#define vkCmdSetDepthBounds(cb, ...) fake_command((cb), "vkCmdSetDepthBounds", ##__VA_ARGS__)
#define vkCmdSetDepthBoundsTestEnable(cb, ...) fake_command((cb), "vkCmdSetDepthBoundsTestEnable", ##__VA_ARGS__)
#define vkCmdSetDepthCompareOp(cb, ...) fake_command((cb), "vkCmdSetDepthCompareOp", ##__VA_ARGS__)
#define vkCmdSetDepthTestEnable(cb, ...) fake_command((cb), "vkCmdSetDepthTestEnable", ##__VA_ARGS__)
#define vkCmdSetDepthWriteEnable(cb, ...) fake_command((cb), "vkCmdSetDepthWriteEnable", ##__VA_ARGS__)
#define vkCmdSetFrontFace(cb, ...) fake_command((cb), "vkCmdSetFrontFace", ##__VA_ARGS__)
#define vkCmdSetLineWidth(cb, ...) fake_command((cb), "vkCmdSetLineWidth", ##__VA_ARGS__)
#define vkCmdSetPrimitiveRestartEnable(cb, ...) fake_command((cb), "vkCmdSetPrimitiveRestartEnable", ##__VA_ARGS__)
#define vkCmdSetPrimitiveTopology(cb, ...) fake_command((cb), "vkCmdSetPrimitiveTopology", ##__VA_ARGS__)
#define vkCmdSetRasterizerDiscardEnable(cb, ...) fake_command((cb), "vkCmdSetRasterizerDiscardEnable", ##__VA_ARGS__)
#define vkCmdSetScissor(cb, ...) fake_command((cb), "vkCmdSetScissor", ##__VA_ARGS__)
#define vkCmdSetScissorWithCount(cb, ...) fake_command((cb), "vkCmdSetScissorWithCount", ##__VA_ARGS__)
#define vkCmdSetStencilCompareMask(cb, ...) fake_command((cb), "vkCmdSetStencilCompareMask", ##__VA_ARGS__)
#define vkCmdSetStencilOp(cb, ...) fake_command((cb), "vkCmdSetStencilOp", ##__VA_ARGS__)
#define vkCmdSetStencilReference(cb, ...) fake_command((cb), "vkCmdSetStencilReference", ##__VA_ARGS__)
#define vkCmdSetStencilTestEnable(cb, ...) fake_command((cb), "vkCmdSetStencilTestEnable", ##__VA_ARGS__)
#define vkCmdSetStencilWriteMask(cb, ...) fake_command((cb), "vkCmdSetStencilWriteMask", ##__VA_ARGS__)
#define vkCmdSetViewport(cb, ...) fake_command((cb), "vkCmdSetViewport", ##__VA_ARGS__)
#define vkCmdSetViewportWithCount(cb, ...) fake_command((cb), "vkCmdSetViewportWithCount", ##__VA_ARGS__)
#endif
