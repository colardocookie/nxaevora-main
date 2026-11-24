/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The larger texture and its pair (the reviewer's revision 4 fixture): the keyed variant only where the scene's slot is
   queried, a variant that cannot be made or a fault after the decision leaving the ordinary pair with no key, a second
   scene slot refused, a refused decision inventing no key, and a failed ordinary program turning the mode off. */
int main(void){unsigned controls=0;
 u32 regs[0x900]={
     0
 }
 ;
 GpuChannel ch={
     regs
 }
 ;
 Surf scene={
     .img=1,.gva=0x1000,.w=1920,.h=1080,.sw=1920,.sh=1080,.levels=1,.layers=1
 }
 ,other=scene;
 other.gva=0x2000;
 VkSh ordinary={
     .hash=0x21fbfbecbf90f4a3ull
 }
 ,vs={
     0
 }
 ;
 ordinary.si.ntex=2;
 ordinary.si.tex[0].cbuf_off=0x2a;
 ordinary.si.tex[1].cbuf_off=0x28;
 TexRes res[2]={{.t=&other,.have=1},{.t=&scene,.have=1}};Surf*rt[1]={&scene};
 for(int mode=0;mode<6;mode++){
  FsKey key={
      0
  }
  ;
  g_vk_watch=&scene;
  faults=refused=variant_fail=0;
  memset(requested,0,sizeof requested);
  memset(&made_shader,0,sizeof made_shader);
  ordinary.si.tex_txq_mask=mode==0||mode==5?0:mode==1?1:2;
  spatial_draw(&ch,&vs,&ordinary,0,res,0,0,0,3,0,1,rt,1,NULL,&key);
  const int keyed=mode>=2&&mode<=4;
  assert(g_spatial.larger==2&&g_spatial.keyed==keyed);
  assert(key.txq[0]==(keyed?0x29u:0));if(keyed)assert(key.txq[1]==1920&&key.txq[2]==1080);
  if(mode==3)variant_fail=1;
  VkSh*fs=fs_variant(&ch,0,&ordinary,&key);
  if(keyed)assert(requested[0]==0x29&&requested[1]==1920&&requested[2]==1080);else assert(fs==&ordinary&&!requested[0]);
  if(mode==4||mode==5)g_vk_watch=NULL;
  fs=final_pair(&ch,&ordinary,fs,&key);
  if(mode<=2)assert(!faults&&g_spatial.larger==2&&fs);
  else assert(faults==1&&!g_spatial.larger&&!g_spatial.keyed&&!key.txq[0]&&fs==&ordinary);
  controls++;
 }
 // A second scene slot is rejected independently of whether the first one needed a key.
 ordinary.si.tex[0].cbuf_off=0x28;res[0].t=&scene;
 for(int queried=0;queried<2;queried++){
  FsKey key={0};g_vk_watch=&scene;faults=refused=variant_fail=0;ordinary.si.tex_txq_mask=queried?1:0;
  spatial_draw(&ch,&vs,&ordinary,0,res,0,0,0,3,0,1,rt,1,NULL,&key);
  assert(faults==1&&!g_vk_watch&&!g_spatial.larger&&!g_spatial.keyed&&!key.txq[0]);controls++;
 }
 // Refused decision leaves ordinary pair and never invents a key.
 FsKey key={0};g_vk_watch=&scene;faults=0;refused=1;
 spatial_draw(&ch,&vs,&ordinary,0,res,0,0,0,3,0,1,rt,1,NULL,&key);
 assert(!faults&&!g_spatial.larger&&!g_spatial.keyed&&!key.txq[0]);controls++;

 // Ordinary shader failed after translation: admission cannot leave spatial enabled.
 ordinary.si.tex[0].cbuf_off=0x2a;
 res[0].t=&other;
 ordinary.si.tex_txq_mask=0;
 ordinary.failed=1;
 refused=0;
 faults=0;
 g_vk_watch=&scene;
 key=(FsKey){
     0
 }
 ;
 spatial_draw(&ch,&vs,&ordinary,0,res,0,0,0,3,0,1,rt,1,NULL,&key);
 VkSh*bad=final_pair(&ch,&ordinary,&ordinary,&key);
 printf("failed ordinary host case: faults%d larger%llu failed-return%d\n",faults,(unsigned long long)g_spatial.larger,
     bad&&bad->failed);
 const int policy_bad=!(faults==1&&!g_spatial.larger&&!g_spatial.keyed&&!key.txq[0]);
 if(policy_bad)
  puts("host test failure: failed ordinary program accepted by spatial final pair without immediate fault");
 printf("actual renderer spatial decision/key/final pair: %u host controls PASS "
        "(decision, driver and shader factory stand-ins)\n",
     controls);
 return policy_bad?1:0;
}
