/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The host stand-ins for the spatial path's draw in gpu/vk/render.c (the reviewer's revision 4 fixture): the decision,
   the sampler and view queries, the shader factory. render_gen.py puts the real code after them. */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nx/gpu/shader/shader.h"
#include "nx/gpu/vk/temporal_spatial.h"
typedef struct {u32*r3d;}GpuChannel;
typedef struct {ShaderInfo si;u64 hash;int failed;}VkSh;
typedef struct {u64 img,gva;u32 w,h,sw,sh,levels,layers,fkey;void*member,*group;struct {int depth,is_int;}f;}Surf;
typedef struct {Surf*t;u32 have;u32 tic[8],tsc[8];}TexRes;
typedef struct {u64 tm,ts,dm;u32 rm,rs,df,unfed,txq[3];}FsKey;
static Surf*g_vk_watch;
static unsigned g_vk_scale=1;
static int faults,refused,variant_fail;static u32 requested[3];static VkSh made_shader;
#define LOG(...) ((void)0)
static VkImageViewType vk_tex_view_type(const ShTex*s){(void)s;return VK_IMAGE_VIEW_TYPE_2D;}
static VkSampler vk_tex_bind_sampler(const u32*a,const u32*b,const ShTex*s,int n,const Surf*t,VkImageViewType w){
    (void)a;
    (void)b;
    (void)s;
    (void)n;
    (void)t;
    (void)w;
    return(VkSampler)1;
}
static int vk_sampler_made(VkSampler s,VkSamplerCreateInfo*m){
    (void)s;
    *m=(VkSamplerCreateInfo){
        .magFilter=VK_FILTER_NEAREST,
        .minFilter=VK_FILTER_NEAREST,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
    }
    ;
    return 1;
}
static VkPrimitiveTopology topology_vk(u32 t){(void)t;return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;}
static int tex_srgb(const u32*a,const u32*b){(void)a;(void)b;return 0;}
static VkComponentMapping tex_map(const Surf*t,const u32*p){(void)t;(void)p;return(VkComponentMapping){0};}
static int tex_map_identity(VkComponentMapping m){(void)m;return 1;}
static VkViewport vk_render_viewport_at(const u32*r,int i){(void)r;(void)i;return(VkViewport){0,1080,1920,-1080,0,1};}
static VkRect2D draw_scissor(const u32*r,int i,int normal,VkExtent2D ex){
    (void)r;
    (void)i;
    (void)normal;
    return(VkRect2D){
        {
            0,0
        }
        ,ex
    }
    ;
}
static void temporal_txq_today(unsigned w,unsigned h,unsigned scale,int target,unsigned q[2]){
    (void)scale;
    (void)target;
    q[0]=w;
    q[1]=h;
}
static int vk_temporal_spatial_decide(const GpuChannel*ch,const VkSh*vs,const VkSh*fs,u32 handle,const Surf*t,
    TemporalSpatialDraw*d,int*listed){
    (void)ch;
    (void)vs;
    (void)fs;
    (void)t;
    assert(handle==0x28&&d->vertex_count==3);
    *listed=!refused;
    return refused?TEMPORAL_SPATIAL_AS_DRAWN:TEMPORAL_SPATIAL_LARGER;
}
static void vk_temporal_spatial_fault(const char*why){(void)why;faults++;g_vk_watch=NULL;}
static VkSh*vk_shader_get(GpuChannel*ch,u64 a,u64 b,int stage,u64 tm,u64 ts,u32 rm,u32 rs,int clip,u64 dm,u32 df,
    u32 unfed,const ShXfb*xfb,const u32*txq){
    (void)ch;
    (void)a;
    (void)b;
    (void)stage;
    (void)tm;
    (void)ts;
    (void)rm;
    (void)rs;
    (void)clip;
    (void)dm;
    (void)df;
    (void)unfed;
    (void)xfb;
    memcpy(requested,txq,sizeof requested);
    if(variant_fail&&txq[0])return NULL;
    memcpy(made_shader.si.txq_fixed,txq,sizeof requested);
    return&made_shader;
}

/* the replacement's view with the game view's mapping (d3f76dbd): one, unless a case takes it away */
static int no_view;
VkImageView vk_temporal_spatial_view_for(const VkComponentMapping* map) {
    (void)map;
    return no_view ? VK_NULL_HANDLE : (VkImageView)7;
}
