# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.')
s=(root/'src/nx/gpu/vk/upscaler_selftest.c').read_text()
a=s.index('#define VALIDATION_KEPT');b=s.index('\nstatic int memory_type(',a)
head=r'''
/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "nx/gpu/vk/upscaler_selftest.h"
#include <pthread.h>
#include "types.h"
#define ST "arceus-upscaler-selftest: "
static struct {u32 messages;int limit_asked;} g;
static unsigned sink;
static __attribute__((noinline)) void log_line(const char *fmt,...){
 char out[2048];
 va_list ap;
 va_start(ap,fmt);
 int n=vsnprintf(out,sizeof out,fmt,ap);
 va_end(ap);
 assert(n>=0);
 __atomic_fetch_add(&sink,(unsigned)n,__ATOMIC_RELAXED);
}
#define LOG(...) log_line(__VA_ARGS__)
'''
tail=r'''
static pthread_barrier_t start;
static void* writer(void*unused){
 (void)unused;
 VkDebugUtilsMessengerCallbackDataEXT d={
  .pMessageIdName="id",.pMessage="text",.messageIdNumber=1};
 const VkDebugUtilsLabelEXT label={SELFTEST_NGX_REGION};
 const VkDebugUtilsObjectNameInfoEXT object={VK_OBJECT_TYPE_IMAGE,SELFTEST_NGX_IMAGE};
 VkDebugUtilsMessengerCallbackDataEXT own={
  .pMessageIdName=SELFTEST_NGX_ID,.pMessage="own callback",.messageIdNumber=9600,
  .cmdBufLabelCount=1,.objectCount=1,.pCmdBufLabels=&label,.pObjects=&object};
  pthread_barrier_wait(&start);
 for(int i=0;i<20000;i++)validation_says(256,0,i%2?&d:&own,NULL);
  return NULL;
 }
int main(void){
 unsigned own=0;
 pthread_t t;
 assert(!pthread_barrier_init(&start,NULL,2));
 assert(!pthread_create(&t,NULL,writer,NULL));
 pthread_barrier_wait(&start);
 for(int i=0;i<20000;i++){
  unsigned total=validation_counts(&own);
  assert(own==(total+1)/2);
 }
 assert(!pthread_join(t,NULL));
 validation_counts(&own);
 assert(g.messages==20000 && own==10000);
 puts("concurrent callback/summary finished");
}
'''
Path(sys.argv[2] if len(sys.argv)>2 else '/tmp/reviewer-final-concurrent.c').write_text(head+s[a:b]+tail)
print('extracted actual final callback/counts and structs; stand-in logger uses local text + atomic sink')
