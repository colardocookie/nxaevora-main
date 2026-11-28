// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The Keyboard and Controllers pages of the engine menu and their key capture
// (engine_menu_input.h). The model changes through imgui_keybind_set,
// imgui_keybind_capture, imgui_padmap_set and imgui_padmap_reset only; the pages
// read it with imgui_keybind_view and imgui_padmap_view.

#include "nx/gpu/vk/engine_menu_input.h"

bool EngineMenuKeyCaptureFrame(EngineMenuKeyCapture& capture) {
    const bool stop = capture.row != kEngineMenuKeyCaptureNone && !capture.drawn;
    capture.drawn = false;
    return stop;
}

void EngineMenuKeyCaptureStart(EngineMenuKeyCapture& capture, int row) {
    capture.row = row;
    capture.unnamed = false;
    capture.drawn = true;
}

bool EngineMenuKeyCaptureDrawn(EngineMenuKeyCapture& capture, bool menu_open) {
    if (menu_open) {
        capture.drawn = true;
        return false;
    }
    return capture.row != kEngineMenuKeyCaptureNone;
}

void EngineMenuKeyCaptureStop(EngineMenuKeyCapture& capture) {
    capture.row = kEngineMenuKeyCaptureNone;
}

void EngineMenuKeyCaptureReset(EngineMenuKeyCapture& capture) {
    capture.row = kEngineMenuKeyCaptureNone;
    capture.drawn = false;
    capture.unnamed = false;
}

#ifndef ENGINE_MENU_PURE_TEST

#include <cstdio>

#include "imgui.h"
#include "nx/gpu/vk/md3/md3_theme.h"
#include "nx/gpu/vk/md3/md3_widgets.h"

namespace {

constexpr int kKeyNameMax = 64;
// The supporting text of a key row: the keys and the default mark.
constexpr int kKeyRowTextMax = IMGUI_KEYBIND_KEYS + 16;
constexpr int kCaptureCancelled = -1; // imgui_keybind_captured: Esc or a loss of the focus

// The right part of a key row holds the three text buttons of the old window.
constexpr float kKeyRowRightWidthPx = 280.0f;
// The right part of a button map row holds the button list of the old window.
constexpr float kPadMapRightWidthPx = 240.0f;

constexpr const char* kKeyBindingsHelp =
    "Bind, then press the key. A key marked (default) is the default of its button. "
    "Esc cancels, and F1 to F12, Pause and Alt cannot be bound.";
constexpr const char* kKeyNoName = "That key has no name that can be saved, so it cannot be bound.";
constexpr const char* kKeyCaptureHint = "Press a key (Esc cancels)";
constexpr const char* kKeyboardLocked = "keyboard_map is set on the command line for this run.";
constexpr const char* kButtonMapHelp =
    "The Switch button that each control of a gamepad sends. A button that another control sends "
    "goes to this one's old control, so no button is lost. This is the map of every player "
    "(a player's own gamepad_pN_map in the .ini goes over it).";
constexpr const char* kGamepadLocked = "gamepad_map is set on the command line for this run.";

EngineMenuKeyCapture g_capture{kEngineMenuKeyCaptureNone, false, false};

void CaptureStop() {
    if (g_capture.row == kEngineMenuKeyCaptureNone) return;
    imgui_keybind_capture(0);
    EngineMenuKeyCaptureStop(g_capture);
}

void CaptureStart(int row) {
    EngineMenuKeyCaptureStart(g_capture, row);
    imgui_keybind_capture(1);
}

// The key that the capture took goes to the row that asked for it; a cancel or
// an unnamed key ends the wait.
void CaptureTake(const ImguiKeybind* keys) {
    if (g_capture.row == kEngineMenuKeyCaptureNone || keys == nullptr) return;
    char key[kKeyNameMax];
    const int result = imgui_keybind_captured(key, sizeof key);
    if (result == 0) return;
    if (result > 0) imgui_keybind_set(keys[g_capture.row].button, key);
    g_capture.unnamed = result < 0 && result != kCaptureCancelled;
    EngineMenuKeyCaptureStop(g_capture);
}

void DrawKeyRow(const ImguiKeybind& bind, int row, bool menu_open) {
    const md3::Theme& theme = md3::CurrentTheme();
    const float s = theme.scale;
    const bool capturing = g_capture.row == row;
    char text[kKeyRowTextMax];
    if (bind.custom)
        std::snprintf(text, sizeof text, "%s", bind.keys);
    else
        std::snprintf(text, sizeof text, "%s (default)", bind.keys);
    char id[32];
    std::snprintf(id, sizeof id, "##key_%d", row);
    ImGui::PushID(row);
    md3::RowBegin(id, bind.button, capturing ? kKeyCaptureHint : text, false,
                  kKeyRowRightWidthPx * s, md3::kButtonHeightPx * s);
    ImGui::BeginDisabled(!menu_open);
    if (md3::Button("##bind", capturing ? "Cancel" : "Bind", md3::ButtonKind::Text)) {
        if (capturing)
            CaptureStop();
        else
            CaptureStart(row);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (md3::Button("##none", "None", md3::ButtonKind::Text)) imgui_keybind_set(bind.button, "None");
    ImGui::SameLine();
    ImGui::BeginDisabled(!bind.custom);
    if (md3::Button("##default", "Default", md3::ButtonKind::Text))
        imgui_keybind_set(bind.button, nullptr);
    ImGui::EndDisabled();
    md3::RowEnd();
    ImGui::PopID();
}

void DrawKeyBindings(bool menu_open) {
    if (EngineMenuKeyCaptureDrawn(g_capture, menu_open)) CaptureStop();
    ImguiKeybind keys[IMGUI_KEYBIND_COUNT];
    imgui_keybind_view(keys);
    CaptureTake(keys);
    md3::SectionTitle("Key bindings");
    md3::BodyText(kKeyBindingsHelp);
    if (g_capture.unnamed) md3::BodyText(kKeyNoName);
    const bool locked = keys[0].locked != 0;
    if (locked) md3::BodyText(kKeyboardLocked);
    ImGui::BeginDisabled(locked);
    for (int row = 0; row < IMGUI_KEYBIND_COUNT; ++row) DrawKeyRow(keys[row], row, menu_open);
    if (md3::Button("##key_reset_all", "Reset all to the defaults", md3::ButtonKind::Outlined))
        imgui_keybind_set(nullptr, nullptr);
    ImGui::EndDisabled();
}

void DrawPadMapRow(int control, int bit) {
    const md3::Theme& theme = md3::CurrentTheme();
    const float s = theme.scale;
    constexpr int kButtonCount = IMGUI_PADMAP_BUTTONS - IMGUI_PADMAP_NONE;
    const char* items[kButtonCount];
    int current = 0;
    for (int button = IMGUI_PADMAP_NONE; button < IMGUI_PADMAP_BUTTONS; ++button) {
        const int index = button - IMGUI_PADMAP_NONE;
        items[index] = imgui_padmap_button_name(button);
        if (button == bit) current = index;
    }
    char id[32];
    std::snprintf(id, sizeof id, "##pad_%d", control);
    ImGui::PushID(control);
    md3::RowBegin(id, imgui_padmap_control_name(control), nullptr, false, kPadMapRightWidthPx * s,
                  md3::kDropdownHeightPx * s);
    int chosen = current;
    if (md3::Combo("##button", &chosen, items, kButtonCount) && chosen != current)
        imgui_padmap_set(control, IMGUI_PADMAP_NONE + chosen);
    md3::RowEnd();
    ImGui::PopID();
}

void DrawPadMap() {
    ImguiPadMap map;
    imgui_padmap_view(&map);
    md3::SectionTitle("Button map");
    md3::BodyText(kButtonMapHelp);
    if (map.locked) md3::BodyText(kGamepadLocked);
    ImGui::BeginDisabled(map.locked != 0);
    for (int control = 0; control < IMGUI_PADMAP_CONTROLS; ++control) DrawPadMapRow(control, map.bit[control]);
    ImGui::BeginDisabled(!map.custom);
    if (md3::Button("##padmap_reset", "Reset to the console's layout", md3::ButtonKind::Outlined))
        imgui_padmap_reset();
    ImGui::EndDisabled();
    ImGui::EndDisabled();
}

} // namespace

void EngineMenuDrawKeyboardPage(bool menu_open) { DrawKeyBindings(menu_open); }

void EngineMenuDrawControllersPage(const char* model_page_name) {
    if (model_page_name == nullptr) return;
    EngineMenuDrawSettingsPage(model_page_name);
    DrawPadMap();
}

void EngineMenuInputFrameBegin() {
    if (EngineMenuKeyCaptureFrame(g_capture)) CaptureStop();
}

void EngineMenuInputReset() {
    if (g_capture.row != kEngineMenuKeyCaptureNone) imgui_keybind_capture(0);
    EngineMenuKeyCaptureReset(g_capture);
}

#endif // ENGINE_MENU_PURE_TEST
