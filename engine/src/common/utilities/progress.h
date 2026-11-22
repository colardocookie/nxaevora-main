/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The JSON Lines progress stream of the commands (docs/records/ui/engine-ui-plan.md, unit 3). One line for each
   event on a private copy of stdout; the human text goes to stderr. Version 1 */
#ifndef SNX_PROGRESS_H
#define SNX_PROGRESS_H
#include <stdio.h>

/* the mode of the stream; snx_progress_parse returns one, or -1 for a refused value */
#define SNX_PROGRESS_OFF 0
#define SNX_PROGRESS_JSONL 1       /* a command process: stage, error and exit lines */
#define SNX_PROGRESS_JSONL_STAGE 2 /* an internal child stage: stage and error lines, no exit line */

/* the stage names of the lines; snx_progress_stage and the others take one of these */
#define SNX_PROGRESS_STAGE_INPUT "input"
#define SNX_PROGRESS_STAGE_RECOMPILE "recompile"
#define SNX_PROGRESS_STAGE_LINK "link"
#define SNX_PROGRESS_STAGE_BUILD "build"
#define SNX_PROGRESS_STAGE_PREPARE "prepare"
#define SNX_PROGRESS_STAGE_START "start"
#define SNX_PROGRESS_STAGE_RUN "run"

/* the room of one line, and the room the end of the line keeps free of the message */
#define SNX_PROGRESS_LINE_SIZE 4096
#define SNX_PROGRESS_LINE_TAIL 64

/* --progress=jsonl or --progress=jsonl-stage: its mode, -1 for any other value */
int snx_progress_parse(const char* arg);
/* makes fd 1 a copy of fd 2 and opens the stream on the old fd 1; 0, or nonzero after snx_error */
int snx_progress_open(int mode);
/* the mode of this process, SNX_PROGRESS_OFF when the stream is off */
int snx_progress_mode(void);
/* writes "start" and makes stage the open stage of this process */
void snx_progress_stage(const char* stage);
/* writes "done" when stage is the open stage of this process, and clears it; otherwise writes nothing */
void snx_progress_done(const char* stage);
/* writes "progress" with value and total for stage */
void snx_progress_value(const char* stage, unsigned long long value, unsigned long long total);
/* writes "error" for the open stage with message and code, and clears it; nothing without an open stage */
void snx_progress_error(const char* message, int code);
/* snx_progress_error with the game's own exit code as game_code (the process ends with SNX_EXIT_GAME_CODE) */
void snx_progress_game_error(const char* message, int code, int game_code);
/* writes the exit line, in mode SNX_PROGRESS_JSONL only */
void snx_progress_exit(int code);
/* keeps text as the last error, a bounded copy, for snx_progress_finish */
void snx_progress_note_error(const char* text);
/* The end of a command process: the open stage's error line, or the last closed stage's when this process noted an
   error after it closed, when code is nonzero; then the exit line. A child stage writes no exit line */
void snx_progress_finish(int code);
/* Puts the private stream on fd 1 for the spawn of a child, and stderr back after it returns (driver.c). When the
   child did not start, the restore keeps the noted error: no child wrote an error line */
void snx_progress_child_redirect(void);
void snx_progress_child_restore(int child_started);
/* tests: sends the stream to out instead of a duplicated stdout; NULL detaches */
void snx_progress_test_attach(FILE* out, int mode);

#endif
