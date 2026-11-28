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

#include <SDL3/SDL_gamepad.h>

#include "imgui.h"
#include "nx/gpu/vk/engine_menu.h"
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

    std::puts("engine_menu: ok");
    return 0;
}
