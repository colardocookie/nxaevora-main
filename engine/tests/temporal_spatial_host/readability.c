/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The required source of every listed reader (the reviewer's 3b fixture): for each entry and stage, each way a
   required word may be unreadable (no binding, too short, not readable, not declared, beyond the declared size) and
   one wrong value, with and without coverage and publication. An unreadable required word is class U, a wrong one N. */
static void reset(void){
    memset(&g_s,0,sizeof g_s);
    memset(&g_t,0,sizeof g_t);
    memset(&g_vk,0,sizeof g_vk);
    g_s.asked=1;
    g_t.point=(VkSampler)1;
    g_vk.props.limits.maxFramebufferWidth=16384;
    g_vk.props.limits.maxFramebufferHeight=16384;
    creates=views_made=deferred=pipes=sets=failure=barriers=fullscreens=fills=queries=query_fail=0;
    features=~0u;
    logs[0]=0;
    g_vk_watch=NULL;
    g_vk_present=0;
    max_w=max_h=16384;
    pending_compute=pending_gfx=g_own_any=unmapped=host_reads=0;
    owned_at=owned_bytes=0;
    ownership_queries=0;
}
int main(void){
 unsigned total=0,bad=0,readability_bad=0,control_bad=0;
 for(int k=0;k<3;k++)for(int stage=0;stage<=4;stage+=4){
  if(k!=0&&stage==0)continue;
  for(int mode=0;mode<7;mode++)for(int coverage=0;coverage<2;coverage++)for(int pub=0;pub<2;pub++){
   reset();read_missing_at=0;
   float v[512]={0},f[512]={0},origin[2]={-1,1};
   v[0x628/4]=v[0x62c/4]=f[0x628/4]=f[0x62c/4]=.5f;
   f[0x794/4]=f[0x798/4]=1;
   GpuChannel ch={0};VkSh vs={0},fs={0};Surf scene={.img=(VkImage)1,.wseq=2};
   vs.hash=temporal_spatial_entries[k].vs;fs.hash=temporal_spatial_entries[k].fs;
   vs.si.cbuf_mask=10;fs.si.cbuf_mask=8;vs.si.cbuf_max[1]=8;
   vs.si.cbuf_max[3]=fs.si.cbuf_max[3]=0x800;
   ch.cbind[0][1]=(u64)(uintptr_t)origin;ch.cbind_size[0][1]=8;
   ch.cbind[0][3]=(u64)(uintptr_t)v;ch.cbind[4][3]=(u64)(uintptr_t)f;
   ch.cbind_size[0][3]=sizeof v;ch.cbind_size[4][3]=sizeof f;
   VkSh*program=stage==0?&vs:&fs;float*words=stage==0?v:f;
   if(mode==1)ch.cbind[stage][3]=0;
   if(mode==2)ch.cbind_size[stage][3]=0x628;
   if(mode==3)read_missing_at=ch.cbind[stage][3]+0x628;
   if(mode==4)program->si.cbuf_mask&=~8u;
   if(mode==5)program->si.cbuf_max[3]=0x628;
   if(mode==6)words[0x628/4]=.6f;
   g_s.frame=1;
   g_s.fr=(TemporalSpatialFrame){
       1,pub,pub
   }
   ;
   g_s.scene=&scene;
   g_s.image=scene.img;
   g_s.wseq=2;
   g_s.f[0]=g_s.f[1]=.5;
   TemporalSpatialDraw d=spatial_draw(k,.5);if(coverage)d.viewport[2]=0;
   int listed=0;int got=vk_temporal_spatial_decide(&ch,&vs,&fs,temporal_spatial_entries[k].handle,&scene,&d,&listed);
   const int required=k!=1,want_u=required&&mode>=1&&mode<=5,want_n=coverage||(required&&mode==6);
   total++;
   if(listed!=!want_u||got!=(!want_u&&!want_n)||g_s.latch.off_next!=(want_u||(want_n&&pub))){
    bad++;if(want_u)readability_bad++;else control_bad++;
    printf("host test failure entry%d stage%d mode%d coverage%d published%d: given%d listed%d stop%d; wanted U%d N%d\n",
        k,stage,mode,coverage,pub,got,listed,g_s.latch.off_next,want_u,want_n);
   }
  }
 }
 printf("reviewer required-source host tests: %u cases, %u test failures (%u required-readability, %u control)\n",total,
     bad,readability_bad,control_bad);
 return bad?1:0;
}
