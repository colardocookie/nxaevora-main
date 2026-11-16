/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Discord presence, the host half (DISCORD-DESIGN.md, D1): Discord's local IPC pipe, our own code, on a thread of its
   own that wakes once a second. It reads the state the game half writes and never touches game memory. */
#include "nx/runtime/discord_presence.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

SnxDrpState g_drp_state = {0, {DRP_TITLE}};
/* What fs.c notes while g_drp_packs is set (1 the presence, 2 its probe): the region, the battle kind files since
   the battle start g_drp_battle counts, and a noble fight (SnxDrpFiles) */
SnxDrpFiles g_drp_files = {-1, 0, 0, 0};
int g_drp_packs;
uint32_t g_drp_battle;

#ifdef _WIN32
static HANDLE drp_handle = INVALID_HANDLE_VALUE;
static int drp_open(void* user, int index) {
    (void)user;
    char name[32];
    snprintf(name, sizeof name, "\\\\.\\pipe\\discord-ipc-%d", index);
    drp_handle = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    return drp_handle != INVALID_HANDLE_VALUE;
}
static int drp_write(void* user, const void* data, size_t size) {
    (void)user;
    DWORD done = 0;
    return WriteFile(drp_handle, data, (DWORD)size, &done, NULL) && done == size;
}
static int drp_read(void* user, void* data, size_t size) {
    (void)user;
    for (size_t got = 0; got < size;) {
        DWORD done = 0;
        if (!ReadFile(drp_handle, (char*)data + got, (DWORD)(size - got), &done, NULL) || !done) return 0;
        got += done;
    }
    return 1;
}
static void drp_close(void* user) {
    (void)user;
    CloseHandle(drp_handle);
    drp_handle = INVALID_HANDLE_VALUE;
}
static unsigned long drp_pid(void) { return GetCurrentProcessId(); }
#else
/* A write to a socket Discord closed must fail, not raise SIGPIPE, which would end the process: MSG_NOSIGNAL on each
   send where the system has it (Linux, Android), else SO_NOSIGPIPE on the socket (macOS, iOS) */
#ifdef MSG_NOSIGNAL
#define DRP_SEND_FLAGS MSG_NOSIGNAL
#else
#define DRP_SEND_FLAGS 0
#endif
static int drp_fd = -1;
static int drp_open(void* user, int index) {
    (void)user;
    const char* dir = getenv("XDG_RUNTIME_DIR");
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    snprintf(a.sun_path, sizeof a.sun_path, "%s/discord-ipc-%d", dir && dir[0] ? dir : "/tmp", index);
    drp_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (drp_fd < 0) return 0;
#if !defined(MSG_NOSIGNAL) && defined(SO_NOSIGPIPE)
    const int one = 1;
    setsockopt(drp_fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#elif !defined(MSG_NOSIGNAL)
#error "no way to keep a closed Discord socket from raising SIGPIPE on this system"
#endif
    if (connect(drp_fd, (const struct sockaddr*)&a, sizeof a)) {
        close(drp_fd);
        drp_fd = -1;
        return 0;
    }
    return 1;
}
static int drp_write(void* user, const void* data, size_t size) {
    (void)user;
    for (size_t done = 0; done < size;) {
        const ssize_t n = send(drp_fd, (const char*)data + done, size - done, DRP_SEND_FLAGS);
        if (n <= 0) return 0;
        done += (size_t)n;
    }
    return 1;
}
static int drp_read(void* user, void* data, size_t size) {
    (void)user;
    for (size_t got = 0; got < size;) {
        const ssize_t n = recv(drp_fd, (char*)data + got, size - got, 0);
        if (n <= 0) return 0;
        got += (size_t)n;
    }
    return 1;
}
static void drp_close(void* user) {
    (void)user;
    close(drp_fd);
    drp_fd = -1;
}
static unsigned long drp_pid(void) { return (unsigned long)getpid(); }
#endif

static const SnxDrpPipe drp_pipe = {drp_open, drp_write, drp_read, drp_close, NULL};
static SnxDrpSession drp_session;
static char drp_app[24];

/* Once a second: a consistent copy of the state, then the session's step. Discord clears the activity itself when
   the pipe closes with the process. */
static void drp_thread(void* arg) {
    (void)arg;
    SnxDrpValues v = {.region = -1, .activity = DRP_TITLE};
    unsigned said = 0, replies = 0;
    char last_evt[24] = "";
    for (;;) {
        SnxDrpValues now = v; /* the read leaves the region, the host's own, as it was */
        if (snx_drp_read(&g_drp_state, &now, 8)) v = now;
        v.region = __atomic_load_n(&g_drp_files.region, __ATOMIC_RELAXED);
        v.noble = snx_drp_noble_now(&g_drp_files, plat_ns());
        const int was = drp_session.connected;
        const unsigned sends = drp_session.sends;
        snx_drp_step(&drp_session, &v, plat_ns());
        /* each activity sent, which is only ever a change, so that a look can be read against the log */
        if (drp_session.sends != sends) LOG("arceus-discord: sent \"%s\"\n", drp_session.sent);
        /* Discord's reply to it (research 21:50): its evt when it changes, an ERROR in full, so the log shows whether
           Discord took each status */
        if (drp_session.replies != replies) {
            replies = drp_session.replies;
            char evt[24], message[160];
            long code;
            snx_drp_reply_parse(drp_session.reply, evt, sizeof evt, &code, message, sizeof message);
            if (!strcmp(evt, "ERROR"))
                WARN("arceus-discord: Discord refused it: code %ld, \"%s\"\n", code, message);
            else if (strcmp(evt, last_evt))
                LOG("arceus-discord: Discord replies with evt %s\n", evt[0] ? evt : "(none)");
            snprintf(last_evt, sizeof last_evt, "%s", evt);
        }
        if (drp_session.connected != was && said < 20) {
            said++;
            LOG("arceus-discord: %s Discord's pipe\n", drp_session.connected ? "connected to" : "lost");
        }
        plat_sleep_ns(1000000000ll);
    }
}

/* 1 when the thread was started. The id is checked by the caller and copied here, and only its last four digits
   are ever logged. */
int snx_discord_start(const char* app) {
    if (!snx_drp_app_ok(app)) return 0;
    snprintf(drp_app, sizeof drp_app, "%s", app);
    drp_session.pipe = &drp_pipe;
    drp_session.app = drp_app;
    drp_session.pid = drp_pid();
    drp_session.start = plat_unix_time();
    if (plat_thread(drp_thread, NULL, 256 * 1024)) {
        WARN("arceus-discord: the presence thread could not be started\n");
        return 0;
    }
    __atomic_fetch_or(&g_drp_packs, 1, __ATOMIC_RELAXED);
    const size_t n = strlen(drp_app);
    LOG("arceus-discord: EXPERIMENT: Discord presence on, application ...%s\n", drp_app + (n > 4 ? n - 4 : 0));
    return 1;
}
