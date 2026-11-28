// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The Game page of the engine menu (engine_menu_game.cpp): the labels and text
// formats of its read-only card, and the card, the Pause switch and the Exit
// button with its dialog. The text formats have no ImGui, so the pure test reads
// them.

#pragma once

#include <stddef.h>
#include <stdint.h>

// The rows of the read-only card, in the order of the page.
enum {
    ENGINE_MENU_GAME_TITLE,
    ENGINE_MENU_GAME_VERSION,
    ENGINE_MENU_GAME_TITLE_ID,
    ENGINE_MENU_GAME_CONTENT,
    ENGINE_MENU_GAME_PLAY_TIME,
    ENGINE_MENU_GAME_BUILD,
    ENGINE_MENU_GAME_ROW_COUNT
};

// The label of a row, "" outside the list.
const char* EngineMenuGameRowLabel(int row);

// The play time of ns nanoseconds as H:MM:SS, the hours without a limit: 0 ns is
// "0:00:00", 59.999 s is "0:00:59", 3661 s is "1:01:01" and 100 h is "100:00:00".
// A size of 0 writes nothing.
void EngineMenuGamePlayTime(uint64_t ns, char* out, size_t size);

// The title ID as 16 upper-case hex digits: 0x1 is "0000000000000001". A size of
// 0 writes nothing.
void EngineMenuGameTitleId(uint64_t id, char* out, size_t size);

#if defined(__cplusplus) && !defined(ENGINE_MENU_PURE_TEST)
// The content of the Game page. engine_menu.cpp owns the page window and its
// scroll; this function draws the card, the Pause switch and the Exit button.
void EngineMenuDrawGamePage(void);
#endif

// The Game page's C surface. A C++ unit cannot include main.h, clock.h, pause.h or overlay.h, whose cpu.h is C only
// (vpad.h keeps ui.h out of a C++ unit for the same reason): the page calls these instead, and the frontend's C side
// (sdl3.c) does the runtime's own work on the frontend's thread.
#ifdef __cplusplus
extern "C" {
#endif
typedef struct EngineMenuGameCard {
    char title[256];
    char version[16];
    char content[256];
    unsigned long long title_id;
} EngineMenuGameCard;

// main.c's snx_game_info: the values that do not change during a run.
void EngineMenuGameCardLoad(EngineMenuGameCard* out);
// clock.c's guest_ns, the play time's source.
unsigned long long EngineMenuGameElapsedNs(void);
// pause.c's game_player_paused, the player's pause alone.
int EngineMenuGamePlayerPaused(void);
// overlay.h's OVERLAY_ACTION_PAUSE and OVERLAY_ACTION_EXIT, done on the frontend's thread.
enum { ENGINE_MENU_GAME_PAUSE, ENGINE_MENU_GAME_EXIT };
void EngineMenuGameAction(int action);
#ifdef __cplusplus
}
#endif
