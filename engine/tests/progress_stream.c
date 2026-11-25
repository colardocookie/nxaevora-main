/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The JSON Lines progress stream (common/utilities/progress.c): the exact bytes of each event, the escapes, the cut of
   a long message, the exit line's modes and the game_code field */
#include "common/utilities/progress.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECTED_SIZE 8192

/* a temporary file the stream writes to, without the fd split of snx_progress_open */
static FILE* attach(int mode) {
    FILE* out = tmpfile();
    assert(out);
    snx_progress_test_attach(out, mode);
    return out;
}

/* the whole file's text, then its end; out is the caller's */
static void read_stream(FILE* out, char* text, size_t size) {
    long length;
    size_t got;
    fflush(out);
    assert(!fseek(out, 0, SEEK_END));
    length = ftell(out);
    assert(length >= 0 && (size_t)length < size);
    assert(!fseek(out, 0, SEEK_SET));
    got = fread(text, 1, size - 1, out);
    assert(got == (size_t)length);
    text[got] = 0;
}

static void expect(FILE* out, const char* wanted) {
    char text[EXPECTED_SIZE];
    read_stream(out, text, sizeof text);
    if (strcmp(text, wanted)) {
        fprintf(stderr, "progress_stream: expected:\n%s\ngot:\n%s\n", wanted, text);
        assert(0);
    }
}

/* an input stage's error line with message, its JSON body checked against body */
static void error_message(const char* message, const char* body) {
    FILE* out = attach(SNX_PROGRESS_JSONL);
    char wanted[EXPECTED_SIZE];
    snx_progress_stage(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_error(message, 1);
    snprintf(wanted, sizeof wanted,
             "{\"v\":1,\"stage\":\"input\",\"event\":\"start\"}\n"
             "{\"v\":1,\"stage\":\"input\",\"event\":\"error\",\"message\":\"%s\",\"code\":1}\n",
             body);
    expect(out, wanted);
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
}

int main(void) {
    FILE* out;
    /* the progress option's two forms, and the arguments that are not it */
    assert(snx_progress_is_option("--progress"));
    assert(snx_progress_is_option("--progress=jsonl"));
    assert(snx_progress_is_option("--progress=x"));
    assert(!snx_progress_is_option("--progressx"));
    assert(!snx_progress_is_option("--prog"));
    assert(!snx_progress_is_option("-progress=jsonl"));
    assert(!snx_progress_is_option(NULL));
    /* start, progress, done and error, with a quote, a backslash, a newline and byte 0x01 escaped */
    out = attach(SNX_PROGRESS_JSONL);
    snx_progress_stage(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_done(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_stage(SNX_PROGRESS_STAGE_RECOMPILE);
    snx_progress_value(SNX_PROGRESS_STAGE_RECOMPILE, 3, 10);
    snx_progress_error("bad \"x\" \\\n\x01", 4);
    snx_progress_exit(4);
    expect(out,
           "{\"v\":1,\"stage\":\"input\",\"event\":\"start\"}\n"
           "{\"v\":1,\"stage\":\"input\",\"event\":\"done\"}\n"
           "{\"v\":1,\"stage\":\"recompile\",\"event\":\"start\"}\n"
           "{\"v\":1,\"stage\":\"recompile\",\"event\":\"progress\",\"value\":3,\"total\":10}\n"
           "{\"v\":1,\"stage\":\"recompile\",\"event\":\"error\",\"message\":\"bad \\\"x\\\" "
           "\\\\\\u000a\\u0001\",\"code\":4}\n"
           "{\"v\":1,\"event\":\"exit\",\"code\":4}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* the internal stage mode writes the stages and no exit line */
    out = attach(SNX_PROGRESS_JSONL_STAGE);
    snx_progress_stage(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_done(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_exit(9);
    expect(out, "{\"v\":1,\"stage\":\"input\",\"event\":\"start\"}\n"
                "{\"v\":1,\"stage\":\"input\",\"event\":\"done\"}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* no open stage: no error line, and the exit line stays the last one */
    out = attach(SNX_PROGRESS_JSONL);
    snx_progress_error("ignored", 1);
    snx_progress_exit(0);
    expect(out, "{\"v\":1,\"event\":\"exit\",\"code\":0}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* done writes only for the open stage: no open stage, or another stage, writes nothing */
    out = attach(SNX_PROGRESS_JSONL);
    snx_progress_done(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_stage(SNX_PROGRESS_STAGE_RECOMPILE);
    snx_progress_done(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_done(SNX_PROGRESS_STAGE_RECOMPILE);
    expect(out, "{\"v\":1,\"stage\":\"recompile\",\"event\":\"start\"}\n"
                "{\"v\":1,\"stage\":\"recompile\",\"event\":\"done\"}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* the game's own code: code 8 and game_code 42 in the run stage's error line */
    out = attach(SNX_PROGRESS_JSONL_STAGE);
    snx_progress_stage(SNX_PROGRESS_STAGE_RUN);
    snx_progress_game_error("the game ended with code 42", 8, 42);
    snx_progress_exit(8);
    expect(out, "{\"v\":1,\"stage\":\"run\",\"event\":\"start\"}\n"
                "{\"v\":1,\"stage\":\"run\",\"event\":\"error\",\"message\":\"the game ended with code 42\","
                "\"code\":8,\"game_code\":42}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* a byte that is not UTF-8 becomes one U+FFFD: three bytes, three replacements */
    error_message("\xff\x93\xfa", "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
    /* the rule is one U+FFFD for each byte that is not in a valid sequence: a cut sequence gets one for each byte */
    error_message("\xe2\x82", "\xEF\xBF\xBD\xEF\xBF\xBD");
    /* a surrogate's three bytes and an overlong C0 AF are invalid: one U+FFFD for each byte */
    error_message("\xed\xa0\x80", "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
    error_message("\xc0\xaf", "\xEF\xBF\xBD\xEF\xBF\xBD");
    /* a valid 4-byte character passes as it is */
    error_message("\xf0\x9f\x98\x80", "\xf0\x9f\x98\x80");
    /* a valid 2-byte and a valid 3-byte character pass as they are */
    error_message("\xc3\xa9", "\xc3\xa9");
    error_message("\xe2\x82\xac", "\xe2\x82\xac");
    /* the overlong and past U+10FFFF forms: one U+FFFD for each byte, as the rule above says */
    error_message("\xe0\x80\x80", "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
    error_message("\xf0\x80\x80\x80", "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
    error_message("\xf4\x90\x80\x80", "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
    /* a message over the line's room is cut at a UTF-8 boundary, never in an escape */
    {
        static const char euro[] = "\xE2\x82\xAC";
        char message[9000];
        char text[EXPECTED_SIZE];
        char* line;
        size_t i, prefix, suffix, length, content;
        const char prefix_text[] =
            "{\"v\":1,\"stage\":\"input\",\"event\":\"error\",\"message\":\"";
        const char suffix_text[] = "\",\"code\":1}\n";
        out = attach(SNX_PROGRESS_JSONL);
        snx_progress_stage(SNX_PROGRESS_STAGE_INPUT);
        for (i = 0; i + 3 <= sizeof message; i += 3) memcpy(message + i, euro, 3);
        message[sizeof message - 3] = 0;
        snx_progress_error(message, 1);
        read_stream(out, text, sizeof text);
        line = strstr(text, prefix_text); /* the error line, after the stage's start line */
        assert(line);
        prefix = sizeof prefix_text - 1;
        suffix = sizeof suffix_text - 1;
        length = strlen(line);
        assert(length > prefix + suffix);
        assert(!memcmp(line, prefix_text, prefix));
        assert(!memcmp(line + length - suffix, suffix_text, suffix));
        content = length - prefix - suffix;
        assert(content > 0 && content < 9000 && content % 3 == 0);
        for (i = 0; i < content; i += 3) assert(!memcmp(line + prefix + i, euro, 3));
        assert(length + 1 <= SNX_PROGRESS_LINE_SIZE);
        snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
        fclose(out);
    }
    /* finish with an error noted after the stage closed: the closed stage's error line, then the exit line */
    out = attach(SNX_PROGRESS_JSONL);
    snx_progress_stage(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_done(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_note_error("a late failure");
    snx_progress_finish(4);
    expect(out,
           "{\"v\":1,\"stage\":\"input\",\"event\":\"start\"}\n"
           "{\"v\":1,\"stage\":\"input\",\"event\":\"done\"}\n"
           "{\"v\":1,\"stage\":\"input\",\"event\":\"error\",\"message\":\"a late failure\",\"code\":4}\n"
           "{\"v\":1,\"event\":\"exit\",\"code\":4}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* finish with no error noted after the stage closed: no error line, only the exit line after the stage */
    out = attach(SNX_PROGRESS_JSONL);
    snx_progress_stage(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_done(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_finish(3);
    expect(out, "{\"v\":1,\"stage\":\"input\",\"event\":\"start\"}\n"
                "{\"v\":1,\"stage\":\"input\",\"event\":\"done\"}\n"
                "{\"v\":1,\"event\":\"exit\",\"code\":3}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* a note while the stage is open is dropped by done: no error line */
    out = attach(SNX_PROGRESS_JSONL);
    snx_progress_stage(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_note_error("a note while open");
    snx_progress_done(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_finish(4);
    expect(out, "{\"v\":1,\"stage\":\"input\",\"event\":\"start\"}\n"
                "{\"v\":1,\"stage\":\"input\",\"event\":\"done\"}\n"
                "{\"v\":1,\"event\":\"exit\",\"code\":4}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* a child that ended by itself with a nonzero code wrote its own error line: the note is cleared */
    out = attach(SNX_PROGRESS_JSONL);
    snx_progress_stage(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_done(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_note_error("a child error line");
    snx_progress_child_restore(SNX_CHILD_EXITED_ERROR);
    snx_progress_finish(5);
    expect(out, "{\"v\":1,\"stage\":\"input\",\"event\":\"start\"}\n"
                "{\"v\":1,\"stage\":\"input\",\"event\":\"done\"}\n"
                "{\"v\":1,\"event\":\"exit\",\"code\":5}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* a child that did not start: the note is the only text, so finish writes it under the closed stage */
    out = attach(SNX_PROGRESS_JSONL);
    snx_progress_stage(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_done(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_note_error("cannot start the stage");
    snx_progress_child_restore(SNX_CHILD_NOT_STARTED);
    snx_progress_finish(1);
    expect(out, "{\"v\":1,\"stage\":\"input\",\"event\":\"start\"}\n"
                "{\"v\":1,\"stage\":\"input\",\"event\":\"done\"}\n"
                "{\"v\":1,\"stage\":\"input\",\"event\":\"error\",\"message\":\"cannot start the stage\","
                "\"code\":1}\n"
                "{\"v\":1,\"event\":\"exit\",\"code\":1}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* an interrupt keeps the note: finish writes it under the stage the child's next work names */
    out = attach(SNX_PROGRESS_JSONL);
    snx_progress_stage(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_done(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_stage_name(SNX_PROGRESS_STAGE_RUN);
    snx_progress_note_error("the recompile was interrupted");
    snx_progress_child_restore(SNX_CHILD_INTERRUPTED);
    snx_progress_finish(130);
    expect(out, "{\"v\":1,\"stage\":\"input\",\"event\":\"start\"}\n"
                "{\"v\":1,\"stage\":\"input\",\"event\":\"done\"}\n"
                "{\"v\":1,\"stage\":\"run\",\"event\":\"error\",\"message\":\"the recompile was interrupted\","
                "\"code\":130}\n"
                "{\"v\":1,\"event\":\"exit\",\"code\":130}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* a signal-ended child keeps the note: finish writes it under the stage the child's next work names */
    out = attach(SNX_PROGRESS_JSONL);
    snx_progress_stage(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_done(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_stage_name(SNX_PROGRESS_STAGE_RUN);
    snx_progress_note_error("the game was killed");
    snx_progress_child_restore(SNX_CHILD_SIGNALED);
    snx_progress_finish(143);
    expect(out, "{\"v\":1,\"stage\":\"input\",\"event\":\"start\"}\n"
                "{\"v\":1,\"stage\":\"input\",\"event\":\"done\"}\n"
                "{\"v\":1,\"stage\":\"run\",\"event\":\"error\",\"message\":\"the game was killed\","
                "\"code\":143}\n"
                "{\"v\":1,\"event\":\"exit\",\"code\":143}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* the stage name writes no start line: a note after it is the error line of that stage, then the exit line */
    out = attach(SNX_PROGRESS_JSONL);
    snx_progress_stage(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_done(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_stage_name(SNX_PROGRESS_STAGE_RUN);
    snx_progress_note_error("the game did not start");
    snx_progress_finish(6);
    expect(out, "{\"v\":1,\"stage\":\"input\",\"event\":\"start\"}\n"
                "{\"v\":1,\"stage\":\"input\",\"event\":\"done\"}\n"
                "{\"v\":1,\"stage\":\"run\",\"event\":\"error\",\"message\":\"the game did not start\","
                "\"code\":6}\n"
                "{\"v\":1,\"event\":\"exit\",\"code\":6}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* a stage name alone, with no note: no error line, only the exit line */
    out = attach(SNX_PROGRESS_JSONL);
    snx_progress_stage(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_done(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_stage_name(SNX_PROGRESS_STAGE_RUN);
    snx_progress_finish(7);
    expect(out, "{\"v\":1,\"stage\":\"input\",\"event\":\"start\"}\n"
                "{\"v\":1,\"stage\":\"input\",\"event\":\"done\"}\n"
                "{\"v\":1,\"event\":\"exit\",\"code\":7}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* the crash writer: one error line of the open stage, with the message and the code */
    out = attach(SNX_PROGRESS_JSONL);
    snx_progress_stage(SNX_PROGRESS_STAGE_RUN);
    snx_progress_crash_error("a crash of the game", 6);
    expect(out, "{\"v\":1,\"stage\":\"run\",\"event\":\"start\"}\n"
                "{\"v\":1,\"stage\":\"run\",\"event\":\"error\",\"message\":\"a crash of the game\",\"code\":6}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* the crash line's code: a negative one, the smallest int, and 6 */
    out = attach(SNX_PROGRESS_JSONL);
    snx_progress_stage(SNX_PROGRESS_STAGE_RUN);
    snx_progress_crash_error("a negative code", -3);
    snx_progress_crash_error("the smallest int", INT_MIN);
    snx_progress_crash_error("a crash code", 6);
    expect(out, "{\"v\":1,\"stage\":\"run\",\"event\":\"start\"}\n"
                "{\"v\":1,\"stage\":\"run\",\"event\":\"error\",\"message\":\"a negative code\",\"code\":-3}\n"
                "{\"v\":1,\"stage\":\"run\",\"event\":\"error\",\"message\":\"the smallest int\","
                "\"code\":-2147483648}\n"
                "{\"v\":1,\"stage\":\"run\",\"event\":\"error\",\"message\":\"a crash code\",\"code\":6}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* a crash message that fills the line's tail room: the closing fields stay, and the line stays one valid line */
    {
        static const char prefix_text[] = "{\"v\":1,\"stage\":\"run\",\"event\":\"error\",\"message\":\"";
        static const char suffix_text[] = "\",\"code\":6}\n";
        char message[3001];
        char text[EXPECTED_SIZE];
        char* line;
        size_t i, prefix, suffix, length, body;
        out = attach(SNX_PROGRESS_JSONL);
        snx_progress_stage(SNX_PROGRESS_STAGE_RUN);
        memset(message, '"', sizeof message - 1);
        message[sizeof message - 1] = 0;
        snx_progress_crash_error(message, 6);
        read_stream(out, text, sizeof text);
        line = strstr(text, prefix_text);
        assert(line);
        assert(!strstr(line + 1, prefix_text)); /* one error line */
        prefix = sizeof prefix_text - 1;
        suffix = sizeof suffix_text - 1;
        length = strlen(line);
        assert(length > prefix + suffix);
        assert(line[length - 1] == '\n');
        assert(!memchr(line, '\n', length - 1)); /* one line: the newline is its last byte */
        assert(!memcmp(line, prefix_text, prefix));
        assert(!memcmp(line + length - suffix, suffix_text, suffix));
        body = length - prefix - suffix;
        assert(body == SNX_PROGRESS_LINE_SIZE - SNX_PROGRESS_LINE_TAIL - prefix); /* the message filled the room */
        assert(body % 2 == 0);
        for (i = 0; i < body; i += 2) assert(line[prefix + i] == '\\' && line[prefix + i + 1] == '"');
        assert(length + 1 <= SNX_PROGRESS_LINE_SIZE);
        snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
        fclose(out);
    }
    /* no open stage: the crash writer writes nothing */
    out = attach(SNX_PROGRESS_JSONL);
    snx_progress_stage(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_done(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_crash_error("ignored", 6);
    expect(out, "{\"v\":1,\"stage\":\"input\",\"event\":\"start\"}\n"
                "{\"v\":1,\"stage\":\"input\",\"event\":\"done\"}\n");
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    /* the stream off writes nothing */
    out = attach(SNX_PROGRESS_OFF);
    snx_progress_stage(SNX_PROGRESS_STAGE_INPUT);
    snx_progress_error("ignored", 1);
    snx_progress_crash_error("ignored", 6);
    snx_progress_exit(1);
    assert(!fseek(out, 0, SEEK_END));
    assert(ftell(out) == 0);
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    fclose(out);
    puts("progress_stream passed");
    return 0;
}
