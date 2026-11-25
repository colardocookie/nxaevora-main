/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The JSON Lines progress stream: one process-wide stream on a private copy of fd 1, fd 1 itself a copy of stderr,
   one line for each event (docs/records/ui/engine-ui-plan.md, unit 3) */
#include "common/utilities/progress.h"
#include "common/utilities/common.h"
#include "common/utilities/exit_codes.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#define progress_dup _dup
#define progress_dup2 _dup2
#define progress_close _close
#define progress_fdopen _fdopen
#define progress_fileno _fileno
#else
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#define progress_dup dup
#define progress_dup2 dup2
#define progress_close close
#define progress_fdopen fdopen
#define progress_fileno fileno
#endif

/* the version of the line format, and the room of the last error text */
#define SNX_PROGRESS_VERSION 1
#define SNX_PROGRESS_ERROR_SIZE 1024

/* The process-wide state: the stream (NULL off), its mode, the open stage, the last closed stage, the last error text,
   the private fd of the stream (-1 off) and the stream's fd for the crash writer (-1 off) */
static struct {
    FILE* stream;
    int mode;
    const char* open_stage;
    const char* last_stage;
    char last_error[SNX_PROGRESS_ERROR_SIZE];
    int private_fd;
    int stream_fd;
} g_progress = {NULL, SNX_PROGRESS_OFF, NULL, NULL, "", -1, -1};

#ifdef _WIN32
static CRITICAL_SECTION g_progress_lock;
static INIT_ONCE g_progress_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK progress_lock_init(PINIT_ONCE once, PVOID param, PVOID* context) {
    (void)once;
    (void)param;
    (void)context;
    InitializeCriticalSection(&g_progress_lock);
    return TRUE;
}
static void progress_lock(void) {
    InitOnceExecuteOnce(&g_progress_once, progress_lock_init, NULL, NULL);
    EnterCriticalSection(&g_progress_lock);
}
static void progress_unlock(void) { LeaveCriticalSection(&g_progress_lock); }
/* The runtime is a GUI program: without STARTF_USESTDHANDLES (the CRT's _spawnvp passes the fds in lpReserved2) it
   has no Windows standard handles, and snx_tool_console's AttachConsole sets the console's. A standard handle must
   therefore come from the CRT fd (_get_osfhandle), not GetStdHandle(STD_ERROR_HANDLE), which can name the console. */
static void progress_set_std(DWORD which, int fd) { SetStdHandle(which, (HANDLE)_get_osfhandle(fd)); }
#else
static pthread_mutex_t g_progress_lock = PTHREAD_MUTEX_INITIALIZER;
static void progress_lock(void) { pthread_mutex_lock(&g_progress_lock); }
static void progress_unlock(void) { pthread_mutex_unlock(&g_progress_lock); }
#endif

/* one line under construction; len bytes used, buf always NUL terminated */
typedef struct {
    char* buf;
    size_t size;
    size_t len;
} ProgressLine;

static int progress_on(void) { return g_progress.stream != NULL; }

static void line_init(ProgressLine* line, char* buf, size_t size) {
    line->buf = buf;
    line->size = size;
    line->len = 0;
    buf[0] = 0;
}

/* appends size bytes, or nothing when they do not fit; the caller keeps the end of the line free */
static void line_put(ProgressLine* line, const char* text, size_t size) {
    if (line->len + size > line->size - SNX_PROGRESS_LINE_TAIL) return;
    memcpy(line->buf + line->len, text, size);
    line->len += size;
    line->buf[line->len] = 0;
}

static void line_puts(ProgressLine* line, const char* text) { line_put(line, text, strlen(text)); }

/* appends the closing text of the crash line: the tail room is its, and one byte stays for the newline */
static void line_put_close(ProgressLine* line, const char* text, size_t size) {
    if (line->len + size > line->size - 1) return;
    memcpy(line->buf + line->len, text, size);
    line->len += size;
    line->buf[line->len] = 0;
}

static void line_puts_close(ProgressLine* line, const char* text) { line_put_close(line, text, strlen(text)); }

static void line_addf(ProgressLine* line, const char* fmt, ...) {
    va_list ap;
    int n;
    size_t room;
    if (line->len >= line->size) return;
    room = line->size - line->len;
    va_start(ap, fmt);
    n = vsnprintf(line->buf + line->len, room, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= room) return;
    line->len += (size_t)n;
}

static void line_prefix(ProgressLine* line, const char* stage, const char* event) {
    line_addf(line, "{\"v\":%d,\"stage\":\"%s\",\"event\":\"%s\"", SNX_PROGRESS_VERSION, stage, event);
}

/* the bytes of one valid UTF-8 sequence at s, or 0 when s does not start one: a bad lead, an overlong, a surrogate,
   past U+10FFFF, a bad continuation byte, or a sequence cut by the end of the text */
static size_t utf8_valid_span(const char* s) {
    unsigned char b0 = (unsigned char)s[0], b1, b2, b3;
    if (b0 < 0x80) return 1;
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        b1 = (unsigned char)s[1];
        return (b1 & 0xC0) == 0x80 ? 2 : 0;
    }
    if (b0 >= 0xE0 && b0 <= 0xEF) {
        b1 = (unsigned char)s[1];
        if ((b1 & 0xC0) != 0x80) return 0;
        b2 = (unsigned char)s[2];
        if ((b2 & 0xC0) != 0x80) return 0;
        if (b0 == 0xE0 && b1 < 0xA0) return 0; /* overlong */
        if (b0 == 0xED && b1 > 0x9F) return 0; /* a surrogate */
        return 3;
    }
    if (b0 >= 0xF0 && b0 <= 0xF4) {
        b1 = (unsigned char)s[1];
        if ((b1 & 0xC0) != 0x80) return 0;
        b2 = (unsigned char)s[2];
        if ((b2 & 0xC0) != 0x80) return 0;
        b3 = (unsigned char)s[3];
        if ((b3 & 0xC0) != 0x80) return 0;
        if (b0 == 0xF0 && b1 < 0x90) return 0; /* overlong */
        if (b0 == 0xF4 && b1 > 0x8F) return 0; /* past U+10FFFF */
        return 4;
    }
    return 0; /* C0, C1, F5 to FF, a continuation byte, or a byte cut by the end */
}

/* the message as a JSON string's body: " and \ escaped, a byte below 0x20 as \u00XX, an invalid byte as U+FFFD, cut at
   a character boundary. The rule is one U+FFFD for each byte that is not part of a valid sequence */
static void line_message(ProgressLine* line, const char* message) {
    const size_t room = line->size - SNX_PROGRESS_LINE_TAIL;
    static const char replacement[] = "\xEF\xBF\xBD";
    size_t i = 0;
    while (message[i]) {
        unsigned char c = (unsigned char)message[i];
        char escape[6];
        size_t size;
        if (c == '"') {
            escape[0] = '\\';
            escape[1] = '"';
            size = 2;
        } else if (c == '\\') {
            escape[0] = '\\';
            escape[1] = '\\';
            size = 2;
        } else if (c < 0x20) {
            static const char hex[] = "0123456789abcdef";
            escape[0] = '\\';
            escape[1] = 'u';
            escape[2] = '0';
            escape[3] = '0';
            escape[4] = hex[c >> 4];
            escape[5] = hex[c & 15];
            size = 6;
        } else {
            size = utf8_valid_span(message + i);
            if (!size) {
                if (line->len + 3 > room) return;
                memcpy(line->buf + line->len, replacement, 3);
                line->len += 3;
                line->buf[line->len] = 0;
                ++i;
                continue;
            }
            if (line->len + size > room) return; /* a whole character, never a part of one */
            memcpy(line->buf + line->len, message + i, size);
            line->len += size;
            line->buf[line->len] = 0;
            i += size;
            continue;
        }
        if (line->len + size > room) return; /* the end of the message keeps the fields' room */
        memcpy(line->buf + line->len, escape, size);
        line->len += size;
        line->buf[line->len] = 0;
        ++i;
    }
}

/* writes the line and its newline; the caller holds the lock */
static void line_emit(ProgressLine* line) {
    if (!progress_on()) return;
    line->buf[line->len++] = '\n';
    fwrite(line->buf, 1, line->len, g_progress.stream);
    fflush(g_progress.stream);
}

static int open_failed(const char* what) {
    int error = errno;
    snx_error("progress: %s (%s)", what, error ? strerror(error) : "no error");
    return SNX_EXIT_INTERNAL;
}

static void error_line(const char* stage, const char* message, int code, int game_code, int has_game_code) {
    ProgressLine line;
    char buf[SNX_PROGRESS_LINE_SIZE];
    line_init(&line, buf, sizeof buf);
    line_prefix(&line, stage, "error");
    line_puts(&line, ",\"message\":\"");
    line_message(&line, message ? message : "");
    if (has_game_code)
        line_addf(&line, "\",\"code\":%d,\"game_code\":%d}", code, game_code);
    else
        line_addf(&line, "\",\"code\":%d}", code);
    line_emit(&line);
}

static void exit_line(int code) {
    ProgressLine line;
    char buf[SNX_PROGRESS_LINE_SIZE];
    line_init(&line, buf, sizeof buf);
    line_addf(&line, "{\"v\":%d,\"event\":\"exit\",\"code\":%d}", SNX_PROGRESS_VERSION, code);
    line_emit(&line);
}

/* the last error text, or a line that names code when snx_error kept none */
static void finish_error(int code) {
    char fallback[96];
    const char* stage = g_progress.open_stage;
    if (g_progress.last_error[0]) {
        error_line(stage, g_progress.last_error, code, 0, 0);
    } else {
        snprintf(fallback, sizeof fallback, "the command failed with exit code %d", code);
        error_line(stage, fallback, code, 0, 0);
    }
    g_progress.last_stage = stage;
    g_progress.open_stage = NULL;
    g_progress.last_error[0] = 0;
}

int snx_progress_parse(const char* arg) {
    if (!arg) return -1;
    if (!strcmp(arg, "--progress=jsonl")) return SNX_PROGRESS_JSONL;
    if (!strcmp(arg, "--progress=jsonl-stage")) return SNX_PROGRESS_JSONL_STAGE;
    return -1;
}

int snx_progress_is_option(const char* arg) {
    if (!arg) return 0;
    return !strcmp(arg, "--progress") || !strncmp(arg, "--progress=", 11);
}

int snx_progress_open_stage(int argc, char** argv) {
    int i;
    if (!argv) return 0;
    for (i = 1; i < argc && strcmp(argv[i], "--"); ++i)
        if (snx_progress_parse(argv[i]) == SNX_PROGRESS_JSONL_STAGE)
            return snx_progress_open(SNX_PROGRESS_JSONL_STAGE);
    return 0;
}

int snx_progress_open(int mode) {
    int fd;
    FILE* stream;
    if (mode == SNX_PROGRESS_OFF) return 0;
    progress_lock();
    if (progress_on()) {
        progress_unlock();
        return 0;
    }
    progress_unlock();
    snx_tool_console(); /* a Windows GUI program's stdout is valid only after the console is attached */
    fflush(stdout);
    fd = progress_dup(progress_fileno(stdout));
    if (fd < 0) return open_failed("cannot duplicate stdout for the JSON Lines stream");
#ifdef _WIN32
    /* A child stage inherits this handle as its stdout (CreateProcessW, common/utilities/process.c) */
    if (!SetHandleInformation((HANDLE)_get_osfhandle(fd), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
        char why[160];
        snx_os_error(why, sizeof why);
        progress_close(fd);
        snx_error("progress: cannot make the JSON Lines stream inheritable (%s)", why);
        return SNX_EXIT_INTERNAL;
    }
#endif
#ifndef _WIN32
    /* the child stage gets this fd as its stdout through dup2, which drops the flag on the new fd 1 */
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        int error = errno;
        progress_close(fd);
        errno = error;
        return open_failed("cannot set close-on-exec on the JSON Lines stream");
    }
#endif
#ifdef _WIN32
    /* the copied fd keeps stdout's text mode, which turns each LF into CR LF; binary mode keeps the one LF */
    if (_setmode(fd, _O_BINARY) == -1) {
        int error = errno;
        progress_close(fd);
        errno = error;
        return open_failed("cannot put the JSON Lines stream in binary mode");
    }
#endif
    stream = progress_fdopen(fd, "wb");
    if (!stream) {
        int error = errno;
        progress_close(fd);
        errno = error;
        return open_failed("cannot open the JSON Lines stream on the copy of stdout");
    }
    if (progress_dup2(progress_fileno(stderr), progress_fileno(stdout)) < 0) {
        int error = errno;
        fclose(stream);
        errno = error;
        return open_failed("cannot send stdout to stderr");
    }
#ifdef _WIN32
    /* CreateProcessW takes the standard handles, not the CRT's fds (common/utilities/process.c) */
    progress_set_std(STD_OUTPUT_HANDLE, progress_fileno(stderr));
#endif
    progress_lock();
    g_progress.stream = stream;
    g_progress.mode = mode;
    g_progress.open_stage = NULL;
    g_progress.private_fd = fd;
    g_progress.stream_fd = fd;
    g_progress.last_error[0] = 0;
    progress_unlock();
    return 0;
}

int snx_progress_mode(void) {
    int mode;
    progress_lock();
    mode = progress_on() ? g_progress.mode : SNX_PROGRESS_OFF;
    progress_unlock();
    return mode;
}

void snx_progress_stage(const char* stage) {
    ProgressLine line;
    char buf[SNX_PROGRESS_LINE_SIZE];
    if (!stage) return;
    progress_lock();
    if (progress_on()) {
        g_progress.last_error[0] = 0;
        line_init(&line, buf, sizeof buf);
        line_prefix(&line, stage, "start");
        line_puts(&line, "}");
        line_emit(&line);
        g_progress.open_stage = stage;
    }
    progress_unlock();
}

void snx_progress_done(const char* stage) {
    ProgressLine line;
    char buf[SNX_PROGRESS_LINE_SIZE];
    if (!stage) return;
    progress_lock();
    if (progress_on()) {
        g_progress.last_error[0] = 0;
        if (g_progress.open_stage && !strcmp(g_progress.open_stage, stage)) {
            line_init(&line, buf, sizeof buf);
            line_prefix(&line, stage, "done");
            line_puts(&line, "}");
            line_emit(&line);
            g_progress.last_stage = g_progress.open_stage;
            g_progress.open_stage = NULL;
        }
    }
    progress_unlock();
}

void snx_progress_stage_name(const char* stage) {
    if (!stage) return;
    progress_lock();
    if (progress_on()) g_progress.last_stage = stage;
    progress_unlock();
}

void snx_progress_value(const char* stage, unsigned long long value, unsigned long long total) {
    ProgressLine line;
    char buf[SNX_PROGRESS_LINE_SIZE];
    if (!stage) return;
    progress_lock();
    if (progress_on()) {
        line_init(&line, buf, sizeof buf);
        line_prefix(&line, stage, "progress");
        line_addf(&line, ",\"value\":%llu,\"total\":%llu}", value, total);
        line_emit(&line);
    }
    progress_unlock();
}

void snx_progress_error(const char* message, int code) {
    progress_lock();
    if (progress_on() && g_progress.open_stage) {
        error_line(g_progress.open_stage, message, code, 0, 0);
        g_progress.last_stage = g_progress.open_stage;
        g_progress.open_stage = NULL;
        g_progress.last_error[0] = 0;
    }
    progress_unlock();
}

void snx_progress_game_error(const char* message, int code, int game_code) {
    progress_lock();
    if (progress_on() && g_progress.open_stage) {
        error_line(g_progress.open_stage, message, code, game_code, 1);
        g_progress.last_stage = g_progress.open_stage;
        g_progress.open_stage = NULL;
        g_progress.last_error[0] = 0;
    }
    progress_unlock();
}

void snx_progress_exit(int code) {
    progress_lock();
    if (progress_on() && g_progress.mode == SNX_PROGRESS_JSONL) exit_line(code);
    progress_unlock();
}

void snx_progress_note_error(const char* text) {
    if (!text) return;
    progress_lock();
    if (progress_on()) snprintf(g_progress.last_error, sizeof g_progress.last_error, "%s", text);
    progress_unlock();
}

/* appends the decimal text of value: no stdio formatting, for the crash writer. 0u - value is the magnitude of INT_MIN
   too, whose negation does not fit an int */
static void line_put_int(ProgressLine* line, int value) {
    char digits[12]; /* -2147483648 and its NUL */
    char* p = digits + sizeof digits;
    unsigned int magnitude = value < 0 ? 0u - (unsigned int)value : (unsigned int)value;
    *--p = 0;
    do {
        *--p = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (value < 0) *--p = '-';
    line_puts_close(line, p);
}

/* Crash mode: the crashed thread may hold g_progress_lock and the stdio locks, so this path reads without the lock
   and writes one prepared line with a raw write; the race with a normal writer is accepted for a process that ends.
   fatal_code calls logf_ before this writer, so this path is not fully async-signal-safe */
void snx_progress_crash_error(const char* message, int code) {
    ProgressLine line;
    char buf[SNX_PROGRESS_LINE_SIZE];
    FILE* stream = g_progress.stream;
    const char* stage = g_progress.open_stage;
    int fd = g_progress.stream_fd;
    size_t done = 0;
    if (!stream || !stage || fd < 0) return;
    line_init(&line, buf, sizeof buf);
    line_prefix(&line, stage, "error");
    line_puts(&line, ",\"message\":\"");
    line_message(&line, message ? message : "");
    line_puts_close(&line, "\",\"code\":");
    line_put_int(&line, code);
    line_puts_close(&line, "}");
    buf[line.len++] = '\n';
    while (done < line.len) {
        long written;
#ifdef _WIN32
        written = _write(fd, buf + done, (unsigned)(line.len - done));
#else
        written = (long)write(fd, buf + done, line.len - done);
#endif
        if (written > 0) {
            done += (size_t)written;
            continue;
        }
        if (written == 0) break;
#ifndef _WIN32
        if (errno == EINTR) continue;
#endif
        break;
    }
}

void snx_progress_finish(int code) {
    progress_lock();
    if (progress_on()) {
        if (code != 0) {
            if (g_progress.open_stage)
                finish_error(code);
            else if (g_progress.last_stage && g_progress.last_error[0]) {
                error_line(g_progress.last_stage, g_progress.last_error, code, 0, 0);
                g_progress.last_error[0] = 0;
            }
        }
        if (g_progress.mode == SNX_PROGRESS_JSONL) exit_line(code);
    }
    progress_unlock();
}

int snx_progress_child_redirect(void) {
    int error = 0;
    progress_lock();
    if (progress_on() && g_progress.private_fd >= 0) {
        fflush(stdout);
        if (progress_dup2(g_progress.private_fd, progress_fileno(stdout)) < 0) {
            error = errno ? errno : 1;
        } else {
#ifdef _WIN32
            progress_set_std(STD_OUTPUT_HANDLE, g_progress.private_fd);
            progress_set_std(STD_ERROR_HANDLE, progress_fileno(stderr));
#endif
        }
    }
    progress_unlock();
    return error;
}

int snx_progress_child_restore(int child_result) {
    int error = 0;
    progress_lock();
    /* a child that ended by itself with a nonzero code wrote its own error line: a second one would be wrong */
    if (child_result == SNX_CHILD_EXITED_ERROR) g_progress.last_error[0] = 0;
    if (progress_on() && g_progress.private_fd >= 0) {
        fflush(stdout);
        if (progress_dup2(progress_fileno(stderr), progress_fileno(stdout)) < 0) {
            error = errno ? errno : 1;
        } else {
#ifdef _WIN32
            progress_set_std(STD_OUTPUT_HANDLE, progress_fileno(stderr));
#endif
        }
    }
    progress_unlock();
    return error;
}

void snx_progress_test_attach(FILE* out, int mode) {
    progress_lock();
    g_progress.stream = out && mode != SNX_PROGRESS_OFF ? out : NULL;
    g_progress.mode = out ? mode : SNX_PROGRESS_OFF;
    g_progress.open_stage = NULL;
    g_progress.last_stage = NULL;
    g_progress.private_fd = -1;
    g_progress.stream_fd = g_progress.stream ? progress_fileno(out) : -1;
    g_progress.last_error[0] = 0;
    progress_unlock();
}
