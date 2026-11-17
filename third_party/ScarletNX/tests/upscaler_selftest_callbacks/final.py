# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
from pathlib import Path
import sys,re
root=Path(sys.argv[1] if len(sys.argv)>1 else '.')
s=(root/'third_party/ScarletNX/src/nx/gpu/vk/upscaler_selftest.c').read_text()
a=s.index('#define VALIDATION_KEPT');b=s.index('\nstatic int memory_type(',a)
def fn(name):
 m=re.search(r'^(?:static )?[\w* ]+\b'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 p=m.end();d=1
 while d:d+=(s[p]=='{')-(s[p]=='}');p+=1
 return s[m.start():p]+'\n'
head=r'''
/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "nx/gpu/vk/upscaler_selftest.h"
#include "types.h"
#define VK_OK(x) ((x)==0)
#define VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME "VK_EXT_validation_features"
#define SNX_VK_LAYER_NO_MEMORY 5
#define ST "arceus-upscaler-selftest: "
enum { SNX_UPSCALER_OK = 0 };
typedef struct { int (*shutdown)(int device); } HostApi;
static const HostApi api = {0};
static struct {
 u32 messages,contexts;
 int validation,unknown,device,limit_asked,checks_asked,checks_why,checks_result;
 int dlss,halted,known,ended_wrong;
 char step[160],halted_in[160];
 const HostApi* api;
} g;
static char output[4000000];static size_t used;static void(*hook)(const char*);
static void log_line(const char *fmt,...){
 char line[2048];
 va_list ap;
 va_start(ap,fmt);
 int n=vsnprintf(line,sizeof line,fmt,ap);
 va_end(ap);
 assert(n>=0&&(size_t)n<sizeof line);
 assert((size_t)n<sizeof output-used);
 memcpy(output+used,line,(size_t)n+1);
 used+=(size_t)n;
 if(hook)hook(fmt);
}
#define LOG(...) log_line(__VA_ARGS__)
'''
stubs=r'''
static int wait_calls,wait_result,shutdown_calls,shutdown_result,shutdown_message;
static void emit(const char*n,int id,const char*t);
static int vkDeviceWaitIdle(int device){(void)device;wait_calls++;return wait_result;}
static const char* validation_missing(void){return "not available";}
static int shutdown_device(int device){
 (void)device;
 assert(wait_calls==1 && wait_result==VK_SUCCESS && !g.contexts);
 shutdown_calls++;
 if(shutdown_message)emit("shutdown",3,"shutdown callback");
 return shutdown_result;
}
static const HostApi started_api={shutdown_device};
'''
# The final case7 tail, including actual wait/provenance, count snapshot, late check and RESULT.
body=fn('vk_upscaler_selftest')
a_tail=body.index('    const int was_unknown = g.unknown;')
final_tail='static int finish(int pass){\n'+body[a_tail:body.rfind('}')]+ '}\n'
test=r'''
static void reset(void){
 memset(&g,0,sizeof g);
 g.api=&api;
 memset(&g_said,0,sizeof g_said);
 g_said_lock=0;
 used=0;
 output[0]=0;
 wait_calls=wait_result=0;
 shutdown_calls=shutdown_result=shutdown_message=0;
 hook=NULL;
}
static void emit(const char*n,int id,const char*t){
 VkDebugUtilsMessengerCallbackDataEXT d={
  .pMessageIdName=n,.pMessage=t,.messageIdNumber=id};
  validation_says(256,0,&d,NULL);
 }
static int nibble(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static size_t decode(const char*s,size_t n,unsigned char*out){
 size_t p=0,k=0;
 while(p<n){
  unsigned char c=(unsigned char)s[p++];
  if(c=='\\'){
   assert(p<n);
   c=(unsigned char)s[p++];
   if(c=='n')c='\n';
   else if(c=='r')c='\r';
   else if(c=='t')c='\t';
   else if(c=='x'){
    assert(p+2<=n);
    int hi=nibble(s[p++]),lo=nibble(s[p++]);
    assert(hi>=0&&lo>=0);
    c=(unsigned char)(16*hi+lo);
   }
   else assert(c=='\\');
  }
  out[k++]=c;
 }
 return k;
}
static void check_lines(void){
 const char*p=output;
 while(*p){
  assert(!strncmp(p,ST,strlen(ST)));
  const char*e=strchr(p,'\n');
  assert(e);
  assert((size_t)(e-p)<2047);
  for(const char*q=p;q<e;q++)assert((unsigned char)*q>=32&&(unsigned char)*q<127);
  p=e+1;
 }
}
static void late_on_case(const char*fmt){if(strstr(fmt,"case 7:")){hook=NULL;emit("late",9,"late callback");}}
static VkDebugUtilsMessengerCallbackDataEXT own_data(void){
 static const VkDebugUtilsLabelEXT label={SELFTEST_NGX_REGION};
 static const VkDebugUtilsObjectNameInfoEXT object={VK_OBJECT_TYPE_IMAGE,SELFTEST_NGX_IMAGE};
 return (VkDebugUtilsMessengerCallbackDataEXT){
  .pMessageIdName=SELFTEST_NGX_ID,.pMessage="own callback",.messageIdNumber=9600,
  .cmdBufLabelCount=1,.objectCount=1,.pCmdBufLabels=&label,.pObjects=&object};
}
static void emit_own(void){
 VkDebugUtilsMessengerCallbackDataEXT d=own_data();
 validation_says(256,0,&d,NULL);
}
static void late_own_on_case(const char*fmt){if(strstr(fmt,"case 7:")){hook=NULL;emit_own();}}
int main(void){
 unsigned own=0;
 reset();
 VkDebugUtilsMessengerCallbackDataEXT info={
  .pMessageIdName="id",.pMessage="ignored",.messageIdNumber=1};
  validation_says(16,0,&info,NULL);
  assert(!g.messages&&!used);
 emit("id",1,"first");
 emit("id",1,"second");
 emit("id",2,"third");
 assert(g.messages==3&&g_said.ids==2&&g_said.id[0].count==2);
 reset();
 char prefix_a[128],prefix_b[128];
 memset(prefix_a,'p',126);
 prefix_a[126]='A';
 prefix_a[127]=0;
 memcpy(prefix_b,prefix_a,sizeof prefix_a);
 prefix_b[126]='B';
 emit(prefix_a,42,"first retained id");
 emit(prefix_b,42,"second retained id");
 emit(prefix_a,42,"repeat first id");
 assert(g.messages==3&&g_said.ids==2&&g_said.id[0].count==2&&g_said.id[1].count==1);
 assert(!g_said.unlisted&&!g_said.overlong);
 assert(validation_counts(&own)==3&&strstr(output,"counts add up"));
 reset();
 char a[140],b[140];
 memset(a,'a',139);
 a[139]=0;
 memcpy(b,a,140);
 a[127]='X';
 b[127]='Y';
 emit(a,42,"a");
 emit(b,42,"b");
 assert(g_said.ids==0&&g_said.overlong==2);
 assert(validation_counts(&own)==2&&strstr(output,"name is cut here"));
 reset();
 char name[32];
 for(int i=0;i<65;i++){
  snprintf(name,sizeof name,"id%d",i);
  emit(name,i,"text");
 }
 emit("id0",0,"repeat");
 assert(g.messages==66&&g_said.ids==64&&g_said.unlisted==1&&g_said.id[0].count==2);
 assert(validation_counts(&own)==66&&strstr(output,"counts add up"));
 reset();
 char text[4100];
 memset(text,'q',4099);
 text[4099]=0;
 for(int i=0;i<257;i++)emit("rule",1,text);
 assert(g.messages==257&&g_said.omitted==1&&g_said.truncated==256&&g_said.id[0].count==257);
 assert(validation_counts(&own)==257&&strstr(output,"more than 256")&&strstr(output,"counts add up"));
 check_lines();
 reset();validation_says(256,0,NULL,NULL);assert(g_said.nameless==1&&validation_counts(&own)==1);check_lines();
 reset();
 emit("rule",1,"descriptor [Set 0, Binding 128, variable \"rw_a\"] and variable \"rw_b\"");
 emit("rule",1,"descriptor [variable \"rw_a\"] again");
 emit("rule",1,"(Warning - This VUID has now been reported 10 times, which is the duplicate_message_limit value,"
  " this will be the last time reporting it)");
 emit("rule",1,"descriptor [variable \"duplicate_message_limit\"] only names something of that name");
 emit("other",2,"variable \"a\nb\" with a line break in its name");
 assert(g_said.variables.callbacks==5&&g_said.variables.names==4&&g_said.variables.unmatched==1);
 assert(g_said.variables.name[0].count==2&&g_said.variables.name[1].count==1&&g_said.stops==1);
 g.limit_asked=1;
 assert(validation_counts(&own)==5&&strstr(output,"asked off (enable_message_limit = false)"));
 assert(strstr(output,"The most callbacks one listed message id had is 4"));
 assert(strstr(output,"this will be the last time reporting it\"): 1"));
 assert(strstr(output,"validation variable name rw_a: in 2 callbacks")&&strstr(output,"No limit was met"));
 assert(strstr(output,"validation variable name a\\nb: in 1 callbacks"));
 check_lines();
 reset();
 emit("rule",1,"variable \"never closed");
 assert(validation_counts(&own)==1&&strstr(output,"A LIMIT WAS MET")&&strstr(output,"not asked off"));
 check_lines();
 reset();
 emit("a\nb",42,"first");
 emit("a\\nb",42,"second");
 assert(g_said.ids==2);
 validation_counts(&own);
 assert(strstr(output,"a\\nb")&&strstr(output,"a\\\\nb"));
 check_lines();
 for(unsigned v=0;v<256;v++){
  char mark[4];
  unsigned char raw[4];
  u32 n=validation_mark((unsigned char)v,mark);
  assert(n&&n<=4);
  for(u32 i=0;i<n;i++)assert((unsigned char)mark[i]>=32&&(unsigned char)mark[i]<127);
  assert(decode(mark,n,raw)==1&&raw[0]==v);
 }
 reset();
 unsigned char raw[4096],back[4096];
 for(unsigned i=0;i<4096;i++)raw[i]=(unsigned char)(i%255+1);
 u32 pieces=validation_pieces((char*)raw,sizeof raw,7,0,0);
 assert(pieces>1);
 assert(validation_pieces((char*)raw,sizeof raw,7,pieces,1)==pieces);
 check_lines();
 size_t got=0;
 const char*p=output;
 while(*p){
  const char*body=strstr(p,": ");
  body=strstr(body+2,": ")+2;
  const char*e=strchr(body,'\n');
  got+=decode(body,(size_t)(e-body),back+got);
  p=e+1;
 }
 assert(got==sizeof raw&&!memcmp(raw,back,sizeof raw));
 reset();
 memset(text,'a',1499);
 text[1499]=(char)0xc3;
 text[1500]=(char)0xa9;
 text[1501]=0;
 emit("utf8",4,text);
 validation_counts(&own);
 check_lines();
 assert(strstr(output,"\\xc3\\xa9"));
 puts("callback identity/caps/unknowns, all256 byte marks,4096-byte piece roundtrip, UTF8 and prefixed ASCII "
     "controls pass");
 unsigned cases=0;
 for(int initial=0;initial<2;initial++)for(int validation=0;validation<2;validation++)
  for(int unknown_before=0;unknown_before<2;unknown_before++)for(int wait_ok=0;wait_ok<2;wait_ok++)
   for(int prior=0;prior<2;prior++)for(int late=0;late<2;late++)for(int why=0;why<7;why++){
  reset();
  g.validation=validation;
  /* why 0: the synchronisation checks were asked for. 1 to 6: each way they were not */
  g.checks_asked=!why;g.checks_why=why;g.checks_result=-3;
  g.unknown=unknown_before;
  wait_result=wait_ok?0:-13;
  if(prior)emit("prior",1,"before count");
  if(late)hook=late_on_case;
  int r=finish(initial);int expected=initial&&validation&&!why&&!unknown_before&&wait_ok&&!prior&&!late;
  assert(r==!expected&&wait_calls==!unknown_before);check_lines();
  if(unknown_before)assert(strstr(output,"wait for it was not tried"));
  else if(!wait_ok)assert(strstr(output,"was made and failed with the result -13"));
  if(late)assert(strstr(output,"more validation callbacks arrived"));
  /* The checks not asked for: never a pass in case 7's lines or the RESULT, and the reason is said, also in
     a run that stopped. A result is said only for a call that was made. Without the layer nothing is said. */
  static const char*const reason[7]={"","read whole, does not hold it","stayed incomplete","the count of the",
   "the list of the layer's extensions failed","no memory","no room"};
  const char*line=strstr(output,"case 7: the synchronisation checks were not asked for");
  assert(!line==!(validation&&why));
  if(line){
   const char*end=strchr(line,'\n');
   const char*at=strstr(line,reason[why]);
   const char*result=strstr(line,"the last call's result -3");
   assert(at&&at<end&&!strstr(output,": PASS")&&strstr(output,"RESULT: FAIL"));
   assert(!(result&&result<end)==(why==5||why==6));
   /* a wait that failed stops the run as well */
   if(!unknown_before&&wait_ok)assert(strstr(output,"case 7: NOT MET"));
   else assert(strstr(output,"case 7: NOT JUDGED")&&!strstr(output,"case 7: NOT MET"));
  }
  else assert(!strstr(output,"NOT MET"));
  assert(!strstr(output,"RESULT: PASS")==!expected);
  cases++;
 }
 printf("%u actual finaltail controls pass: wait independent of pass, failed/skipped provenance, late FAIL and "
     "common snapshot\n",cases);
 /* The actual callback's label/object wiring and count snapshot, including oversized lists never read. */
 reset();
 emit_own();
 assert(validation_counts(&own)==1 && own==1);
 for(int changed=0;changed<5;changed++){
  VkDebugUtilsMessengerCallbackDataEXT d=own_data();
  VkDebugUtilsObjectNameInfoEXT object={VK_OBJECT_TYPE_IMAGE,"host image"};
  if(changed==0)d.pMessageIdName="another id";
  if(changed==1)d.cmdBufLabelCount=0;
  if(changed==2)d.pObjects=&object;
  if(changed==3)d.cmdBufLabelCount=17,d.pCmdBufLabels=NULL;
  if(changed==4)d.objectCount=17,d.pObjects=NULL;
  validation_says(256,0,&d,NULL);
 }
 assert(validation_counts(&own)==6 && own==1);
 for(int late=0;late<3;late++)for(int known=0;known<2;known++){
  reset();
  g.dlss=g.validation=g.checks_asked=1;
  g.known=known;
  emit_own();
  if(late==1)hook=late_own_on_case;
  if(late==2)hook=late_on_case;
  assert(finish(1)==(known || late==2));
  if(late==1)assert(strstr(output,"more callbacks of NGX's own")&&!strstr(output,"more validation callbacks"));
  if(late==2)assert(strstr(output,"more validation callbacks"));
  if(known && late!=2)assert(strstr(output,"1 known library deviation alone"));
  check_lines();
 }
 for(int unknown_before=0;unknown_before<2;unknown_before++)for(int wait_ok=0;wait_ok<2;wait_ok++)
  for(int contexts=0;contexts<2;contexts++)for(int failed=0;failed<2;failed++)for(int message=0;message<2;message++){
  reset();
  g.api=&started_api;
  g.validation=g.checks_asked=1;
  g.unknown=unknown_before;
  g.contexts=contexts;
  wait_result=wait_ok?0:-13;
  shutdown_result=failed?2:0;
  shutdown_message=message;
  int called=!unknown_before && wait_ok && !contexts;
  assert(finish(1)==!(called && !failed && !message));
  assert(shutdown_calls==called);
  assert(g.messages==(unsigned)(called && message));
  if(called && message)assert(strstr(output,"case 7: 1 validation messages: FAIL"));
 }
 reset();
 g.validation=g.checks_asked=1;
 g.halted=1;
 assert(finish(1)==1 && strstr(output,"an evaluate answered UNKNOWN: FAIL"));
 reset();
 g.validation=g.checks_asked=1;
 g.ended_wrong=1;
 assert(finish(1)==1 && strstr(output,"NO: FAIL"));
 puts("current callback wiring, own/late counts, known reset, shutdown and stopped-run controls pass");
}
'''
out=Path(sys.argv[2] if len(sys.argv)>2 else '/tmp/reviewer-validation-final.c')
out.write_text(head+s[a:b]+stubs+fn('checks_missing')+fn('stop_noted')+fn('unknown')+final_tail+test)
print('extracted actual callback/counts + unknown + complete final wait/case7/RESULT tail from',root)
