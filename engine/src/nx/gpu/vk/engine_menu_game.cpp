// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The Game page of the engine menu (engine_menu_game.h). The card's labels, the
// play time and the title ID formats are pure; the card reads its values one time,
// at the first draw of the page, and the Pause switch and Exit request the
// frontend's thread, where the game's pause and g_quit belong.

#include "nx/gpu/vk/engine_menu_game.h"

#include <cstdio>

namespace {

const char* const kRowLabels[ENGINE_MENU_GAME_ROW_COUNT] = {
    "Title", "Version", "Title ID", "Content", "Play time", "Build",
};

// 1000000000 nanoseconds in a second, 60 seconds in a minute and 60 minutes in an hour.
constexpr uint64_t kNanosecondsPerSecond = 1000000000ull;
constexpr uint64_t kSecondsPerMinute = 60ull;
constexpr uint64_t kMinutesPerHour = 60ull;
constexpr uint64_t kSecondsPerHour = kSecondsPerMinute * kMinutesPerHour;

} // namespace

const char* EngineMenuGameRowLabel(int row) {
    if (row < 0 || row >= ENGINE_MENU_GAME_ROW_COUNT) return "";
    return kRowLabels[row];
}

void EngineMenuGamePlayTime(uint64_t ns, char* out, size_t size) {
    if (out == nullptr || size == 0) return;
    const uint64_t seconds = ns / kNanosecondsPerSecond;
    std::snprintf(out, size, "%llu:%02llu:%02llu", (unsigned long long)(seconds / kSecondsPerHour),
                  (unsigned long long)(seconds / kSecondsPerMinute % kMinutesPerHour),
                  (unsigned long long)(seconds % kSecondsPerMinute));
}

void EngineMenuGameTitleId(uint64_t id, char* out, size_t size) {
    if (out == nullptr || size == 0) return;
    std::snprintf(out, size, "%016llX", (unsigned long long)id);
}

#ifndef ENGINE_MENU_PURE_TEST

#include "imgui.h"
#include "nx/gpu/vk/md3/md3_theme.h"
#include "nx/gpu/vk/md3/md3_widgets.h"
// The runtime's header holds C functions and has no extern "C" guard: a C++ unit needs the block. The other runtime
// headers cannot be included here, because cpu.h is C only: engine_menu_game.h declares the calls instead.
extern "C" {
#include "common/utilities/build_info.h"
}

namespace {

// The largest value text of the card, and a line of a label and a value.
constexpr int kTextMax = 256;
constexpr int kVersionMax = 16;
constexpr int kBuildMax = 192;
// 16 hex digits and the 0 after them.
constexpr int kTitleIdMax = 17;
constexpr int kLineMax = kTextMax + kTitleIdMax;
// A play time of many hours still has room, with the colons and the two-digit minutes and seconds.
constexpr int kPlayTimeMax = 32;

const char* const kUnknown = "Unknown";
const char* const kBaseGame = "Base game";
const char* const kGameCardId = "##game_card";
const char* const kExitDialogId = "exit_dialog";
const char* const kPauseLabel = "Pause";
const char* const kPauseHelp = "Freezes the game behind the panel";
const char* const kExitLabel = "Exit game";
const char* const kExitHeader = "Exit the game?";
const char* const kExitBody = "The game closes. Unsaved progress is lost.";
constexpr float kDialogWidthPx = 300.0f;

// The values of the card that do not change during a run, read at its first draw.
struct GameInfo {
    bool loaded;
    char title[kTextMax];
    char version[kVersionMax];
    char title_id[kTitleIdMax];
    char content[kTextMax];
    char build[kBuildMax];
};

GameInfo g_info;

const char* TextOrUnknown(const char* text) {
    return text != nullptr && text[0] != '\0' ? text : kUnknown;
}

void LoadGameInfo(GameInfo* info) {
    EngineMenuGameCard game{};
    EngineMenuGameCardLoad(&game);
    std::snprintf(info->title, sizeof info->title, "%s", TextOrUnknown(game.title));
    std::snprintf(info->version, sizeof info->version, "%s", TextOrUnknown(game.version));
    if (game.title_id != 0)
        EngineMenuGameTitleId(game.title_id, info->title_id, sizeof info->title_id);
    else
        std::snprintf(info->title_id, sizeof info->title_id, "%s", kUnknown);
    std::snprintf(info->content, sizeof info->content, "%s", game.content[0] != '\0' ? game.content : kBaseGame);
    std::snprintf(info->build, sizeof info->build, "%s", TextOrUnknown(snx_build_info()));
    info->loaded = true;
}

const GameInfo& GameInfoNow() {
    if (!g_info.loaded) LoadGameInfo(&g_info);
    return g_info;
}

// A line of the read-only card: the label and the value, as the mockup's InfoRow and the Debug page's cards show.
void DrawInfoRow(int row, const char* value) {
    char line[kLineMax];
    std::snprintf(line, sizeof line, "%-12s %s", EngineMenuGameRowLabel(row),
                  value != nullptr ? value : kUnknown);
    md3::BodyText(line);
}

void DrawGameCard() {
    const GameInfo& info = GameInfoNow();
    char play_time[kPlayTimeMax];
    EngineMenuGamePlayTime(EngineMenuGameElapsedNs(), play_time, sizeof play_time);
    md3::CardBegin(kGameCardId);
    md3::SectionTitle("Game");
    DrawInfoRow(ENGINE_MENU_GAME_TITLE, info.title);
    DrawInfoRow(ENGINE_MENU_GAME_VERSION, info.version);
    DrawInfoRow(ENGINE_MENU_GAME_TITLE_ID, info.title_id);
    DrawInfoRow(ENGINE_MENU_GAME_CONTENT, info.content);
    DrawInfoRow(ENGINE_MENU_GAME_PLAY_TIME, play_time);
    DrawInfoRow(ENGINE_MENU_GAME_BUILD, info.build);
    md3::CardEnd();
}

void DrawPauseRow() {
    const md3::Theme& theme = md3::CurrentTheme();
    bool paused = EngineMenuGamePlayerPaused() != 0;
    md3::RowBegin("##pause", kPauseLabel, kPauseHelp, false, md3::kSwitchWidthPx * theme.scale,
                  md3::kSwitchHeightPx * theme.scale);
    if (md3::Switch("##pause", &paused)) EngineMenuGameAction(ENGINE_MENU_GAME_PAUSE);
    md3::RowEnd();
}

void DrawExitButton() {
    if (md3::Button("##exit_game", kExitLabel, md3::ButtonKind::ErrorOutlined)) md3::DialogOpen(kExitDialogId);
    const bool open = md3::DialogBegin(kExitDialogId, kExitHeader,
                                       kDialogWidthPx * md3::CurrentTheme().scale);
    if (open) {
        md3::BodyText(kExitBody);
        ImGui::Spacing();
        if (md3::Button("##exit_cancel", "Cancel", md3::ButtonKind::Text)) md3::DialogClose();
        // The dialog opens with the focus on Cancel, so a stray press does not exit.
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (md3::Button("##exit_confirm", "Exit", md3::ButtonKind::Filled)) {
            EngineMenuGameAction(ENGINE_MENU_GAME_EXIT);
            md3::DialogClose();
        }
    }
    md3::DialogEnd(open);
}

} // namespace

void EngineMenuDrawGamePage(void) {
    DrawGameCard();
    // The Save states card goes here, before the Pause switch.
    ImGui::Spacing();
    DrawPauseRow();
    ImGui::Spacing();
    DrawExitButton();
}

#endif // ENGINE_MENU_PURE_TEST
