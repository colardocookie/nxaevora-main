#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef void* P;
extern P LLVMContextCreate(void);
extern P LLVMModuleCreateWithNameInContext(const char*,P);
extern P LLVMVoidTypeInContext(P);
extern P LLVMFunctionType(P,P*,unsigned,int);
extern P LLVMAddFunction(P,const char*,P);
extern P LLVMAppendBasicBlockInContext(P,P,const char*);
extern P LLVMCreateBuilderInContext(P);
extern void LLVMPositionBuilderAtEnd(P,P);
extern P LLVMBuildRetVoid(P);
extern P LLVMWriteBitcodeToMemoryBuffer(P);
extern const char* LLVMGetBufferStart(P);
extern size_t LLVMGetBufferSize(P);
extern P LLVMCreateMemoryBufferWithMemoryRange(const char*,size_t,const char*,int);
extern P LLVMCreateMemoryBufferWithMemoryRangeCopy(const char*,size_t,const char*);
extern int LLVMParseBitcodeInContext2(P,P,P*);
extern int LLVMVerifyModule(P,int,char**);
extern char* LLVMPrintModuleToString(P);
extern void LLVMDisposeMessage(char*);
extern void LLVMDisposeMemoryBuffer(P);
extern void LLVMDisposeBuilder(P);
extern void LLVMDisposeModule(P);
extern void LLVMContextDispose(P);
int main(void) {
    P ctx=LLVMContextCreate(),mod=LLVMModuleCreateWithNameInContext("embedded",ctx);
    P f=LLVMAddFunction(mod,"fn",LLVMFunctionType(LLVMVoidTypeInContext(ctx),0,0,0));
    P b=LLVMCreateBuilderInContext(ctx);
    LLVMPositionBuilderAtEnd(b,LLVMAppendBasicBlockInContext(ctx,f,"entry"));LLVMBuildRetVoid(b);
    P bits=LLVMWriteBitcodeToMemoryBuffer(mod);
    const char *bytes=LLVMGetBufferStart(bits);size_t len=LLVMGetBufferSize(bits);
    P parsed[2];
    P views[2]={LLVMCreateMemoryBufferWithMemoryRangeCopy(bytes,len,"old"),
                LLVMCreateMemoryBufferWithMemoryRange(bytes,len,"new",0)};
    assert(!LLVMParseBitcodeInContext2(ctx,views[0],&parsed[0]));
    assert(!LLVMParseBitcodeInContext2(ctx,views[1],&parsed[1]));
    LLVMDisposeMemoryBuffer(views[0]);LLVMDisposeMemoryBuffer(views[1]);
    char *err=0;
    for(int i=0;i<2;++i)assert(!LLVMVerifyModule(parsed[i],2,&err));
    char *a=LLVMPrintModuleToString(parsed[0]),*c=LLVMPrintModuleToString(parsed[1]);
    /* Only the buffer identifier differs; the contents must parse identically. */
    assert(strstr(a,"define void @fn")&&strstr(c,"define void @fn"));
    printf("PASS copied and zero-copy embedded bitcode buffers parse, survive view disposal, and verify\n");
    LLVMDisposeMessage(a);LLVMDisposeMessage(c);
    for(int i=0;i<2;++i)LLVMDisposeModule(parsed[i]);
    LLVMDisposeMemoryBuffer(bits);LLVMDisposeBuilder(b);LLVMDisposeModule(mod);LLVMContextDispose(ctx);
    return 0;
}
