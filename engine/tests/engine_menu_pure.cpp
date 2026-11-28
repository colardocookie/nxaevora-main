// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The engine menu's pure rules (src/nx/gpu/vk/engine_menu.cpp): the menu of a run from imgui, legacy_menu and whether
// the ImGui windows run, the SDL gamepad control to ImGui key mapping, and one finger as the pointer. Every expected
// value comes from the decisions: the legacy menu without the Dear ImGui windows or with legacy_menu, the engine menu
// only with imgui=1 and legacy_menu=0 and the windows running, no menu when they did not start, the physical face
// positions (south = activate, east = back), and only the first finger driving the pointer.
// The key capture rule (src/nx/gpu/vk/engine_menu_input.cpp): a capture stays while the Keyboard page draws in each
// frame and the menu is open, stops in the first frame after a frame without the page or when the menu is not open, a
// reset stops it, and a new start clears the note that the last key has no name.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL_gamepad.h>

#include "imgui.h"
#include "nx/gpu/vk/engine_menu.h"
#include "nx/gpu/vk/engine_menu_debug.h"
#include "nx/gpu/vk/engine_menu_game.h"
#include "nx/gpu/vk/engine_menu_input.h"

namespace {

constexpr double kTolerance = 1e-4;

void Check(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
}

void CheckChoice(int imgui, int legacy_menu, int running, int expected, const char* what) {
    Check(engine_menu_choice(imgui, legacy_menu, running) == expected, what);
}

void CheckPointer(EngineMenuPointer p, int action, float x, float y, const char* what) {
    Check(p.action == action, what);
    Check(std::fabs(p.x - x) <= kTolerance && std::fabs(p.y - y) <= kTolerance, what);
}

void CheckButton(int button, int key, const char* what) {
    EngineMenuPadEvent out[2];
    const int count = engine_menu_pad_events(ENGINE_MENU_PAD_BUTTON, button, 1, out);
    Check(count == 1 && out[0].key == key && out[0].down == 1, what);
    const int release = engine_menu_pad_events(ENGINE_MENU_PAD_BUTTON, button, 0, out);
    Check(release == 1 && out[0].key == key && out[0].down == 0, what);
}

void CheckStick(int axis, int value, int positive_key, int negative_key, bool positive, const char* what) {
    EngineMenuPadEvent out[2];
    const int count = engine_menu_pad_events(ENGINE_MENU_PAD_AXIS, axis, value, out);
    Check(count == 2 && out[0].key == positive_key && out[1].key == negative_key, what);
    Check(out[positive ? 0 : 1].down == 1 && out[positive ? 1 : 0].down == 0, what);
    Check(std::fabs(out[positive ? 0 : 1].value - 1.0f) <= kTolerance, what);
}

void CheckTrigger(int axis, int value, int key, bool down, const char* what) {
    EngineMenuPadEvent out[2];
    const int count = engine_menu_pad_events(ENGINE_MENU_PAD_AXIS, axis, value, out);
    Check(count == 1 && out[0].key == key && out[0].down == (down ? 1 : 0), what);
}

void CheckKeyCapture() {
    EngineMenuKeyCapture capture{kEngineMenuKeyCaptureNone, false, false};

    /* a new start clears the note that the last key has no name */
    capture.unnamed = true;
    EngineMenuKeyCaptureStart(capture, 3);
    Check(capture.row == 3 && !capture.unnamed, "a new capture clears the no name note");
    Check(capture.drawn, "a new capture marks the page as drawn");

    /* an open menu marks the page as drawn and does not stop the capture */
    Check(!EngineMenuKeyCaptureDrawn(capture, true), "an open menu does not stop the capture");
    Check(capture.drawn, "an open menu marks the page as drawn");

    /* the page draws in each frame: the capture stays */
    Check(!EngineMenuKeyCaptureFrame(capture), "a capture stays while the page draws in each frame");
    EngineMenuKeyCaptureDrawn(capture, true);
    Check(!EngineMenuKeyCaptureFrame(capture), "a capture stays in the second drawn frame");

    /* a running capture stops when the menu is not open */
    Check(EngineMenuKeyCaptureDrawn(capture, false), "a running capture stops when the menu is not open");

    /* the page does not draw: the next frame stops the capture */
    Check(EngineMenuKeyCaptureFrame(capture), "a capture stops in the first frame after a frame with no draw");
    EngineMenuKeyCaptureStop(capture);
    Check(capture.row == kEngineMenuKeyCaptureNone, "a stopped capture waits for no key");

    /* no capture with the menu not open does not stop */
    Check(!EngineMenuKeyCaptureDrawn(capture, false), "no capture with the menu not open does not stop");

    /* a reset stops the capture */
    EngineMenuKeyCaptureStart(capture, 1);
    EngineMenuKeyCaptureReset(capture);
    Check(capture.row == kEngineMenuKeyCaptureNone && !capture.drawn && !capture.unnamed,
          "a reset stops the capture");
}

// The keys of the Debug section of settings.txt, but imgui, legacy_menu and
// imgui_test, which the page does not show. The list is written here because
// the test has no settings.txt: each key must appear in the page once.
const char* const kDebugKeys[] = {
    "log",         "log_max",          "auto_stub_ipc",     "diag",            "vk_validation",
    "native_astc", "native_bc",        "native_etc2",       "slp_vectorizer",  "fpsr_flags",
    "volatile_memory", "gdb_jit",      "alias_pool",        "profile",         "nvn_trace",
    "nvn_legacy",  "nvn_drawrec",      "nvn_measure",       "nvn_native_missing", "nvn_selftest",
    "nvn_selftest_mutate", "nvn_exectest", "shader_corpus", "gpu_trace_target", "vk_gpu_profile",
    "vk_buffer_stats", "vk_record",    "compute",           "vk_max_allocation_mb", "vk_budget_mb",
    "mme_hle",     "mme_jit",          "mme_verify",        "renderdoc",       "renderdoc_frames",
    "tracy_profiler", "tracy_sampling", "tracy_port",       "tracy_capture_tool", "tracy_open_trace",
};
constexpr int kDebugKeyCount = static_cast<int>(sizeof kDebugKeys / sizeof kDebugKeys[0]);
constexpr int kOverlayKeyCount = 4;

void CheckDebugPage() {
    int count = 0;
    const EngineMenuDebugEntry* entries = EngineMenuDebugEntries(&count);
    Check(entries != nullptr && count == kDebugKeyCount + kOverlayKeyCount,
          "the Debug page has the Debug rows and the Overlays rows");

    // Each key of the Debug section is on the page exactly once.
    for (int k = 0; k < kDebugKeyCount; ++k) {
        int found = 0;
        for (int i = 0; i < count; ++i)
            if (std::strcmp(entries[i].key, kDebugKeys[k]) == 0) ++found;
        Check(found == 1, kDebugKeys[k]);
    }

    // imgui, legacy_menu and the deleted imgui_test are not on the page.
    Check(EngineMenuDebugEntryOfKey("imgui") == nullptr, "imgui stays off the page");
    Check(EngineMenuDebugEntryOfKey("legacy_menu") == nullptr, "legacy_menu stays off the page");
    Check(EngineMenuDebugEntryOfKey("imgui_test") == nullptr, "imgui_test is gone");
    Check(EngineMenuDebugEntryOfKey(nullptr) == nullptr, "a null key is not on the page");

    // The keys of the SNX_TRACY blocks of settings.txt, written from settings.txt:
    // a build without SNX_TRACY leaves these out, so only they are marked.
    static const char* const build_optional_keys[] = {
        "tracy_profiler", "tracy_sampling", "tracy_port", "tracy_capture_tool", "tracy_open_trace",
    };
    constexpr int kBuildOptionalKeyCount = static_cast<int>(sizeof build_optional_keys / sizeof build_optional_keys[0]);
    int marked = 0;
    for (int i = 0; i < count; ++i) {
        if (!entries[i].build_optional) continue;
        ++marked;
        int expected = 0;
        for (int k = 0; k < kBuildOptionalKeyCount; ++k)
            if (std::strcmp(entries[i].key, build_optional_keys[k]) == 0) ++expected;
        Check(expected == 1, entries[i].key);
    }
    Check(marked == kBuildOptionalKeyCount, "5 keys a build can leave out are marked");
    const EngineMenuDebugEntry* log_entry = EngineMenuDebugEntryOfKey("log");
    Check(log_entry != nullptr && !log_entry->build_optional, "log is not a key a build leaves out");

    // The 4 overlay keys are in the Overlays card in the page's order.
    static const char* const overlay_keys[kOverlayKeyCount] = {"stats", "stats_corner", "showcase",
                                                               "showcase_corner"};
    int overlay_count = 0;
    const EngineMenuDebugEntry* overlays = EngineMenuDebugCardEntries(ENGINE_MENU_DEBUG_OVERLAYS, &overlay_count);
    Check(overlays != nullptr && overlay_count == kOverlayKeyCount, "4 keys in the Overlays card");
    for (int i = 0; i < overlay_count; ++i) Check(std::strcmp(overlays[i].key, overlay_keys[i]) == 0,
                                                  "the Overlays keys in the page's order");

    // The read-only cards have no settings row.
    for (int card = ENGINE_MENU_DEBUG_HARDWARE; card <= ENGINE_MENU_DEBUG_DISPLAY; ++card) {
        int read_only = -1;
        Check(EngineMenuDebugCardEntries(card, &read_only) == nullptr && read_only == 0,
              "a read-only card has no row");
    }

    // The cards in the order of the page.
    static const char* const card_names[ENGINE_MENU_DEBUG_CARD_COUNT] = {
        "Overlays",   "Hardware",      "Frame",           "Display",           "Logs and IPC",
        "Textures",   "Recompiler and memory", "Vulkan",   "NVN diagnostics",   "Macros",
        "Profilers and captures",
    };
    for (int card = 0; card < ENGINE_MENU_DEBUG_CARD_COUNT; ++card)
        Check(std::strcmp(EngineMenuDebugCardName(card), card_names[card]) == 0, "the card order");
    Check(std::strcmp(EngineMenuDebugCardName(-1), "") == 0 &&
              std::strcmp(EngineMenuDebugCardName(ENGINE_MENU_DEBUG_CARD_COUNT), "") == 0,
          "a card outside the list has no title");

    // Three labels, written from the mockup.
    const EngineMenuDebugEntry* log = EngineMenuDebugEntryOfKey("log");
    const EngineMenuDebugEntry* mme = EngineMenuDebugEntryOfKey("mme_hle");
    const EngineMenuDebugEntry* stats = EngineMenuDebugEntryOfKey("stats");
    Check(log != nullptr && std::strcmp(log->label, "Log level") == 0, "the log label");
    Check(mme != nullptr && std::strcmp(mme->label, "Native macros") == 0, "the mme_hle label");
    Check(stats != nullptr && std::strcmp(stats->label, "Performance overlay") == 0, "the stats label");
}

// The Game page's pure text rules (src/nx/gpu/vk/engine_menu_game.cpp): the play time as H:MM:SS with no limit on the
// hours, the title ID as 16 upper-case hex digits, and its six row labels in the page's order.
void CheckGamePage() {
    struct TimeCase {
        uint64_t ns;
        const char* text;
    };
    // The four values come from the decision, not from the format: 0 ns, 59.999 s, 3661 s and 100 h.
    static const TimeCase times[] = {
        {0ull, "0:00:00"},
        {59999000000ull, "0:00:59"},
        {3661000000000ull, "1:01:01"},
        {360000000000000ull, "100:00:00"},
    };
    for (const TimeCase& c : times) {
        char out[32];
        EngineMenuGamePlayTime(c.ns, out, sizeof out);
        Check(std::strcmp(out, c.text) == 0, "the play time format");
    }

    // 0x01001F5010DFA000 and 0x1, in 16 upper-case hex digits.
    {
        char out[17];
        EngineMenuGameTitleId(0x01001F5010DFA000ull, out, sizeof out);
        Check(std::strcmp(out, "01001F5010DFA000") == 0, "the title ID of the game");
        EngineMenuGameTitleId(0x1ull, out, sizeof out);
        Check(std::strcmp(out, "0000000000000001") == 0, "a title ID of 1");
    }

    // The six labels of the card, in the page's order.
    static const char* const labels[ENGINE_MENU_GAME_ROW_COUNT] = {"Title", "Version", "Title ID",
                                                                   "Content", "Play time", "Build"};
    for (int row = 0; row < ENGINE_MENU_GAME_ROW_COUNT; ++row)
        Check(std::strcmp(EngineMenuGameRowLabel(row), labels[row]) == 0, "the Game card row labels");
    Check(std::strcmp(EngineMenuGameRowLabel(-1), "") == 0 &&
              std::strcmp(EngineMenuGameRowLabel(ENGINE_MENU_GAME_ROW_COUNT), "") == 0,
          "a row outside the list has no label");
}

} // namespace

int main() {
    CheckChoice(0, 0, 0, ENGINE_MENU_LEGACY, "imgui=0 uses the legacy menu");
    CheckChoice(0, 0, 1, ENGINE_MENU_LEGACY, "imgui=0 with the windows running uses the legacy menu");
    CheckChoice(0, 1, 0, ENGINE_MENU_LEGACY, "imgui=0 with legacy_menu=1 uses the legacy menu");
    CheckChoice(0, 1, 1, ENGINE_MENU_LEGACY, "imgui=0 with legacy_menu=1 and the windows running uses the legacy menu");
    CheckChoice(1, 0, 0, ENGINE_MENU_NONE, "imgui=1 without legacy_menu and the windows not running has no menu");
    CheckChoice(1, 0, 1, ENGINE_MENU_ENGINE, "imgui=1 without legacy_menu uses the engine menu");
    CheckChoice(1, 1, 0, ENGINE_MENU_LEGACY, "legacy_menu=1 with the windows not running uses the legacy menu");
    CheckChoice(1, 1, 1, ENGINE_MENU_LEGACY, "legacy_menu=1 uses the legacy menu");

    {
        uint64_t first = 0;
        CheckPointer(engine_menu_pointer(ENGINE_MENU_FINGER_DOWN, 7, 0.0f, 0.0f, 1280, 720, &first),
                     ENGINE_MENU_POINTER_PRESS, 0.0f, 0.0f, "finger down at the top left");
        Check(first == 7, "the first finger drives the pointer");
        CheckPointer(engine_menu_pointer(ENGINE_MENU_FINGER_MOTION, 7, 1.0f, 1.0f, 1280, 720, &first),
                     ENGINE_MENU_POINTER_MOVE, 1280.0f, 720.0f, "finger motion at the bottom right");
        CheckPointer(engine_menu_pointer(ENGINE_MENU_FINGER_MOTION, 9, 0.5f, 0.5f, 1280, 720, &first),
                     ENGINE_MENU_POINTER_IGNORED, 0.0f, 0.0f, "a second finger is ignored");
        CheckPointer(engine_menu_pointer(ENGINE_MENU_FINGER_DOWN, 9, 0.5f, 0.5f, 1280, 720, &first),
                     ENGINE_MENU_POINTER_IGNORED, 0.0f, 0.0f, "a second finger down is ignored");
        CheckPointer(engine_menu_pointer(ENGINE_MENU_FINGER_UP, 9, 0.5f, 0.5f, 1280, 720, &first),
                     ENGINE_MENU_POINTER_IGNORED, 0.0f, 0.0f, "the second finger's up is ignored");
        Check(first == 7, "the first finger still drives the pointer");
        CheckPointer(engine_menu_pointer(ENGINE_MENU_FINGER_UP, 7, 0.5f, 0.5f, 1280, 720, &first),
                     ENGINE_MENU_POINTER_RELEASE, 640.0f, 360.0f, "finger up releases the pointer");
        Check(first == 0, "no finger drives the pointer after the up");
        CheckPointer(engine_menu_pointer(ENGINE_MENU_FINGER_DOWN, 9, 0.5f, 0.5f, 1280, 720, &first),
                     ENGINE_MENU_POINTER_PRESS, 640.0f, 360.0f, "the center of the window is a press");
    }

    CheckButton(SDL_GAMEPAD_BUTTON_SOUTH, ImGuiKey_GamepadFaceDown, "south activates");
    CheckButton(SDL_GAMEPAD_BUTTON_EAST, ImGuiKey_GamepadFaceRight, "east goes back");
    CheckButton(SDL_GAMEPAD_BUTTON_WEST, ImGuiKey_GamepadFaceLeft, "west");
    CheckButton(SDL_GAMEPAD_BUTTON_NORTH, ImGuiKey_GamepadFaceUp, "north");
    CheckButton(SDL_GAMEPAD_BUTTON_BACK, ImGuiKey_GamepadBack, "back");
    CheckButton(SDL_GAMEPAD_BUTTON_START, ImGuiKey_GamepadStart, "start");
    CheckButton(SDL_GAMEPAD_BUTTON_LEFT_STICK, ImGuiKey_GamepadL3, "left stick click");
    CheckButton(SDL_GAMEPAD_BUTTON_RIGHT_STICK, ImGuiKey_GamepadR3, "right stick click");
    CheckButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, ImGuiKey_GamepadL1, "left shoulder");
    CheckButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, ImGuiKey_GamepadR1, "right shoulder");
    CheckButton(SDL_GAMEPAD_BUTTON_DPAD_UP, ImGuiKey_GamepadDpadUp, "d-pad up");
    CheckButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, ImGuiKey_GamepadDpadDown, "d-pad down");
    CheckButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT, ImGuiKey_GamepadDpadLeft, "d-pad left");
    CheckButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, ImGuiKey_GamepadDpadRight, "d-pad right");

    {
        EngineMenuPadEvent out[2];
        Check(engine_menu_pad_events(ENGINE_MENU_PAD_BUTTON, SDL_GAMEPAD_BUTTON_MISC1, 1, out) == 0,
              "an unmapped button makes no event");
    }

    CheckStick(SDL_GAMEPAD_AXIS_LEFTX, 32767, ImGuiKey_GamepadLStickRight, ImGuiKey_GamepadLStickLeft, true,
               "left X right");
    CheckStick(SDL_GAMEPAD_AXIS_LEFTX, -32767, ImGuiKey_GamepadLStickRight, ImGuiKey_GamepadLStickLeft, false,
               "left X left");
    CheckStick(SDL_GAMEPAD_AXIS_LEFTY, 32767, ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickUp, true,
               "left Y down");
    CheckStick(SDL_GAMEPAD_AXIS_LEFTY, -32767, ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickUp, false,
               "left Y up");
    CheckStick(SDL_GAMEPAD_AXIS_RIGHTX, 32767, ImGuiKey_GamepadRStickRight, ImGuiKey_GamepadRStickLeft, true,
               "right X right");
    CheckStick(SDL_GAMEPAD_AXIS_RIGHTY, -32767, ImGuiKey_GamepadRStickDown, ImGuiKey_GamepadRStickUp, false,
               "right Y up");
    {
        EngineMenuPadEvent out[2];
        const int count = engine_menu_pad_events(ENGINE_MENU_PAD_AXIS, SDL_GAMEPAD_AXIS_LEFTX, 0, out);
        Check(count == 2 && out[0].key == ImGuiKey_GamepadLStickRight &&
                  out[1].key == ImGuiKey_GamepadLStickLeft && out[0].down == 0 && out[1].down == 0,
              "a centered stick is let go");
    }
    CheckTrigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767, ImGuiKey_GamepadL2, true, "left trigger down");
    CheckTrigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0, ImGuiKey_GamepadL2, false, "left trigger up");
    CheckTrigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767, ImGuiKey_GamepadR2, true, "right trigger down");

    CheckKeyCapture();
    CheckDebugPage();
    CheckGamePage();

    std::puts("engine_menu: ok");
    return 0;
}
