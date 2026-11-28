/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/vk/imgui_text.h"
#include "nx/gpu/vk/imgui_ui.h"
#include <cfloat>
#include <cstring>
#include "imgui.h"
#include "nx/gpu/vk/debug_stats.h"
#include "nx/gpu/vk/engine_menu.h"
#include "nx/gpu/vk/engine_menu_input.h"

namespace {
constexpr float BAR_TRIGGER_PX = 24.0f; /* the cursor this close to the top of the picture shows the bar */
bool g_bar_shown;

bool menu_open_anywhere() {
    return ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

/* shows from the top edge. It stays while the cursor is on it or one of its menus is open, but only if it showed in the
   last frame: a popup of another window (a combo) must not bring it up, as its window would take the focus and close
   the popup */
bool bar_wanted(bool was_shown) {
    const ImGuiIO& io = ImGui::GetIO();
    bool has_mouse = io.MousePos.x > -FLT_MAX / 2 && io.MousePos.y >= 0;
    bool at_top = has_mouse && io.MousePos.y < BAR_TRIGGER_PX;
    bool on_bar = was_shown && has_mouse && io.MousePos.y < ImGui::GetFrameHeight();
    return at_top || on_bar || (was_shown && menu_open_anywhere());
}

void unavailable(const char* what) {
    ImGui::BeginDisabled();
    ImGui::MenuItem(what);
    ImGui::EndDisabled();
}

void menu_bar() {
    if (ImGui::BeginMenu("About")) {
        ImGui::TextUnformatted("NXAevora");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Mods")) {
        unavailable("Not available yet");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Cheats")) {
        unavailable("Not available yet");
        ImGui::EndMenu();
    }
}

/* ---- the game's software keyboard (the player's name): the box that overlay.c keeps the state of ---- */
constexpr float KEYBOARD_FIELD_WIDTH = 420.0f;
constexpr unsigned UTF16_PAIR_MIN = 0x10000; /* a character from here on is two UTF-16 units, as the game counts them */

struct KeyboardLimit {
    unsigned max;
    bool numbers;
};

/* ImGui hands the filter the character only (not the line), so it refuses controls and, for a number pad, all but
   digits. The line's most is kept after each edit, which does have the line */
int keyboard_callback(ImGuiInputTextCallbackData* data) {
    const KeyboardLimit* limit = (const KeyboardLimit*)data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
        unsigned c = (unsigned)data->EventChar;
        if (c < 0x20 || c == 0x7F) return 1;
        return limit->numbers && (c < '0' || c > '9') ? 1 : 0;
    }
    size_t keep = imgui_text_fit_units(data->Buf, (size_t)data->BufTextLen, limit->max);
    if (keep < (size_t)data->BufTextLen) data->DeleteChars((int)keep, data->BufTextLen - (int)keep);
    return 0;
}

/* the window's own state: the line as typed, the request it is for, whether Enter was refused */
struct KeyboardState {
    char line[IMGUI_KB_TEXT];
    unsigned last_ticket;
    bool was_shown, refused, refocus;
} g_kb;

void keyboard_reset() { g_kb = KeyboardState(); }

void keyboard_window(ImguiKeyboard& k) {
    char* line = g_kb.line;
    bool& was_shown = g_kb.was_shown;
    bool& refused = g_kb.refused;
    bool& refocus = g_kb.refocus;
    unsigned& last_ticket = g_kb.last_ticket;
    k.text_changed = 0;
    k.answer = IMGUI_KB_NONE;
    if (!k.shown) {
        was_shown = false;
        return;
    }
    bool fresh = !was_shown || k.ticket != last_ticket;
    if (fresh) {
        memcpy(line, k.text, sizeof g_kb.line);
        last_ticket = k.ticket;
        was_shown = true;
        refused = false;
        refocus = false;
    }
    const ImGuiViewport* view = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(view->Size.x * 0.5f, view->Size.y * 0.4f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (fresh) ImGui::SetNextWindowFocus();
    ImGui::Begin("Keyboard", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    ImGui::PushTextWrapPos(KEYBOARD_FIELD_WIDTH);
    if (k.header[0]) ImGui::TextUnformatted(k.header);
    if (k.sub[0]) ImGui::TextDisabled("%s", k.sub);
    if (k.guide[0]) ImGui::TextDisabled("%s", k.guide);
    ImGui::PopTextWrapPos();
    KeyboardLimit limit = {k.max, k.numbers != 0};
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter |
                                ImGuiInputTextFlags_CallbackEdit;
    if (k.password) flags |= ImGuiInputTextFlags_Password;
    ImGui::SetNextItemWidth(KEYBOARD_FIELD_WIDTH);
    ImGui::PushID((int)k.ticket); /* a new request is a new field: it starts from its own text, not an old field's */
    if (fresh || refocus) ImGui::SetKeyboardFocusHere();
    refocus = false;
    const bool pad_enter = k.pad_answer == IMGUI_KB_ENTER, pad_cancel = k.pad_answer == IMGUI_KB_CANCEL;
    bool entered = ImGui::InputText("##line", line, sizeof g_kb.line, flags, keyboard_callback, &limit);
    unsigned units = imgui_text_utf16_units(line, strlen(line));
    bool enough = units >= k.min;
    bool ok = ImGui::Button(k.ok[0] ? k.ok : "OK");
    ImGui::SameLine();
    bool cancel = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || pad_cancel;
    ImGui::PopID();
    if ((entered || ok || pad_enter) && !enough) {
        refused = true;
        refocus = true; /* Enter ended the field: the next frame gives it back, so typing goes on */
    }
    if (enough) refused = false;
    ImGui::TextDisabled("%u of %u characters%s. A: %s, B: Cancel.", units, k.max, k.min ? ", a minimum is needed" : "",
                        k.ok[0] ? k.ok : "OK");
    if (refused) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%u more character(s) needed, %u at least.", k.min - units, k.min);
    ImGui::End();
    if (strcmp(line, k.text) != 0) {
        memcpy(k.text, line, sizeof k.text);
        k.text_changed = 1;
    }
    if (cancel)
        k.answer = IMGUI_KB_CANCEL;
    else if ((entered || ok || pad_enter) && enough)
        k.answer = IMGUI_KB_ENTER;
}
} // namespace

void imgui_ui_reset(void) {
    g_bar_shown = false;
    EngineMenuInputReset();
    keyboard_reset();
    engine_menu_reset();
}

bool imgui_ui_draw(ImguiFrame* f) {
    EngineMenuInputFrameBegin();
    /* the menu is modal: only its panel draws, and the game's windows come back when it closes */
    if (engine_menu_visible()) {
        engine_menu_draw(f);
        return true;
    }
    debug_stats_hardware_demand(nullptr);
    bool bar_was_shown = g_bar_shown;
    g_bar_shown = false;
    if (bar_wanted(bar_was_shown) && ImGui::BeginMainMenuBar()) {
        g_bar_shown = true;
        menu_bar();
        ImGui::EndMainMenuBar();
    }
    keyboard_window(f->kb);
    return g_bar_shown;
}
