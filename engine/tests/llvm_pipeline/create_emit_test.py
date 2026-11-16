from pathlib import Path
source=Path(__file__).parents[2]/'src/common/cpu/recompiler/llvm/ir_emit.c'
s=source.read_text()
def extract(sig):
    start=s.index(sig);a=s.index('{',start);depth=1;i=a+1
    while depth:
        if s[i]=='{':depth+=1
        elif s[i]=='}':depth-=1
        i+=1
    return s[start:i]
functions='\n'.join(extract(x) for x in ['void ir_fence(SnxIr* ir)', 'static void ir_branch_weights(SnxIr* ir', 'void ir_unlikely(SnxIr* ir','void ir_likely(SnxIr* ir'])
pre=r'''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef void *P;
typedef P LLVMValueRef;
typedef P LLVMBuilderRef;
typedef P LLVMContextRef;
typedef P LLVMMetadataRef;
typedef struct { P ctx, b, t_i32; } SnxIr;
#define LLVMFence 55
#define LLVMAtomicOrderingSequentiallyConsistent 7
extern P LLVMContextCreate(void);
extern P LLVMModuleCreateWithNameInContext(const char*,P);
extern P LLVMCreateBuilderInContext(P);
extern P LLVMVoidTypeInContext(P);
extern P LLVMInt32TypeInContext(P);
extern P LLVMInt1TypeInContext(P);
extern P LLVMFunctionType(P,P*,unsigned,int);
extern P LLVMAddFunction(P,const char*,P);
extern P LLVMAppendBasicBlockInContext(P,P,const char*);
extern void LLVMPositionBuilderAtEnd(P,P);
extern P LLVMGetInsertBlock(P);
extern P LLVMGetLastInstruction(P);
extern P LLVMGetNextInstruction(P);
extern P LLVMGetFirstInstruction(P);
extern int LLVMGetInstructionOpcode(P);
extern int LLVMGetOrdering(P);
extern P LLVMBuildFence(P,int,int,const char*);
extern P LLVMBuildAlloca(P,P,const char*);
extern P LLVMBuildLoad2(P,P,P,const char*);
extern P LLVMBuildRetVoid(P);
extern P LLVMBuildCondBr(P,P,P,P);
extern P LLVMGetParam(P,unsigned);
extern P LLVMConstInt(P,uint64_t,int);
extern P LLVMMDStringInContext2(P,const char*,size_t);
extern P LLVMValueAsMetadata(P);
extern P LLVMMDNodeInContext2(P,P*,unsigned);
extern P LLVMMetadataAsValue(P,P);
extern unsigned LLVMGetMDKindIDInContext(P,const char*,unsigned);
extern void LLVMSetMetadata(P,unsigned,P);
extern char* LLVMPrintModuleToString(P);
extern void LLVMDisposeMessage(char*);
extern int LLVMVerifyModule(P,int,char**);
extern void LLVMDisposeBuilder(P);
extern void LLVMDisposeModule(P);
extern void LLVMContextDispose(P);
static P ir_i32(SnxIr*ir,unsigned i){return LLVMConstInt(ir->t_i32,i,0);}
'''
post=r'''
int main(void) {
  SnxIr ir={0};ir.ctx=LLVMContextCreate();ir.b=LLVMCreateBuilderInContext(ir.ctx);ir.t_i32=LLVMInt32TypeInContext(ir.ctx);
  P mod=LLVMModuleCreateWithNameInContext("actual_emit_helpers",ir.ctx);
  P ft=LLVMFunctionType(LLVMVoidTypeInContext(ir.ctx),0,0,0),f=LLVMAddFunction(mod,"test_fence",ft);
  P bb=LLVMAppendBasicBlockInContext(ir.ctx,f,"entry");LLVMPositionBuilderAtEnd(ir.b,bb);
  ir_fence(&ir); ir_fence(&ir);
  P first=LLVMGetFirstInstruction(bb);
  assert(LLVMGetInstructionOpcode(first)==LLVMFence);
  assert(LLVMGetNextInstruction(first)==0);
  P ptr=LLVMBuildAlloca(ir.b,ir.t_i32,"");
  (void)LLVMBuildLoad2(ir.b,ir.t_i32,ptr,"");
  ir_fence(&ir);
  P last=LLVMGetLastInstruction(bb);
  assert(LLVMGetInstructionOpcode(last)==LLVMFence);
  assert(LLVMGetNextInstruction(first)!=last);
  LLVMBuildRetVoid(ir.b);
  P branch=LLVMAddFunction(mod,"test_hot_lookup",LLVMFunctionType(LLVMVoidTypeInContext(ir.ctx), &((P){LLVMInt1TypeInContext(ir.ctx)}),1,0));
  bb=LLVMAppendBasicBlockInContext(ir.ctx,branch,"entry");P hot=LLVMAppendBasicBlockInContext(ir.ctx,branch,"hot"),cold=LLVMAppendBasicBlockInContext(ir.ctx,branch,"cold");
  LLVMPositionBuilderAtEnd(ir.b,bb);ir_likely(&ir,LLVMBuildCondBr(ir.b,LLVMGetParam(branch,0),hot,cold));
  LLVMPositionBuilderAtEnd(ir.b,hot);LLVMBuildRetVoid(ir.b);
  LLVMPositionBuilderAtEnd(ir.b,cold);LLVMBuildRetVoid(ir.b);
  char* err=0;assert(!LLVMVerifyModule(mod,2,&err));
  char*text=LLVMPrintModuleToString(mod);
  assert(strstr(text,"!prof")&&strstr(text,"branch_weights")&&strstr(text,"2000, i32 1"));
  printf("PASS actual fence and branch metadata emitters: adjacent fence folding, nonadjacent fence kept, hot-path weights\n");
  LLVMDisposeMessage(text);LLVMDisposeModule(mod);LLVMDisposeBuilder(ir.b);LLVMContextDispose(ir.ctx);
  return 0;
}
'''
import sys
Path(sys.argv[1]).write_text(pre+'\n'+functions+'\n'+post)
