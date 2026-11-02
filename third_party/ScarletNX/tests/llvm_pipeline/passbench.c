/* LLVM 19's C API, without depending on a development-header installation. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void* Ref;
extern Ref LLVMContextCreate(void);
extern void LLVMContextDispose(Ref);
extern int LLVMCreateMemoryBufferWithContentsOfFile(const char*, Ref*, char**);
extern int LLVMParseIRInContext(Ref, Ref, Ref*, char**);
extern void LLVMDisposeModule(Ref);
extern Ref LLVMCreatePassBuilderOptions(void);
extern void LLVMPassBuilderOptionsSetInlinerThreshold(Ref, int);
extern void LLVMPassBuilderOptionsSetSLPVectorization(Ref, int);
extern Ref LLVMRunPasses(Ref, const char*, Ref, Ref);
extern void LLVMDisposePassBuilderOptions(Ref);
extern char* LLVMGetErrorMessage(Ref);
extern char* LLVMGetDefaultTargetTriple(void);
extern int LLVMGetTargetFromTriple(const char*, Ref*, char**);
extern void LLVMDisposeMessage(char*);
extern Ref LLVMCreateTargetMachine(Ref, const char*, const char*, const char*, int, int, int);
extern void LLVMDisposeTargetMachine(Ref);
extern void LLVMInitializeX86Target(void);
extern void LLVMInitializeX86TargetInfo(void);
extern void LLVMInitializeX86TargetMC(void);
extern void LLVMInitializeX86AsmPrinter(void);
extern Ref LLVMCreateTargetDataLayout(Ref);
extern char* LLVMCopyStringRepOfTargetData(Ref);
extern void LLVMSetTarget(Ref, const char*);
extern void LLVMSetDataLayout(Ref, const char*);
extern void LLVMDisposeTargetData(Ref);
extern char* LLVMPrintModuleToString(Ref);
extern int LLVMTargetMachineEmitToMemoryBuffer(Ref, Ref, int, char**, Ref*);
extern size_t LLVMGetBufferSize(Ref);
extern const char* LLVMGetBufferStart(Ref);
extern void LLVMDisposeMemoryBuffer(Ref);

static double milliseconds(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec * 1000.0 + time.tv_nsec / 1000000.0;
}

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();

    char* triple = LLVMGetDefaultTargetTriple();
    Ref target = NULL;
    char* error = NULL;
    if (LLVMGetTargetFromTriple(triple, &target, &error)) {
        fprintf(stderr, "target: %s\n", error);
        return 2;
    }
    Ref machine = LLVMCreateTargetMachine(target, triple, "x86-64", "", 2, 2, 0);
    for (int iteration = 0; iteration < 3; ++iteration) {
        Ref context = LLVMContextCreate(), buffer = NULL, module = NULL;
        if (LLVMCreateMemoryBufferWithContentsOfFile(argv[1], &buffer, &error) ||
            LLVMParseIRInContext(context, buffer, &module, &error)) {
            fprintf(stderr, "IR: %s\n", error ? error : "unavailable");
            return 2;
        }
        /* LLVMParseIRInContext takes ownership of the input buffer. */
        LLVMSetTarget(module, triple);
        Ref layout = LLVMCreateTargetDataLayout(machine);
        char* string = LLVMCopyStringRepOfTargetData(layout);
        LLVMSetDataLayout(module, string);
        LLVMDisposeMessage(string);
        LLVMDisposeTargetData(layout);

        Ref options = LLVMCreatePassBuilderOptions();
        int budget = atoi(argv[3]);
        if (budget >= 0) LLVMPassBuilderOptionsSetInlinerThreshold(options, budget);
        const char* slp = getenv("SNX_LLVM_SLP");
        if (slp && (!strcmp(slp, "0") || !strcmp(slp, "1")))
            LLVMPassBuilderOptionsSetSLPVectorization(options, slp[0] == '1');
        double started = milliseconds();
        Ref failed = LLVMRunPasses(module, argv[2], machine, options);
        double optimized = milliseconds();
        if (failed) {
            fprintf(stderr, "passes: %s\n", LLVMGetErrorMessage(failed));
            return 2;
        }
        string = LLVMPrintModuleToString(module);
        size_t ir_bytes = strlen(string);
        LLVMDisposeMessage(string);

        Ref object = NULL;
        if (LLVMTargetMachineEmitToMemoryBuffer(machine, module, 1, &error, &object)) {
            fprintf(stderr, "codegen: %s\n", error ? error : "unavailable");
            return 2;
        }
        double emitted = milliseconds();
        if (iteration == 2 && argc > 4) {
            FILE* file = fopen(argv[4], "wb");
            if (!file) return 2;
            fwrite(LLVMGetBufferStart(object), 1, LLVMGetBufferSize(object), file);
            fclose(file);
        }
        printf("%s threshold=%s repetition=%d passes_ms=%.3f codegen_ms=%.3f ir_bytes=%zu obj_bytes=%zu\n",
               argv[2], argv[3], iteration, optimized - started, emitted - optimized, ir_bytes,
               LLVMGetBufferSize(object));
        LLVMDisposeMemoryBuffer(object);
        LLVMDisposePassBuilderOptions(options);
        LLVMDisposeModule(module);
        LLVMContextDispose(context);
    }
    LLVMDisposeTargetMachine(machine);
    LLVMDisposeMessage(triple);
    return 0;
}
