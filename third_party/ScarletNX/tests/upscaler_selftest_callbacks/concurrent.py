# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.')
s=(root/'third_party/ScarletNX/src/nx/gpu/vk/upscaler_selftest.c').read_text()
a=s.index('#define VALIDATION_KEPT');b=s.index('\nstatic int memory_type(',a)
head=r'''
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
typedef uint32_t u32;
typedef int32_t i32;
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
#define VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT 256
#define ST "arceus-upscaler-selftest: "
static struct {u32 messages;} g;
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
  "id","text",1};
  pthread_barrier_wait(&start);
  for(int i=0;i<20000;i++)validation_says(256,0,&d,NULL);
  return NULL;
 }
int main(void){
 pthread_t t;
 assert(!pthread_barrier_init(&start,NULL,2));
 assert(!pthread_create(&t,NULL,writer,NULL));
 pthread_barrier_wait(&start);
 for(int i=0;i<20000;i++)validation_counts();
 assert(!pthread_join(t,NULL));
 validation_counts();
 assert(g.messages==20000);
 puts("concurrent callback/summary finished");
}
'''
Path(sys.argv[2] if len(sys.argv)>2 else '/tmp/reviewer-final-concurrent.c').write_text(head+s[a:b]+tail)
print('extracted actual final callback/counts and structs; stand-in logger uses local text + atomic sink')
