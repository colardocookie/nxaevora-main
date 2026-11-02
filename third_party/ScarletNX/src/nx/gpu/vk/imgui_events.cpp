/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/vk/imgui_events.h"
#include <cfloat>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include "imgui.h"
#include "nx/gpu/vk/imgui_input.h"

namespace {
constexpr float NO_MOUSE_BELOW = -1.0e8f; /* a position under this is "no mouse" (imgui_input.h) */
ImVec2 g_ime_pos;
float g_ime_height;

/* called at the end of a frame, when a text field is active: where its cursor is */
void set_ime_data(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData* data) {
    g_ime_pos = data->InputPos;
    g_ime_height = data->InputLineHeight;
}

ImGuiKey map_key(int key) {
    if (key >= SDLK_A && key <= SDLK_Z) return (ImGuiKey)(ImGuiKey_A + (key - SDLK_A));
    if (key >= SDLK_0 && key <= SDLK_9) return (ImGuiKey)(ImGuiKey_0 + (key - SDLK_0));
    if (key >= SDLK_F1 && key <= SDLK_F12) return (ImGuiKey)(ImGuiKey_F1 + (key - SDLK_F1));
    switch (key) {
        case SDLK_TAB: return ImGuiKey_Tab;
        case SDLK_LEFT: return ImGuiKey_LeftArrow;
        case SDLK_RIGHT: return ImGuiKey_RightArrow;
        case SDLK_UP: return ImGuiKey_UpArrow;
        case SDLK_DOWN: return ImGuiKey_DownArrow;
        case SDLK_PAGEUP: return ImGuiKey_PageUp;
        case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
        case SDLK_HOME: return ImGuiKey_Home;
        case SDLK_END: return ImGuiKey_End;
        case SDLK_INSERT: return ImGuiKey_Insert;
        case SDLK_DELETE: return ImGuiKey_Delete;
        case SDLK_BACKSPACE: return ImGuiKey_Backspace;
        case SDLK_SPACE: return ImGuiKey_Space;
        case SDLK_RETURN: return ImGuiKey_Enter;
        case SDLK_ESCAPE: return ImGuiKey_Escape;
        case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
        default: return ImGuiKey_None;
    }
}

int map_button(int sdl_button) {
    switch (sdl_button) {
        case SDL_BUTTON_LEFT: return 0;
        case SDL_BUTTON_RIGHT: return 1;
        case SDL_BUTTON_MIDDLE: return 2;
        default: return -1;
    }
}

void feed_key(ImGuiIO& io, const ImguiEvent& e) {
    io.AddKeyEvent(ImGuiMod_Ctrl, (e.mods & SDL_KMOD_CTRL) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (e.mods & SDL_KMOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (e.mods & SDL_KMOD_ALT) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (e.mods & SDL_KMOD_GUI) != 0);
    ImGuiKey key = map_key(e.key);
    if (key != ImGuiKey_None) io.AddKeyEvent(key, e.down != 0);
}
} // namespace

void imgui_events_init(void) { ImGui::GetPlatformIO().Platform_SetImeDataFn = set_ime_data; }

void imgui_events_feed(void) {
    ImGuiIO& io = ImGui::GetIO();
    ImguiEvent e;
    while (imgui_input_pop(&e)) {
        switch (e.type) {
            case IMGUI_EV_MOUSE_POS:
                if (e.x < NO_MOUSE_BELOW)
                    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                else
                    io.AddMousePosEvent(e.x, e.y);
                break;
            case IMGUI_EV_MOUSE_BUTTON:
                if (map_button(e.button) >= 0) io.AddMouseButtonEvent(map_button(e.button), e.down != 0);
                break;
            case IMGUI_EV_WHEEL: io.AddMouseWheelEvent(e.x, e.y); break;
            case IMGUI_EV_KEY: feed_key(io, e); break;
            case IMGUI_EV_TEXT: io.AddInputCharactersUTF8(e.text); break;
            case IMGUI_EV_FOCUS: io.AddFocusEvent(false); break;
        }
    }
}

void imgui_events_publish(bool menu_visible) {
    ImGuiIO& io = ImGui::GetIO();
    ImguiWants w;
    w.mouse = io.WantCaptureMouse;
    w.keyboard = io.WantCaptureKeyboard;
    w.text = io.WantTextInput;
    w.release = io.WantCaptureMouse || menu_visible;
    w.redraw = ImGui::GetDrawData()->TotalVtxCount > 0;
    w.ime_x = (int)g_ime_pos.x;
    w.ime_y = (int)g_ime_pos.y;
    w.ime_h = (int)g_ime_height;
    imgui_input_publish(&w);
}
