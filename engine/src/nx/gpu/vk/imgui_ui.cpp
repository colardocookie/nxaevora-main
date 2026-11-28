/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/vk/imgui_text.h"
#include "nx/gpu/vk/imgui_ui.h"
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include "imgui.h"
#include "nx/gpu/vk/debug_stats.h"
#include "nx/gpu/vk/engine_menu.h"
#include "nx/gpu/vk/engine_menu_input.h"

namespace {
constexpr float BAR_TRIGGER_PX = 24.0f; /* the cursor this close to the top of the picture shows the bar */
bool g_bar_shown;
bool g_show_test;
bool g_show_debug;
constexpr float DEBUG_WIDTH = 380.0f, DEBUG_HEIGHT = 520.0f, DEBUG_MIN_WIDTH = 300.0f, DEBUG_MIN_HEIGHT = 200.0f;
constexpr float GRAPH_HEIGHT = 56.0f, GRAPH_TOP_MS = 50.0f;
constexpr float MB_PER_GB = 1024.0f;
int g_open_test_was = -1;

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
    if (ImGui::BeginMenu("Debug")) {
        ImGui::MenuItem("Debug window", nullptr, &g_show_debug);
        ImGui::MenuItem("Test window", nullptr, &g_show_test);
        ImGui::EndMenu();
    }
}

void test_window(float dt) {
    ImGui::SetNextWindowPos(ImVec2(40, 60), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Dear ImGui test", &g_show_test)) {
        ImGui::End();
        return;
    }
    static bool checked = true;
    static float value = 0.6f;
    ImGui::Text("Backend check: Roboto and Noto Sans JP");
    ImGui::Text("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE8\xA1\xA8\xE7\xA4\xBA\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88"); /* Japanese text, UTF-8 */
    ImGui::Text("%.1f FPS, %.2f ms", ImGui::GetIO().Framerate, dt * 1000.0f);
    ImGui::Checkbox("Theme check", &checked);
    ImGui::SliderFloat("Value", &value, 0.0f, 1.0f);
    ImGui::Button("Button");
    ImGui::End();
}

/* a value that the machine did not tell is -1 and shows as ? */
void number(char* out, size_t n, const char* format, float v, bool tenths = false) {
    if (v < 0)
        snprintf(out, n, "?");
    else if (tenths)
        snprintf(out, n, format, v);
    else
        snprintf(out, n, format, (int)(v + 0.5f));
}

void row(const char* label, const char* value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value);
}

void hardware_lines(const DebugStats& s) {
    char a[24], b[24], c[24], line[96];
    number(a, sizeof a, "%d%%", (float)s.gpu_load);
    number(b, sizeof b, "%d\xC2\xB0" "C", (float)s.gpu_temp);
    number(c, sizeof c, "%dW", (float)s.gpu_power);
    snprintf(line, sizeof line, "GPU:   %s   %s   %s", a, b, c);
    ImGui::TextUnformatted(line);
    char used[24], total[24], clock[24];
    number(used, sizeof used, "%.1fGB", s.vram_used_mb < 0 ? -1.0f : s.vram_used_mb / MB_PER_GB, true);
    number(total, sizeof total, "%.1fGB", s.vram_total_mb < 0 ? -1.0f : s.vram_total_mb / MB_PER_GB, true);
    number(clock, sizeof clock, "%dMHz", (float)s.vram_clock_mhz);
    snprintf(line, sizeof line, "VRAM:  %s  /%s  %s", used, total, clock);
    ImGui::TextUnformatted(line);
    number(a, sizeof a, "%d%%", (float)s.cpu_load);
    number(b, sizeof b, "%d\xC2\xB0" "C", (float)s.cpu_temp);
    number(c, sizeof c, "%dW", (float)s.cpu_power);
    snprintf(line, sizeof line, "CPU:   %s   %s   %s", a, b, c);
    ImGui::TextUnformatted(line);
    char game[24];
    number(used, sizeof used, "%.1fGB", s.ram_used_mb < 0 ? -1.0f : s.ram_used_mb / MB_PER_GB, true);
    number(total, sizeof total, "%.1fGB", s.ram_total_mb < 0 ? -1.0f : s.ram_total_mb / MB_PER_GB, true);
    number(game, sizeof game, "%.1fGB", s.game_ram_mb < 0 ? -1.0f : s.game_ram_mb / MB_PER_GB, true);
    snprintf(line, sizeof line, "RAM:   %s  /%s  game %s", used, total, game);
    ImGui::TextUnformatted(line);
}

void frame_lines(const DebugStats& s) {
    char fps[24], last[24], max[24], line[96];
    number(fps, sizeof fps, "%.0fFPS", s.fps, true);
    number(last, sizeof last, "%.1fms", s.frame_last_ms, true);
    number(max, sizeof max, "%.1fms", s.frame_max_ms, true);
    snprintf(line, sizeof line, "%s   %s   max %s", fps, last, max);
    ImGui::TextUnformatted(line);
    ImGui::PlotLines("##frames", s.frame_ms, s.frame_count, 0, nullptr, 0.0f, GRAPH_TOP_MS, ImVec2(-1, GRAPH_HEIGHT));
}

void cost_lines(const DebugStats& s) {
    char v[32];
    if (!ImGui::BeginTable("costs", 2, ImGuiTableFlags_SizingStretchProp)) return;
    number(v, sizeof v, "%.1f ms", s.prep_ms, true);
    row("CPU frame prep:", v);
    number(v, sizeof v, "%.1f ms", s.cpu_ms, true);
    row("CPU busy per frame:", v);
    number(v, sizeof v, "%.1f ms", s.pipeline_wait_ms, true);
    row("Pipeline waits:", v);
    number(v, sizeof v, "%.1f per frame", s.texture_uploads, true);
    row("Texture uploads:", v);
    ImGui::EndTable();
}

void display_lines(const ImguiFrame& f) {
    char v[48];
    if (!ImGui::BeginTable("display", 2, ImGuiTableFlags_SizingStretchProp)) return;
    snprintf(v, sizeof v, "%d x %d", f.window_w, f.window_h);
    row("Window Size", v);
    if (f.game_w > 0)
        snprintf(v, sizeof v, "%d x %d", f.game_w, f.game_h);
    else
        snprintf(v, sizeof v, "?");
    row("Game resolution", v);
    row("Scaling", f.scaling_filter == 1 ? "Bicubic" : "Bilinear");
    row("Anti-aliasing", f.anti_aliasing == 2 ? "SMAA" : f.anti_aliasing == 1 ? "FXAA" : "Off");
    snprintf(v, sizeof v, "%s", f.fps_limit);
    row("Frame limit", v);
    row("V-sync", f.vsync == 2 ? "On (Mailbox)" : f.vsync ? "On (FIFO)" : "Off");
    ImGui::EndTable();
}

void debug_window(const ImguiFrame& f) {
    DebugStats stats;
    ImGui::SetNextWindowPos(ImVec2(40, 60), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(DEBUG_WIDTH, DEBUG_HEIGHT), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(DEBUG_MIN_WIDTH, DEBUG_MIN_HEIGHT), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin("Debug", &g_show_debug)) {
        debug_stats_hardware_demand(nullptr);
        ImGui::End();
        return;
    }
    debug_stats_hardware_demand(f.gpu_uuid);
    debug_stats_get(&stats);
    hardware_lines(stats);
    frame_lines(stats);
    cost_lines(stats);
    ImGui::Spacing();
    display_lines(f);
    ImGui::End();
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
    g_open_test_was = -1;
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
    if (f->open_test != g_open_test_was) {
        g_open_test_was = f->open_test;
        g_show_test = f->open_test != 0;
    }
    bool bar_was_shown = g_bar_shown;
    g_bar_shown = false;
    if (bar_wanted(bar_was_shown) && ImGui::BeginMainMenuBar()) {
        g_bar_shown = true;
        menu_bar();
        ImGui::EndMainMenuBar();
    }
    if (g_show_debug)
        debug_window(*f);
    else
        debug_stats_hardware_demand(nullptr);
    if (g_show_test) test_window(f->dt);
    keyboard_window(f->kb);
    return g_bar_shown;
}
