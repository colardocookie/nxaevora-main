#!/usr/bin/env python3
"""JIT the exact production fshr-based rotate emitter, including zero counts."""
from pathlib import Path
import sys
s=(Path(__file__).resolve().parents[2]/'src/common/cpu/recompiler/llvm/ir_helpers.c').read_text()
a=s.index('static void build_ror(');open_=s.index('{',a);i=open_+1;nest=1
while nest:
 if s[i]=='{':nest+=1
 if s[i]=='}':nest-=1
 i+=1
body=s[a:i]
code=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void* V;
typedef V LLVMValueRef;
typedef V LLVMTypeRef;
typedef V LLVMContextRef;
typedef V LLVMModuleRef;
typedef V LLVMBuilderRef;
typedef V LLVMExecutionEngineRef;
typedef struct {LLVMContextRef ctx;LLVMModuleRef mod;LLVMBuilderRef b;LLVMTypeRef t_i32,t_i64;} SnxIr;
extern LLVMContextRef LLVMContextCreate(void);
extern LLVMModuleRef LLVMModuleCreateWithNameInContext(const char*,LLVMContextRef);
extern LLVMBuilderRef LLVMCreateBuilderInContext(LLVMContextRef);
extern LLVMTypeRef LLVMInt32TypeInContext(LLVMContextRef),LLVMInt64TypeInContext(LLVMContextRef),LLVMFunctionType(LLVMTypeRef,LLVMTypeRef*,unsigned,int);
extern V LLVMAddFunction(LLVMModuleRef,const char*,LLVMTypeRef),LLVMGetParam(V,unsigned),LLVMBuildRet(LLVMBuilderRef,V),LLVMConstInt(LLVMTypeRef,uint64_t,int);
extern V LLVMBuildTrunc(LLVMBuilderRef,V,LLVMTypeRef,const char*),LLVMBuildZExt(LLVMBuilderRef,V,LLVMTypeRef,const char*);
extern unsigned LLVMGetIntTypeWidth(LLVMTypeRef);
extern LLVMTypeRef LLVMTypeOf(V);
extern void* LLVMAppendBasicBlockInContext(LLVMContextRef,V,const char*);
extern void LLVMPositionBuilderAtEnd(LLVMBuilderRef,void*);
extern int LLVMVerifyModule(LLVMModuleRef,int,char**),LLVMCreateExecutionEngineForModule(LLVMExecutionEngineRef*,LLVMModuleRef,char**);
extern uint64_t LLVMGetFunctionAddress(LLVMExecutionEngineRef,const char*);
extern unsigned LLVMLookupIntrinsicID(const char*,size_t);
extern V LLVMGetIntrinsicDeclaration(LLVMModuleRef,unsigned,LLVMTypeRef*,size_t),LLVMBuildCall2(LLVMBuilderRef,LLVMTypeRef,V,V*,unsigned,const char*);
extern LLVMTypeRef LLVMIntrinsicGetType(LLVMContextRef,unsigned,LLVMTypeRef*,size_t);
extern void LLVMLinkInMCJIT(void),LLVMInitializeX86TargetInfo(void),LLVMInitializeX86Target(void),LLVMInitializeX86TargetMC(void),LLVMInitializeX86AsmPrinter(void);
static V ir_cast(SnxIr*ir,V v,LLVMTypeRef t,int sign){(void)sign;unsigned from=LLVMGetIntTypeWidth(LLVMTypeOf(v)),to=LLVMGetIntTypeWidth(t);return from==to?v:from<to?LLVMBuildZExt(ir->b,v,t,""):LLVMBuildTrunc(ir->b,v,t,"");}
static V ir_intrinsic(SnxIr*ir,const char* name,LLVMTypeRef type,V*args,unsigned count){unsigned id=LLVMLookupIntrinsicID(name,strlen(name));V func=LLVMGetIntrinsicDeclaration(ir->mod,id,&type,1);return LLVMBuildCall2(ir->b,LLVMIntrinsicGetType(ir->ctx,id,&type,1),func,args,count,"");}
#define B ir->b
#define P(i) LLVMGetParam(function,i)
''' + body + r'''
int main(void){
    LLVMInitializeX86TargetInfo();LLVMInitializeX86Target();LLVMInitializeX86TargetMC();LLVMInitializeX86AsmPrinter();LLVMLinkInMCJIT();
    SnxIr ir={0};ir.ctx=LLVMContextCreate();ir.mod=LLVMModuleCreateWithNameInContext("rotates",ir.ctx);ir.b=LLVMCreateBuilderInContext(ir.ctx);
    ir.t_i32=LLVMInt32TypeInContext(ir.ctx);ir.t_i64=LLVMInt64TypeInContext(ir.ctx);
    for(unsigned w=32;w<=64;w+=32){
        LLVMTypeRef type=w==32?ir.t_i32:ir.t_i64;
        LLVMTypeRef args[]={type,ir.t_i32};
        V fn=LLVMAddFunction(ir.mod,w==32?"rot32":"rot64",LLVMFunctionType(type,args,2,0));
        LLVMPositionBuilderAtEnd(ir.b,LLVMAppendBasicBlockInContext(ir.ctx,fn,"entry"));
        build_ror(&ir,fn,type,w);
    }
    char*err=NULL;if(LLVMVerifyModule(ir.mod,2,&err)){fprintf(stderr,"IR invalid: %s\n",err);return 2;}
    LLVMExecutionEngineRef jit=NULL;
    if(LLVMCreateExecutionEngineForModule(&jit,ir.mod,&err)){fprintf(stderr,"JIT %s\n",err);return 3;}
    uint64_t(*r64)(uint64_t,uint32_t)=(void*)(uintptr_t)LLVMGetFunctionAddress(jit,"rot64");
    uint32_t(*r32)(uint32_t,uint32_t)=(void*)(uintptr_t)LLVMGetFunctionAddress(jit,"rot32");
    unsigned n=0;
    for(uint64_t x=0;x<2500;++x)for(unsigned shift=0;shift<128;++shift){
        uint64_t value=x*0x9e3779b97f4a7c15ULL;
        uint32_t v32=(uint32_t)value;
        unsigned s64=shift&63,s32=shift&31;
        uint64_t expect64=s64?(value>>s64)|(value<<(64-s64)):value;
        uint32_t expect32=s32?(v32>>s32)|(v32<<(32-s32)):v32;
        if(r64(value,shift)!=expect64 || r32(v32,shift)!=expect32){fprintf(stderr,"rotate mismatch %llu,%u\n",(unsigned long long)value,shift);return 4;}
        ++n;
    }
    printf("PASS fshr rotate: %u LLVM-JIT comparisons, 32/64-bit and zero/oversize counts\n",n*2);
    return 0;
}
'''
Path(sys.argv[1]).write_text(code)
