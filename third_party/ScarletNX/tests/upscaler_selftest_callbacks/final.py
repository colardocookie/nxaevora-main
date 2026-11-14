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
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
typedef uint32_t u32;
typedef int32_t i32;
typedef int VkResult;
typedef int VkBool32;
typedef unsigned VkDebugUtilsMessageSeverityFlagBitsEXT;
typedef unsigned VkDebugUtilsMessageTypeFlagsEXT;
typedef struct {
 const char *pMessageIdName;
 const char *pMessage;
 int32_t messageIdNumber;
}
VkDebugUtilsMessengerCallbackDataEXT;
#define VKAPI_ATTR
#define VKAPI_CALL
#define VK_FALSE 0
#define VK_SUCCESS 0
#define VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT 256
#define VK_OK(x) ((x)==0)
#define ST "arceus-upscaler-selftest: "
static struct {u32 messages;int validation,unknown,device;} g;
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
static int wait_calls,wait_result;
static int vkDeviceWaitIdle(int device){(void)device;wait_calls++;return wait_result;}
static const char* validation_missing(void){return "not available";}
'''
# The final case7 tail, including actual wait/provenance, count snapshot, late check and RESULT.
a_tail=s.index('    edge_response(0);',s.index('int vk_upscaler_selftest('));a_tail=s.index('\n',a_tail)+1
final_tail='static int finish(int pass){\n'+s[a_tail:s.rfind('}')]+'}\n'
test=r'''
static void reset(void){
 memset(&g,0,sizeof g);
 memset(&g_said,0,sizeof g_said);
 g_said_lock=0;
 used=0;
 output[0]=0;
 wait_calls=wait_result=0;
 hook=NULL;
}
static void emit(const char*n,int id,const char*t){
 VkDebugUtilsMessengerCallbackDataEXT d={
  n,t,id};
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
int main(void){
 reset();
 VkDebugUtilsMessengerCallbackDataEXT info={
  "id","ignored",1};
  validation_says(16,0,&info,NULL);
  assert(!g.messages&&!used);
 emit("id",1,"first");
 emit("id",1,"second");
 emit("id",2,"third");
 assert(g.messages==3&&g_said.ids==2&&g_said.id[0].count==2);
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
 assert(validation_counts()==2&&strstr(output,"name is cut here"));
 reset();
 char name[32];
 for(int i=0;i<65;i++){
  snprintf(name,sizeof name,"id%d",i);
  emit(name,i,"text");
 }
 emit("id0",0,"repeat");
 assert(g.messages==66&&g_said.ids==64&&g_said.unlisted==1&&g_said.id[0].count==2);
 assert(validation_counts()==66&&strstr(output,"counts add up"));
 reset();
 char text[4100];
 memset(text,'q',4099);
 text[4099]=0;
 for(int i=0;i<257;i++)emit("rule",1,text);
 assert(g.messages==257&&g_said.omitted==1&&g_said.truncated==256&&g_said.id[0].count==257);
 assert(validation_counts()==257&&strstr(output,"more than 256")&&strstr(output,"counts add up"));
 check_lines();
 reset();validation_says(256,0,NULL,NULL);assert(g_said.nameless==1&&validation_counts()==1);check_lines();
 reset();
 emit("a\nb",42,"first");
 emit("a\\nb",42,"second");
 assert(g_said.ids==2);
 validation_counts();
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
 validation_counts();
 check_lines();
 assert(strstr(output,"\\xc3\\xa9"));
 puts("callback identity/caps/unknowns, all256 byte marks,4096-byte piece roundtrip, UTF8 and prefixed ASCII "
     "controls pass");
 unsigned cases=0;
 for(int initial=0;initial<2;initial++)for(int validation=0;validation<2;validation++)
  for(int unknown_before=0;unknown_before<2;unknown_before++)for(int wait_ok=0;wait_ok<2;wait_ok++)
   for(int prior=0;prior<2;prior++)for(int late=0;late<2;late++){
  reset();
  g.validation=validation;
  g.unknown=unknown_before;
  wait_result=wait_ok?0:-13;
  if(prior)emit("prior",1,"before count");
  if(late)hook=late_on_case;
  int r=finish(initial);int expected=initial&&validation&&!unknown_before&&wait_ok&&!prior&&!late;
  assert(r==!expected&&wait_calls==!unknown_before);check_lines();
  if(unknown_before)assert(strstr(output,"wait for it was not tried"));
  else if(!wait_ok)assert(strstr(output,"was made and failed with the result -13"));
  if(late)assert(strstr(output,"more validation callbacks arrived"));
  cases++;
 }
 printf("%u actual finaltail controls pass: wait independent of pass, failed/skipped provenance, late FAIL and "
     "common snapshot\n",cases);
}
'''
out=Path(sys.argv[2] if len(sys.argv)>2 else '/tmp/reviewer-validation-final.c')
out.write_text(head+s[a:b]+stubs+fn('unknown')+final_tail+test)
print('extracted actual callback/counts + unknown + complete final wait/case7/RESULT tail from',root)
