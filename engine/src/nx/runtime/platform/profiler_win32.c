/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The profile option on Windows: samples the program counter of the runtime's threads every millisecond, and every
   10 seconds writes each thread's CPU time, its share in game code, runtime code (by function, from .pdata), host
   code (by module) and system waits, and its top functions, to profile.txt in the state folder beside the log. Each
   window is kept as profile-<seconds>.txt too */
#include "common/utilities/common.h"
#include "nx/loader/module_abi.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/main.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include "nx/runtime/platform/profile_calc.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROF_THREADS 256
#define PROF_PERIOD_MS 10000
#define PROF_LABEL 48
#define PROF_WAITS 32

extern char __ImageBase; /* the linker's: the executable's load address */

/* A slot is free when NULL. Threads take one with a compare-exchange; the sampler frees it once the thread ended.
   g_tids and g_gens are written after the take: a sample in between counts for the slot's previous thread */
static HANDLE g_threads[PROF_THREADS];
static DWORD g_tids[PROF_THREADS];
static uint16_t g_gens[PROF_THREADS];
static int g_threads_used; /* slots ever taken: only grows */

/* Names that threads give themselves (plat_prof_label_self), by Windows thread id. A thread may name itself before
   the sampler starts */
static struct {
    DWORD tid;
    int ready;
    char name[PROF_LABEL];
} g_labels[PROF_THREADS];

/* the sampler's own (no other thread touches these). A thread that ends is copied to g_ended, so its window is still
   written after another thread takes its slot */
#define PROF_ENDED 64
typedef struct {
    uint16_t gen, slot;
    uint8_t live;
    DWORD tid;
    u64 kinds[PROF_KINDS];
    u64 cpu_user, cpu_kernel;   /* the window's CPU time in 100 ns units (GetThreadTimes) */
    u64 seen_user, seen_kernel; /* the thread's totals at the last reading */
} ProfStat;
static ProfTable g_table;
static ProfStat g_stats[PROF_THREADS], g_ended[PROF_ENDED];
static int g_nended;
static u32 g_ended_lost; /* threads that ended past PROF_ENDED in the window */
static u64 g_ticks;
typedef struct {
    u64 host;
    u64 guest;
} ProfFn;
static ProfFn* g_fns; /* the game module's functions by host address */
static u32 g_nfns;
static ProfRange g_waits[PROF_WAITS];
static u32 g_nwaits;
/* the runtime's own functions, from its exception directory, and its image size */
static const void* g_pdata;
static u32 g_npdata, g_pdata_stride;
static u64 g_image_size = 0x10000000u;
/* the host modules seen at write time */
#define PROF_MODULES 64
typedef struct {
    u64 base, size;
    char name[64];
} ProfModule;
static ProfModule g_modules[PROF_MODULES];
static u32 g_nmodules;

void plat_prof_add_thread(HANDLE h) {
    HANDLE own;
    int i, used;
    if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &own,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                         FALSE, 0))
        return;
    for (i = 0; i < PROF_THREADS; i++) {
        HANDLE none = NULL;
        if (__atomic_compare_exchange_n(&g_threads[i], &none, own, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            __atomic_store_n(&g_tids[i], GetThreadId(own), __ATOMIC_RELAXED);
            __atomic_add_fetch(&g_gens[i], 1, __ATOMIC_RELEASE);
            used = __atomic_load_n(&g_threads_used, __ATOMIC_ACQUIRE);
            while (used < i + 1 &&
                   !__atomic_compare_exchange_n(&g_threads_used, &used, i + 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            }
            return;
        }
    }
    CloseHandle(own); /* all taken: this one goes unsampled */
}

void plat_prof_label_self(const char* name) {
    const DWORD tid = GetCurrentThreadId();
    if (!tid) return;
    for (int i = 0; i < PROF_THREADS; i++) {
        DWORD seen = __atomic_load_n(&g_labels[i].tid, __ATOMIC_ACQUIRE);
        if (!seen && __atomic_compare_exchange_n(&g_labels[i].tid, &seen, tid, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            seen = tid;
        if (seen != tid) continue;
        __atomic_store_n(&g_labels[i].ready, 0, __ATOMIC_RELEASE);
        snprintf(g_labels[i].name, sizeof g_labels[i].name, "%s", name);
        __atomic_store_n(&g_labels[i].ready, 1, __ATOMIC_RELEASE);
        return;
    }
}

static int fn_cmp(const void* a, const void* b) {
    u64 x = ((const ProfFn*)a)->host, y = ((const ProfFn*)b)->host;
    return x < y ? -1 : x > y;
}
static void fns_index(void) {
    u32 i;
    if (!g_mod || !g_mod->nentries || !g_mod->entry_fn) return;
    g_fns = malloc((size_t)g_mod->nentries * sizeof *g_fns);
    if (!g_fns) return;
    for (i = 0; i < g_mod->nentries; i++) {
        g_fns[i].host = (u64)(size_t)g_mod->entry_fn[i];
        g_fns[i].guest = g_mod->entry_addr[i];
    }
    qsort(g_fns, g_mod->nentries, sizeof *g_fns, fn_cmp);
    g_nfns = g_mod->nentries;
}
/* the system's wait entries: a thread blocked in the kernel stops inside one of these system call stubs */
static void waits_index(void) {
    static const struct { const char* module; const char* name; } entries[] = {
        {"ntdll.dll", "NtWaitForSingleObject"}, {"ntdll.dll", "NtWaitForMultipleObjects"},
        {"ntdll.dll", "NtWaitForAlertByThreadId"}, {"ntdll.dll", "NtDelayExecution"},
        {"ntdll.dll", "NtRemoveIoCompletion"}, {"ntdll.dll", "NtRemoveIoCompletionEx"},
        {"ntdll.dll", "NtSignalAndWaitForSingleObject"}, {"ntdll.dll", "NtWaitForKeyedEvent"},
        {"ntdll.dll", "NtWaitForWorkViaWorkerFactory"}, {"win32u.dll", "NtUserMsgWaitForMultipleObjectsEx"},
        {"win32u.dll", "NtUserWaitMessage"}, {"win32u.dll", "NtUserGetMessage"},
        {"win32u.dll", "NtGdiDdDDIWaitForSynchronizationObjectFromCpu"},
        {"win32u.dll", "NtGdiDdDDIWaitForVerticalBlankEvent"}, {"win32u.dll", "NtGdiDdDDIWaitForVerticalBlankEvent2"},
    };
#if defined(_M_ARM64) || defined(__aarch64__)
    const u64 stub = 16;
#else
    const u64 stub = 32;
#endif
    for (u32 i = 0; i < sizeof entries / sizeof entries[0] && g_nwaits < PROF_WAITS; i++) {
        HMODULE m = GetModuleHandleA(entries[i].module);
        FARPROC f = m ? GetProcAddress(m, entries[i].name) : NULL;
        if (!f) continue;
        g_waits[g_nwaits].lo = (u64)(size_t)f;
        g_waits[g_nwaits].hi = (u64)(size_t)f + stub;
        g_nwaits++;
    }
}
/* the runtime's function table (.pdata). x64 only: an ARM64 entry packs the length, so it keeps the 16-byte keys */
static void pdata_index(void) {
    const unsigned char* base = (const unsigned char*)&__ImageBase;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    g_image_size = nt->OptionalHeader.SizeOfImage;
#if defined(_M_X64) || defined(__x86_64__)
    const IMAGE_DATA_DIRECTORY* d = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!d->VirtualAddress || d->Size < sizeof(RUNTIME_FUNCTION)) return;
    g_pdata = base + d->VirtualAddress;
    g_pdata_stride = sizeof(RUNTIME_FUNCTION);
    g_npdata = d->Size / sizeof(RUNTIME_FUNCTION);
#endif
}
/* the game function pc is in: the closest start at or below it, within 1 MiB */
static const ProfFn* fn_at(u64 pc) {
    u32 lo = 0, hi = g_nfns;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (g_fns[mid].host <= pc)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo && pc - g_fns[lo - 1].host < (1u << 20) ? &g_fns[lo - 1] : NULL;
}

static void record(int slot, u64 pc) {
    const u64 image = (u64)(size_t)&__ImageBase;
    const ProfFn* f = fn_at(pc);
    const uint16_t gen = __atomic_load_n(&g_gens[slot], __ATOMIC_ACQUIRE);
    if (!g_stats[slot].live || g_stats[slot].gen != gen) {
        memset(&g_stats[slot], 0, sizeof g_stats[slot]);
        g_stats[slot].gen = gen;
        g_stats[slot].slot = (uint16_t)slot;
        g_stats[slot].live = 1;
        g_stats[slot].tid = __atomic_load_n(&g_tids[slot], __ATOMIC_RELAXED);
    }
    const int kind = prof_kind(pc, f != NULL, (ProfRange){image, image + g_image_size}, g_waits, g_nwaits);
    uint32_t begin;
    u64 key = f ? f->host : pc & ~(u64)15;
    g_stats[slot].kinds[kind]++;
    if (kind == PROF_WAIT) return;
    if (kind == PROF_RUNTIME && prof_function_at(g_pdata, g_npdata, g_pdata_stride, (uint32_t)(pc - image), &begin))
        key = image + begin; /* the function, not its 16 bytes */
    prof_count(&g_table, (uint16_t)slot, gen, key);
}

/* The thread's name: its own label, the system's description, or its Windows id. No thread is stopped here */
static void thread_name(const ProfStat* s, char* out, size_t size) {
    typedef HRESULT(WINAPI * Describe)(HANDLE, PWSTR*);
    static Describe describe;
    static int looked;
    const DWORD tid = s->tid;
    for (int i = 0; i < PROF_THREADS; i++) {
        if (__atomic_load_n(&g_labels[i].tid, __ATOMIC_ACQUIRE) != tid) continue;
        if (!__atomic_load_n(&g_labels[i].ready, __ATOMIC_ACQUIRE)) break;
        snprintf(out, size, "%s", g_labels[i].name);
        return;
    }
    if (!looked) {
        HMODULE k = GetModuleHandleA("kernel32.dll");
        describe = k ? (Describe)(void (*)(void))GetProcAddress(k, "GetThreadDescription") : NULL;
        looked = 1;
    }
    HANDLE h = s->live ? __atomic_load_n(&g_threads[s->slot], __ATOMIC_ACQUIRE) : NULL;
    PWSTR wide = NULL;
    if (h && describe && SUCCEEDED(describe(h, &wide)) && wide && wide[0]) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)size, NULL, NULL);
        LocalFree(wide);
        if (n > 1) return;
    } else if (wide)
        LocalFree(wide);
    snprintf(out, size, "host thread %lu", (unsigned long)tid);
}

/* The thread's CPU totals in 100 ns units, 0 when the system has none */
static void thread_times(HANDLE h, u64* user, u64* kernel) {
    FILETIME created, exited, k, u;
    *user = *kernel = 0;
    if (!h || !GetThreadTimes(h, &created, &exited, &k, &u)) return;
    *user = (u64)u.dwHighDateTime << 32 | u.dwLowDateTime;
    *kernel = (u64)k.dwHighDateTime << 32 | k.dwLowDateTime;
}
/* the window's CPU time: the totals now against the last reading */
static void cpu_window(ProfStat* s, HANDLE h) {
    u64 user, kernel;
    thread_times(h, &user, &kernel);
    s->cpu_user = user > s->seen_user ? user - s->seen_user : 0;
    s->cpu_kernel = kernel > s->seen_kernel ? kernel - s->seen_kernel : 0;
    s->seen_user = user;
    s->seen_kernel = kernel;
}
/* The host module that holds addr, kept for one write. Write time only: GetModuleHandleEx takes the loader's lock,
   and no thread is stopped then. The handle holds a reference while the headers and the path are read, so a module
   that another thread unloads stays mapped until FreeLibrary */
static const ProfModule* module_at(u64 addr) {
    HMODULE m;
    char path[MAX_PATH];
    for (u32 i = 0; i < g_nmodules; i++)
        if (addr - g_modules[i].base < g_modules[i].size) return &g_modules[i];
    if (g_nmodules == PROF_MODULES ||
        !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)(size_t)addr, &m))
        return NULL;
    const unsigned char* base = (const unsigned char*)m;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    ProfModule* mod = &g_modules[g_nmodules];
    mod->base = (u64)(size_t)base;
    mod->size = dos->e_magic == IMAGE_DOS_SIGNATURE && nt->Signature == IMAGE_NT_SIGNATURE
                    ? nt->OptionalHeader.SizeOfImage
                    : 1;
    const DWORD n = GetModuleFileNameA(m, path, sizeof path);
    const char* leaf = n ? path + n : path;
    while (leaf > path && leaf[-1] != '\\' && leaf[-1] != '/') leaf--;
    snprintf(mod->name, sizeof mod->name, "%s", n ? leaf : "?");
    FreeLibrary(m);
    if (addr - mod->base >= mod->size) return NULL;
    g_nmodules++;
    return mod;
}
/* "host by module": the thread's host samples summed by module, the largest first */
typedef struct {
    const ProfModule* mod; /* NULL: no module holds the address */
    u64 count;
} ProfSum;
static void host_modules(FILE* f, const ProfStat* s, double ticks) {
    ProfSum sums[16];
    u32 n = 0;
    u64 other = 0;
    const u64 image = (u64)(size_t)&__ImageBase;
    for (u32 i = 0; i < PROF_BUCKETS; i++) {
        const ProfBucket* b = &g_table.buckets[i];
        if (!b->key || b->slot != s->slot || b->gen != s->gen) continue;
        const ProfFn* fn = fn_at(b->key);
        if ((fn && fn->host == b->key) || b->key - image < g_image_size) continue; /* game or runtime */
        const ProfModule* mod = module_at(b->key);
        u32 k = 0;
        while (k < n && sums[k].mod != mod) k++;
        if (k == n && n < sizeof sums / sizeof sums[0]) sums[n++] = (ProfSum){mod, 0};
        if (k < n)
            sums[k].count += b->count;
        else
            other += b->count;
    }
    if (!n) return;
    for (u32 i = 1; i < n; i++) /* a few entries: insertion sort */
        for (u32 j = i; j && sums[j - 1].count < sums[j].count; j--) {
            const ProfSum t = sums[j];
            sums[j] = sums[j - 1];
            sums[j - 1] = t;
        }
    fprintf(f, "  host by module:");
    for (u32 i = 0; i < n; i++)
        fprintf(f, "%s %s %.1f%%", i ? "," : "", sums[i].mod ? sums[i].mod->name : "unknown",
                100.0 * (double)sums[i].count / ticks);
    if (other) fprintf(f, ", others %.1f%%", 100.0 * (double)other / ticks);
    fprintf(f, "\n");
}

static u64 busy(const ProfStat* s) { return s->kinds[PROF_GAME] + s->kinds[PROF_RUNTIME] + s->kinds[PROF_HOST]; }
static int busy_cmp(const void* a, const void* b) {
    u64 x = busy(*(const ProfStat* const*)a), y = busy(*(const ProfStat* const*)b);
    return x > y ? -1 : x < y;
}
static void write_window(FILE* f, const ProfStat* const* order, int n, u64 from_ns, u64 to_ns) {
    static ProfBucket top[PROF_TOP];
    const u64 image = (u64)(size_t)&__ImageBase;
    const double ticks = g_ticks ? (double)g_ticks : 1.0;
    const double window_100ns = to_ns > from_ns ? (double)(to_ns - from_ns) / 100.0 : 1.0;
    fprintf(f, "ScarletNX profile: the window from %.1f s to %.1f s after the profiler started, %llu ticks of 1 ms, "
               "%d threads, %u ended threads left out, %llu samples not placed. Shares are of the ticks\n",
            (double)from_ns / 1e9, (double)to_ns / 1e9, (unsigned long long)g_ticks, n, g_ended_lost,
            (unsigned long long)g_table.unplaced);
    for (int k = 0; k < n; k++) {
        const ProfStat* s = order[k];
        char name[PROF_LABEL + 32];
        thread_name(s, name, sizeof name);
        const u64* c = s->kinds;
        fprintf(f, "\nthread %s: cpu %.1f%% (kernel %.1f%%), busy %.1f%% (game %.1f%%, runtime %.1f%%, host %.1f%%), "
                   "wait %.1f%%%s\n",
                name, 100.0 * (double)(s->cpu_user + s->cpu_kernel) / window_100ns,
                100.0 * (double)s->cpu_kernel / window_100ns, 100.0 * (double)busy(s) / ticks,
                100.0 * (double)c[PROF_GAME] / ticks, 100.0 * (double)c[PROF_RUNTIME] / ticks,
                100.0 * (double)c[PROF_HOST] / ticks, 100.0 * (double)c[PROF_WAIT] / ticks,
                s->live ? "" : ", ended");
        host_modules(f, s, ticks);
        const u32 found = prof_top(&g_table, s->slot, s->gen, top, PROF_TOP);
        for (u32 i = 0; i < found; i++) {
            const ProfFn* fn = fn_at(top[i].key);
            const double pct = 100.0 * top[i].count / ticks;
            if (fn && fn->host == top[i].key)
                fprintf(f, "  %6.2f%% %8u  game function %llx\n", pct, top[i].count, (unsigned long long)fn->guest);
            else if (top[i].key >= image && top[i].key - image < g_image_size)
                fprintf(f, "  %6.2f%% %8u  runtime +%llx\n", pct, top[i].count,
                        (unsigned long long)(top[i].key - image));
            else {
                const ProfModule* mod = module_at(top[i].key);
                if (mod)
                    fprintf(f, "  %6.2f%% %8u  host %s+%llx\n", pct, top[i].count, mod->name,
                            (unsigned long long)(top[i].key - mod->base));
                else
                    fprintf(f, "  %6.2f%% %8u  host %llx\n", pct, top[i].count, (unsigned long long)top[i].key);
            }
        }
    }
}
/* profile.txt holds the last window, profile-<seconds>.txt keeps each one. Then the next window starts empty, at the
   time returned, which is when the CPU totals were read, so a window's CPU time and its length cover the same span */
static u64 write_profile(u64 started, u64 window_start) {
    static const ProfStat* order[PROF_THREADS + PROF_ENDED];
    const u64 now = plat_ns();
    char path[1200];
    int n = 0;
    for (int i = 0; i < PROF_THREADS; i++) {
        if (!g_stats[i].live) continue;
        cpu_window(&g_stats[i], __atomic_load_n(&g_threads[i], __ATOMIC_ACQUIRE));
        if (busy(&g_stats[i]) || g_stats[i].kinds[PROF_WAIT]) order[n++] = &g_stats[i];
    }
    for (int i = 0; i < g_nended; i++) order[n++] = &g_ended[i];
    qsort(order, (size_t)n, sizeof *order, busy_cmp);
    for (int pass = 0; pass < 2; pass++) {
        if (pass)
            snprintf(path, sizeof path, "%sprofile-%05llu.txt", g_statedir,
                     (unsigned long long)((now - started) / 1000000000u));
        else
            snprintf(path, sizeof path, "%sprofile.txt", g_statedir);
        FILE* f = fopen(path, "w");
        if (!f) continue;
        write_window(f, order, n, window_start - started, now - started);
        fclose(f);
    }
    prof_clear(&g_table);
    for (int i = 0; i < PROF_THREADS; i++) memset(g_stats[i].kinds, 0, sizeof g_stats[i].kinds);
    g_nended = 0;
    g_ended_lost = 0;
    g_ticks = 0;
    g_nmodules = 0; /* a module at a base another one had is looked up again */
    return now;
}

static DWORD WINAPI sampler(LPVOID unused) {
    u64 started = plat_ns(), window_start = started, next_write = started + (u64)PROF_PERIOD_MS * 1000000u;
    (void)unused;
    fns_index();
    waits_index();
    pdata_index();
    for (;;) {
        int i, used = __atomic_load_n(&g_threads_used, __ATOMIC_ACQUIRE);
        Sleep(1);
        g_ticks++;
        for (i = 0; i < used; i++) {
            HANDLE h = __atomic_load_n(&g_threads[i], __ATOMIC_ACQUIRE);
            CONTEXT c;
            if (!h) continue;
            if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) { /* the thread ended: its slot is free again */
                if (g_stats[i].live) { /* its window is still written, from g_ended, marked ended */
                    cpu_window(&g_stats[i], h);
                    g_stats[i].live = 0;
                    if (g_nended < PROF_ENDED)
                        g_ended[g_nended++] = g_stats[i];
                    else
                        g_ended_lost++;
                }
                __atomic_store_n(&g_threads[i], NULL, __ATOMIC_RELEASE);
                CloseHandle(h);
                continue;
            }
            if (SuspendThread(h) == (DWORD)-1) continue;
            /* nothing here may allocate or lock: the stopped thread could hold the heap's lock */
            memset(&c, 0, sizeof c);
            c.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(h, &c)) {
#if defined(_M_ARM64) || defined(__aarch64__)
                record(i, (u64)c.Pc);
#else
                record(i, (u64)c.Rip);
#endif
            }
            ResumeThread(h);
        }
        if (plat_ns() >= next_write) {
            window_start = write_profile(started, window_start);
            next_write += (u64)PROF_PERIOD_MS * 1000000u;
        }
    }
}

void plat_prof_start(void) {
    HANDLE h;
    plat_prof_add_thread(GetCurrentThread()); /* the main thread (the window and the frame loop) */
    plat_prof_label_self("main");
    h = CreateThread(NULL, 0, sampler, NULL, 0, NULL);
    if (!h) {
        char why[256];
        WARN("profile: the sampling thread could not start: %s\n", snx_os_error(why, sizeof why));
        return;
    }
    SetThreadPriority(h, THREAD_PRIORITY_HIGHEST);
    CloseHandle(h);
}
