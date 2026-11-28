// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The Keyboard and Controllers pages of the engine menu, and the key capture
// that ends when the Keyboard page does not draw. The capture rule has no ImGui
// and no backend call, so the pure test checks it.

#pragma once

#include "nx/gpu/vk/imgui_settings.h"

// One key capture: the key bind row that waits for a key (-1 for none), whether
// the Keyboard page drew in the current frame, and whether the last key has a
// name that the settings model can hold.
struct EngineMenuKeyCapture {
    int row;
    bool drawn;
    bool unnamed;
};

// The row of a capture that waits for no key.
inline constexpr int kEngineMenuKeyCaptureNone = -1;

// A frame start: true when the capture must stop because the Keyboard page did
// not draw in the frame before. It clears the draw flag for the new frame.
bool EngineMenuKeyCaptureFrame(EngineMenuKeyCapture& capture);

// A new capture of a row. It clears the "no name" flag.
void EngineMenuKeyCaptureStart(EngineMenuKeyCapture& capture, int row);

// The Keyboard page drew in this frame. The page counts as drawn only while the
// menu is open; it returns true, so the caller stops the capture, when a capture
// runs and the menu is not open.
bool EngineMenuKeyCaptureDrawn(EngineMenuKeyCapture& capture, bool menu_open);

// The capture ends without a key.
void EngineMenuKeyCaptureStop(EngineMenuKeyCapture& capture);

// The state as at the start of the run.
void EngineMenuKeyCaptureReset(EngineMenuKeyCapture& capture);

// The content of the engine menu's Keyboard and Controllers pages.
// engine_menu.cpp owns the card and the page title; these functions draw the
// content only.
void EngineMenuDrawKeyboardPage(bool menu_open);
void EngineMenuDrawControllersPage(const char* model_page_name);

// The settings page helper of engine_menu.cpp that the pages above need.
void EngineMenuDrawSettingsPage(const char* model_page_name);

// The frame code: a capture that lost its page ends, and its backend capture
// goes off.
void EngineMenuInputFrameBegin();
// The ImGui context is made again.
void EngineMenuInputReset();
