/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The host stand-ins for the spatial hand-over's code in gpu/vk/temporal.c (the reviewer's, D3-0a to D3-3b): the
   driver, the image and pipeline factories, the guest memory and its owners. gen.py puts the real code after them. */

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include "nx/gpu/vk/temporal_math.h"
#include "nx/gpu/vk/temporal_spatial.h"
#include "nx/gpu/vk/upscaler.h"
typedef uint64_t u64; typedef uint32_t u32; typedef uint8_t u8;
typedef void* VmaAllocation;
typedef struct { VkImage img; VmaAllocation mem; VkImageView view; VkFormat format; u32 w,h; } Image;
typedef struct { float reproject[16], viewport[4],depth[4],size[4],jitter[4]; } Constants;
typedef struct { VkImage img; VkImageView att; u32 sw,sh,levels; struct { VkFormat vk; } f; u64 wseq,gva,bytes; } Surf;
typedef struct { struct { u32 cbuf_mask,cbuf_max[18]; } si; u64 hash; } VkSh;
typedef struct { u64 cbind[5][18]; u32 cbind_size[5][18]; } GpuChannel;
#define MODE_TAA 2
#define ST_VS 0
#define ST_FS 4
#define SETS_BINDINGS 7
#define VK_SAMPLED_STAGES                                                                                           \
    (VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |                              \
     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
#define VK_OK(x) ((x) == VK_SUCCESS)
enum { VK_WATCH_READ, VK_WATCH_WRITE, VK_WATCH_WROTE, VK_WATCH_FREED, VK_WATCH_OVERLAP };
static struct { VkInstance instance; VkDevice dev; VkPhysicalDevice phys; VkPhysicalDeviceProperties props; } g_vk;
static struct { int mode,rect,view,rect_extras_off,reject; VkSampler point; TemporalRectGate rect_gate; u32 resolved_at; } g_t;
static u32 g_vk_present;
static int creates, views_made, deferred, pipes, sets, failure, barriers, fullscreens, fills;
static VkImage retired_images[8];
static VkImageView retired_views[8];
static uintptr_t next_id=100;
static char logs[20000];
static void log_at(const char* f,...) {
    va_list a;
    va_start(a,f);
    vsnprintf(logs+strlen(logs),sizeof logs-strlen(logs),f,a);
    va_end(a);
}
#define LOG(...) log_at(__VA_ARGS__)
#define LOG_LIMIT(n,...) log_at(__VA_ARGS__)
static VkFormatFeatureFlags features=~0u;
static const u32 temporal_spatial_fs_spv[]={0};
static const u64 temporal_spatial_fs_spv_size=4;
static void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice p,VkFormat f,VkFormatProperties* o){
    (void)p;
    (void)f;
    *o=(VkFormatProperties){
        .optimalTilingFeatures=features
    }
    ;
}
static int vk_image_create(const VkImageCreateInfo* ci,VkImage* img,VmaAllocation* mem){
    creates++; assert(ci->mipLevels==1 && ci->arrayLayers==1 && ci->samples==1);
    if(failure==creates) return 0;
    *img=(VkImage)++next_id; *mem=(void*)++next_id; return 1;
}
static VkResult vkCreateImageView(VkDevice d,const VkImageViewCreateInfo* ci,const VkAllocationCallbacks* a,
    VkImageView* v){
    (void)d;(void)a; views_made++; assert(ci->components.r==0 && ci->viewType==VK_IMAGE_VIEW_TYPE_2D);
    if(failure==10+views_made) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *v=(VkImageView)++next_id; return VK_SUCCESS;
}
static void vk_defer_image(VkImage i,VmaAllocation m,VkImageView v,VkImageView v2){
    (void)m;
    (void)v;
    (void)v2;
    assert(i || v || v2);
    assert(deferred < 8);
    retired_images[deferred] = i;
    retired_views[deferred] = v;
    deferred++;
}
static VkPipeline pipeline_make(VkFormat f,const u32* w,u64 sz,const char* what){
    (void)f;
    (void)w;
    (void)sz;
    (void)what;
    pipes++;
    return failure==20?VK_NULL_HANDLE:(VkPipeline)++next_id;
}
static VkDescriptorSet set_make(const VkImageView* v,const VkSampler* s){
    sets++;
    for(int i=0;i<SETS_BINDINGS;i++)assert(v[i]&&s[i]);
    return failure==21?VK_NULL_HANDLE:(VkDescriptorSet)++next_id;
}
static void vk_barrier(VkCommandBuffer cb,VkImage img,VkImageAspectFlags a,VkPipelineStageFlags2 ss,VkAccessFlags2 sa,
    VkImageLayout old,VkPipelineStageFlags2 ds,VkAccessFlags2 da,VkImageLayout now){
    (void)ss;
    (void)sa;
    (void)ds;
    (void)da;
    (void)old;
    (void)now;
    assert(cb && img && a==VK_IMAGE_ASPECT_COLOR_BIT);
    barriers++;
}
static Constants pushed;
static void fullscreen(VkCommandBuffer cb,VkImageView v,u32 w,u32 h,VkPipeline p,VkDescriptorSet s,const Constants* pc,
    const TemporalRect* area){
    assert(cb&&v&&w==3840&&h==2160&&p&&s&&!area);
    fullscreens++;
    pushed=*pc;
}
static void rec_CmdBlitImage(VkCommandBuffer cb,VkImage src,VkImageLayout sl,VkImage dst,VkImageLayout dl,u32 n,
    const VkImageBlit* b,VkFilter filter){
    assert(cb&&src==dst&&sl==VK_IMAGE_LAYOUT_GENERAL&&dl==sl&&n==3&&filter==VK_FILTER_NEAREST);
    for(u32 i=0;i<n;i++) {
        assert(b[i].srcSubresource.layerCount==1);
        assert(b[i].srcOffsets[1].x<=3840&&b[i].srcOffsets[1].y<=2160);
        assert(b[i].dstOffsets[0].x>=3840 || b[i].dstOffsets[0].y>=2160);
    }
    fills++;
}
static u64 vk_surf_guest_size(const Surf* s){return s->bytes;}
static u64 read_missing_at;
static int host_reads;
static void* gmem_host(u64 at,u64 n){
    (void)n;
    host_reads++;
    if(at==read_missing_at)return NULL;
    return (void*)(uintptr_t)at;
}

static int pending_compute,pending_gfx,query_fail,queries;
static u32 max_w=16384,max_h=16384;
static int vk_compute_pending(u64 a,u64 n){(void)a;(void)n;return pending_compute;}
static int vk_buf_pending_any(void){return pending_gfx;}
static int vk_buf_pending(u64 a,u64 n){(void)a;(void)n;return pending_gfx;}
static VkResult vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice p,VkFormat f,VkImageType ty,VkImageTiling ti,
    VkImageUsageFlags u,VkImageCreateFlags flags,VkImageFormatProperties* out){
 (void)p;
 (void)f;
 assert(ty==VK_IMAGE_TYPE_2D&&ti==VK_IMAGE_TILING_OPTIMAL&&!flags);
 assert((u & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
        (f == VK_FORMAT_R32G32B32A32_SFLOAT && u == (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)));
 queries++;
 if(query_fail)return VK_ERROR_FORMAT_NOT_SUPPORTED;
 *out=(VkImageFormatProperties){
     .maxExtent={max_w,max_h,1},
     .maxMipLevels=1,
     .maxArrayLayers=1,
     .sampleCounts=VK_SAMPLE_COUNT_1_BIT,
     .maxResourceSize=UINT64_MAX};return VK_SUCCESS;
}

static int g_own_any,unmapped;
static u64 owned_at,owned_bytes;
static unsigned ownership_queries;
static u64 gmmu_run(u64 a,u64 n,u64*cpu){if(unmapped)return 0;*cpu=a;u64 page=4096-(a&4095);return n<page?n:page;}
static int own_hash(u64 cpu,u64 n,u64*hash){
    ownership_queries++;
    *hash=7;
    return owned_bytes&&cpu<owned_at+owned_bytes&&owned_at<cpu+n;
}
