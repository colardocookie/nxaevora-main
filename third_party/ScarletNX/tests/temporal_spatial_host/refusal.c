/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The refusal's log (the reviewer's log9 and e618 fixtures): a listed reader whose required binding is missing or
   unreadable is class U with that reason alone, a binding other than the entry's is said as such, and the combined
   faults of one draw give every class they hold, and none from words that were not read. */
int main(void){unsigned bad=0;
 for(int which=0;which<5;which++){
  float v[512]={0},f[512]={0},origin[2]={-1,1};
  v[0x628/4]=v[0x62c/4]=f[0x628/4]=f[0x62c/4]=.5f;
  f[0x794/4]=f[0x798/4]=1;
  const int k=which==3?2:0;
  GpuChannel ch={0};VkSh vs={0},fs={0};Surf scene={.img=(VkImage)1,.wseq=2};
  vs.hash=temporal_spatial_entries[k].vs;fs.hash=temporal_spatial_entries[k].fs;
  vs.si.cbuf_mask=10;fs.si.cbuf_mask=8;vs.si.cbuf_max[1]=8;
  vs.si.cbuf_max[3]=fs.si.cbuf_max[3]=0x800;
  ch.cbind[0][1]=(u64)(uintptr_t)origin;ch.cbind_size[0][1]=8;
  ch.cbind[0][3]=(u64)(uintptr_t)v;ch.cbind[4][3]=(u64)(uintptr_t)f;
  ch.cbind_size[0][3]=sizeof v;ch.cbind_size[4][3]=sizeof f;
  memset(&g_s,0,sizeof g_s);
  g_s.asked=1;
  g_s.frame=1;
  g_s.fr=(TemporalSpatialFrame){
      1,0,0
  }
  ;
  g_s.scene=&scene;
  g_s.image=scene.img;
  g_s.wseq=2;
  g_s.f[0]=g_s.f[1]=.5;
  logs[0]=0;
  read_missing_at=0;
  if(which==1)ch.cbind[0][3]=0;
  if(which==2)ch.cbind[0][1]=0;
  if(which==3)read_missing_at=ch.cbind[4][3]+0x628;
  const u32 handle=which==4?0x2a:temporal_spatial_entries[k].handle; /* 0x28 is every entry's since revision 4 */
  TemporalSpatialDraw d=spatial_draw(k,.5);int listed=0;
  const int got=vk_temporal_spatial_decide(&ch,&vs,&fs,handle,&scene,&d,&listed);
  assert(got==(which==0)&&listed==(which==0)&&g_s.latch.off_next==(which!=0));
  const uint32_t mask=temporal_spatial_failed(k,&d,g_s.f);
  printf("host case%d mask%x given%d listed%d stop%d:\n%s",which,mask,got,listed,g_s.latch.off_next,logs);
  if(which>=1&&which<=3&&mask!=(1u<<TEMPORAL_SPATIAL_UNAVAILABLE))bad++;
  if(which==4&&!strstr(logs,"its binding 2a is not the entry's 28 (class U)"))bad++;
 }

 for(int which=0;which<6;which++){
  const int k=which==3?2:which==5?1:0;
  TemporalSpatialDraw x=spatial_draw(k,.5);const double half[2]={.5,.5};
  uint32_t want=(1u<<TEMPORAL_SPATIAL_UNAVAILABLE)|(1u<<TEMPORAL_SPATIAL_FRACTION);
  if(which==0){x.vertex_f_ok=0;x.vertex_f[0]=0;x.fragment_f[0]=.6;}
  if(which==1){x.fragment_f_ok=0;x.fragment_f[0]=0;x.vertex_f[0]=.6;}
  if(which==2){x.vertex_origin_ok=0;x.vertex_origin[0]=0;x.fragment_f[0]=.6;}
  if(which==3){x.divisor_ok=0;x.divisor[0]=0;x.fragment_f[0]=.6;}
  if(which==4){
      x.vertex_f_ok=0;
      x.vertex_f[0]=0;
      x.fragment_f[0]=.6;
      x.vertex_origin[0]=-2;
      want|=1u<<TEMPORAL_SPATIAL_ORIGIN;
  }
  if(which==5){
      x.vertex_f_ok=x.fragment_f_ok=x.vertex_origin_ok=x.divisor_ok=0;
      x.vertex_f[0]=x.fragment_f[0]=x.vertex_origin[0]=x.divisor[0]=0;
      want=0;
  }
  const uint32_t got=temporal_spatial_failed(k,&x,half);
  if(got!=want){printf("combined diagnostic host test failure case%d got%x wanted%x\n",which,got,want);bad++;}
 }
 printf("actual refusal log host tests: 5 runtime + 6 combined/unused controls, %u diagnostic test failures\n",bad);
 return bad?1:0;
}
