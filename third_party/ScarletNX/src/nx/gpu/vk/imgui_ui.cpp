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
#include "nx/gpu/vk/imgui_settings.h"

namespace {
constexpr float BAR_TRIGGER_PX = 24.0f; /* the cursor this close to the top of the picture shows the bar */
bool g_bar_shown;
bool g_show_test;
bool g_show_debug;
bool g_show_input_settings;
bool g_show_display_settings;
constexpr const char* INPUT_SETTINGS_TITLE = "Controller&Keyboard"; /* the menu entries and the windows */
constexpr const char* DISPLAY_SETTINGS_TITLE = "Display";
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
    if (ImGui::BeginMenu("Settings")) {
        ImGui::MenuItem(INPUT_SETTINGS_TITLE, nullptr, &g_show_input_settings);
        ImGui::MenuItem(DISPLAY_SETTINGS_TITLE, nullptr, &g_show_display_settings);
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
    ImGui::BeginDisabled(); /* no setting behind these two yet */
    row("Scaling", "-");
    row("Anti-aliasing", "-");
    ImGui::EndDisabled();
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

/* ---- the Controller&Keyboard window: a page for each section of imgui_settings.h, a row for each setting ---- */
constexpr float SETTINGS_WIDTH = 460.0f, SETTINGS_HEIGHT = 420.0f, SETTINGS_MIN_WIDTH = 320.0f, SETTINGS_MIN_HEIGHT = 160.0f;
constexpr float SETTINGS_HELP_WIDTH = 28.0f; /* in font heights: the help tooltip wraps here */
constexpr double SLIDER_RANGE_MAX = 100000.0; /* a wider range is typed, not dragged */
constexpr int WORD_MAX = 64;
constexpr double PRESET_EPSILON = 1e-6; /* two values of a preset list that differ less are the same */
constexpr int ARROW_COARSE = 10;          /* Shift: this many steps for a press of an arrow */
constexpr float ARROW_FLOAT_STEPS = 200.0f; /* a float's step is its range over this */
constexpr float ARROW_FLOAT_FINE = 10.0f;   /* Ctrl: a step over this */

void setting_help(const ImguiSettingRow& r) {
    char note[160] = "";
    if (r.flags & IMGUI_SETTING_STARTUP) snprintf(note, sizeof note, "Applies when the game starts again.");
    if (r.flags & IMGUI_SETTING_LOCKED) snprintf(note, sizeof note, "Set on the command line for this run.");
    if (!r.help[0] && !note[0]) return;
    if (!ImGui::BeginItemTooltip()) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * SETTINGS_HELP_WIDTH);
    if (r.help[0]) ImGui::TextUnformatted(r.help);
    if (note[0]) ImGui::TextDisabled("%s", note);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

/* Left and Right while the pointer is over the item just drawn: -1, 0 or 1, a held key repeating. The game gets no key
   while the pointer is there (the arrows are its D-pad) */
int arrow_direction() {
    if (!ImGui::IsItemHovered()) return 0;
    ImGui::SetNextFrameWantCaptureKeyboard(true);
    return (int)ImGui::IsKeyPressed(ImGuiKey_RightArrow) - (int)ImGui::IsKeyPressed(ImGuiKey_LeftArrow);
}

/* one press of an arrow moves an int slider by the setting's step, ten of them with Shift, one unit with Ctrl */
int arrow_amount(int step) {
    const ImGuiIO& io = ImGui::GetIO();
    return io.KeyCtrl ? 1 : io.KeyShift ? step * ARROW_COARSE : step;
}

/* the three setting_* below return whether the row changed, and then the new value as the .ini writes it in out */
bool setting_choice(const ImguiSettingRow& r, char* out, size_t n) {
    char word[WORD_MAX];
    int count = (int)((r.hi - r.lo) / r.step) + 1;
    bool changed = false;
    imgui_settings_word(r.key, r.ivalue, word, sizeof word);
    bool open = ImGui::BeginCombo("##value", word);
    int dir = arrow_direction();
    if (dir) {
        int to = (r.ivalue - (int)r.lo) / r.step + dir;
        if (to >= 0 && to < count) {
            snprintf(out, n, "%d", (int)r.lo + to * r.step);
            changed = true;
        }
    }
    if (!open) return changed;
    for (int k = 0; k < count; ++k) {
        int v = (int)r.lo + k * r.step;
        imgui_settings_word(r.key, v, word, sizeof word);
        if (ImGui::Selectable(word, v == r.ivalue) && v != r.ivalue) {
            snprintf(out, n, "%d", v);
            changed = true;
        }
    }
    ImGui::EndCombo();
    return changed;
}

/* the row whose "Custom" entry was picked: its number field shows even when the value is one of the presets */
const char* g_custom_key = nullptr;

/* the number field under a preset choice that takes any value of lo to hi (the FPS limit) */
bool setting_custom_field(const ImguiSettingRow& r, double now, char* out, size_t n) {
    int v = now >= r.lo ? (int)now : (int)r.lo;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (!ImGui::InputInt("##custom", &v, 1, ARROW_COARSE, ImGuiInputTextFlags_EnterReturnsTrue)) return false;
    snprintf(out, n, "%d", v < (int)r.lo ? (int)r.lo : v > (int)r.hi ? (int)r.hi : v);
    return true;
}

/* a choice from the short list of imgui_settings_preset; a value that is not in it shows as custom, and Left and Right
   go to the nearest preset from it. A row with IMGUI_SETTING_CUSTOM also has the entry Custom and a number field */
bool setting_preset(const ImguiSettingRow& r, char* out, size_t n) {
    char value[WORD_MAX], label[WORD_MAX], shown[WORD_MAX];
    const double now = r.type == IMGUI_SETTING_FLOAT ? (double)r.fvalue : (double)r.ivalue;
    const bool custom_row = (r.flags & IMGUI_SETTING_CUSTOM) != 0;
    double number;
    int at = -1, below = -1, count = r.presets;
    for (int i = 0; i < count; ++i) {
        imgui_settings_preset(r.key, i, value, sizeof value, label, sizeof label, &number);
        if (fabs(number - now) < PRESET_EPSILON) at = i;
        if (number < now) below = i;
    }
    const bool custom_on = custom_row && (at < 0 || g_custom_key == r.key);
    if (at >= 0 && !custom_on)
        imgui_settings_preset(r.key, at, value, sizeof value, shown, sizeof shown, &number);
    else if (custom_row)
        snprintf(shown, sizeof shown, "Custom");
    else
        snprintf(shown, sizeof shown, "%g (custom%s)", now,
                 imgui_settings_fits(r.key, now) ? "" : ", too much GPU memory");
    bool open = ImGui::BeginCombo("##value", shown);
    int dir = arrow_direction();
    int to = at >= 0 ? at + dir : dir > 0 ? below + 1 : below;
    bool changed = false;
    if (dir && to >= 0 && to < count) {
        imgui_settings_preset(r.key, to, out, n, label, sizeof label, &number);
        g_custom_key = nullptr;
        changed = true;
    }
    if (open) {
        for (int i = 0; i < count; ++i) {
            imgui_settings_preset(r.key, i, value, sizeof value, label, sizeof label, &number);
            if (ImGui::Selectable(label, i == at && !custom_on) && (i != at || custom_on)) {
                snprintf(out, n, "%s", value);
                g_custom_key = nullptr;
                changed = true;
            }
        }
        if (custom_row && ImGui::Selectable("Custom", custom_on)) g_custom_key = r.key;
        ImGui::EndCombo();
    }
    if (custom_on) changed = setting_custom_field(r, now, out, n) || changed;
    return changed;
}

/* a text setting that is a choice of values (value=label words): the label of the one on, Left and Right move through
   the list */
bool setting_text_choice(const ImguiSettingRow& r, char* out, size_t n) {
    char value[WORD_MAX], label[WORD_MAX], shown[WORD_MAX];
    int at = -1, count = 0;
    while (imgui_settings_text_word(r.key, count, value, sizeof value, label, sizeof label)) {
        if (!strcmp(value, r.text)) at = count;
        ++count;
    }
    if (at >= 0)
        imgui_settings_text_word(r.key, at, value, sizeof value, shown, sizeof shown);
    else
        snprintf(shown, sizeof shown, "%s (custom)", r.text);
    bool open = ImGui::BeginCombo("##value", shown);
    int dir = arrow_direction();
    bool changed = false;
    if (dir && at >= 0 && at + dir >= 0 && at + dir < count) {
        imgui_settings_text_word(r.key, at + dir, out, n, label, sizeof label);
        changed = true;
    }
    if (!open) return changed;
    for (int i = 0; i < count; ++i) {
        imgui_settings_text_word(r.key, i, value, sizeof value, label, sizeof label);
        if (ImGui::Selectable(label, i == at) && i != at) {
            snprintf(out, n, "%s", value);
            changed = true;
        }
    }
    ImGui::EndCombo();
    return changed;
}

bool setting_number(const ImguiSettingRow& r, char* out, size_t n) {
    int v = r.ivalue;
    bool edited;
    if (r.hi - r.lo <= SLIDER_RANGE_MAX) {
        edited = ImGui::SliderInt("##value", &v, (int)r.lo, (int)r.hi, "%d", ImGuiSliderFlags_AlwaysClamp);
        int dir = arrow_direction();
        if (dir) {
            v += dir * arrow_amount(r.step);
            edited = true;
        }
    } else
        edited = ImGui::InputInt("##value", &v, r.step, r.step * ARROW_COARSE, ImGuiInputTextFlags_EnterReturnsTrue);
    if (!edited || v == r.ivalue) return false;
    snprintf(out, n, "%d", v < (int)r.lo ? (int)r.lo : v > (int)r.hi ? (int)r.hi : v);
    return true;
}

bool setting_float(const ImguiSettingRow& r, char* out, size_t n) {
    float v = r.fvalue, step = (float)(r.hi - r.lo) / ARROW_FLOAT_STEPS;
    const ImGuiIO& io = ImGui::GetIO();
    bool edited = ImGui::SliderFloat("##value", &v, (float)r.lo, (float)r.hi, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    int dir = arrow_direction();
    if (dir) {
        v += dir * (io.KeyCtrl ? step / ARROW_FLOAT_FINE : io.KeyShift ? step * ARROW_COARSE : step);
        v = v < (float)r.lo ? (float)r.lo : v > (float)r.hi ? (float)r.hi : v;
        edited = true;
    }
    if (!edited || v == r.fvalue) return false;
    snprintf(out, n, "%g", v);
    return true;
}

void setting_row(const ImguiSettingRow& r) {
    char value[WORD_MAX] = "";
    bool changed = false;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(r.name);
    setting_help(r);
    ImGui::TableNextColumn();
    ImGui::PushID(r.key);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::BeginDisabled((r.flags & IMGUI_SETTING_LOCKED) != 0);
    if (r.presets)
        changed = setting_preset(r, value, sizeof value);
    else if (r.type == IMGUI_SETTING_TEXT && r.has_words)
        changed = setting_text_choice(r, value, sizeof value);
    else if (r.type == IMGUI_SETTING_TEXT)
        ImGui::TextDisabled("Edit it in the .ini file");
    else if (r.type == IMGUI_SETTING_FLOAT)
        changed = setting_float(r, value, sizeof value);
    else if (r.has_words)
        changed = setting_choice(r, value, sizeof value);
    else
        changed = setting_number(r, value, sizeof value);
    ImGui::EndDisabled();
    if (r.flags & IMGUI_SETTING_RESTART)
        ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_CheckMark], "Saved. Start the game again to apply it.");
    ImGui::PopID();
    if (changed) imgui_settings_set(r.key, value);
}

/* ---- the key bindings of the Keyboard page: the row waiting for a key is g_capture_row ---- */
constexpr const char* KEYBIND_PAGE = "Keyboard";
constexpr int NO_ROW = -1;
constexpr int KEY_NAME_MAX = 48;
constexpr int CAPTURE_CANCELLED = -1; /* imgui_keybind_captured: Esc or a loss of the focus; -2 is an unnamed key */
int g_capture_row = NO_ROW;
bool g_capture_drawn;      /* the bindings showed this frame: a tab or a window that went away ends the capture */
bool g_capture_unnamed;    /* the last key pressed has no name that keyboard_map can hold */

void capture_stop() {
    if (g_capture_row == NO_ROW) return;
    imgui_keybind_capture(0);
    g_capture_row = NO_ROW;
}

/* the key that the capture took goes to the row that asked for it; a cancel or an unnamed key ends the wait */
void capture_take(const ImguiKeybind* v) {
    if (g_capture_row == NO_ROW) return;
    char key[KEY_NAME_MAX];
    int result = imgui_keybind_captured(key, sizeof key);
    if (!result) return;
    if (result > 0) imgui_keybind_set(v[g_capture_row].button, key);
    g_capture_unnamed = result < 0 && result != CAPTURE_CANCELLED;
    g_capture_row = NO_ROW;
}

void keybind_row(const ImguiKeybind& k, int i) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(k.button);
    ImGui::TableNextColumn();
    if (g_capture_row == i)
        ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_CheckMark], "Press a key (Esc cancels)");
    else if (k.custom)
        ImGui::TextUnformatted(k.keys);
    else
        ImGui::TextDisabled("%s", k.keys);
    ImGui::TableNextColumn();
    ImGui::PushID(i);
    if (ImGui::SmallButton(g_capture_row == i ? "Cancel" : "Bind")) {
        if (g_capture_row == i) {
            capture_stop();
        } else {
            g_capture_row = i;
            g_capture_unnamed = false;
            imgui_keybind_capture(1);
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) imgui_keybind_set(k.button, "None");
    ImGui::SameLine();
    ImGui::BeginDisabled(!k.custom);
    if (ImGui::SmallButton("Default")) imgui_keybind_set(k.button, nullptr);
    ImGui::EndDisabled();
    ImGui::PopID();
}

void keybinds() {
    ImguiKeybind v[IMGUI_KEYBIND_COUNT];
    imgui_keybind_view(v);
    capture_take(v);
    g_capture_drawn = true;
    ImGui::SeparatorText("Key bindings");
    ImGui::TextWrapped("Bind, then press the key. A key the list shows dimmed is the default of its button. "
                       "Esc cancels, and F1 to F12, Pause and Alt cannot be bound.");
    if (g_capture_unnamed)
        ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_CheckMark], "That key has no name that can be saved, so it cannot be bound.");
    bool locked = v[0].locked != 0;
    if (locked) ImGui::TextDisabled("keyboard_map is set on the command line for this run.");
    ImGui::BeginDisabled(locked);
    if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed);
        for (int i = 0; i < IMGUI_KEYBIND_COUNT; ++i) keybind_row(v[i], i);
        ImGui::EndTable();
    }
    if (ImGui::Button("Reset all to the defaults")) imgui_keybind_set(nullptr, nullptr);
    ImGui::EndDisabled();
}

/* ---- the button map of the Controllers page: a choice of the console's button for each control ---- */
constexpr const char* PADMAP_PAGE = "Controllers";
void padmap_row(int control, int bit) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(imgui_padmap_control_name(control));
    ImGui::TableNextColumn();
    ImGui::PushID(control);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##button", imgui_padmap_button_name(bit))) {
        for (int b = IMGUI_PADMAP_NONE; b < IMGUI_PADMAP_BUTTONS; ++b)
            if (ImGui::Selectable(imgui_padmap_button_name(b), b == bit) && b != bit) imgui_padmap_set(control, b);
        ImGui::EndCombo();
    }
    ImGui::PopID();
}

void padmap() {
    ImguiPadMap m;
    imgui_padmap_view(&m);
    ImGui::SeparatorText("Button map");
    ImGui::TextWrapped("The Switch button that each control of a gamepad sends. A button that another control sends "
                       "goes to this one's old control, so no button is lost. This is the map of every player "
                       "(a player's own gamepad_pN_map in the .ini goes over it).");
    if (m.locked) ImGui::TextDisabled("gamepad_map is set on the command line for this run.");
    ImGui::BeginDisabled(m.locked != 0);
    if (ImGui::BeginTable("##padmap", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Sends", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        for (int c = 0; c < IMGUI_PADMAP_CONTROLS; ++c) padmap_row(c, m.bit[c]);
        ImGui::EndTable();
    }
    ImGui::BeginDisabled(!m.custom);
    if (ImGui::Button("Reset to the console's layout")) imgui_padmap_reset();
    ImGui::EndDisabled();
    ImGui::EndDisabled();
}

void settings_page(int page) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Pointer over a slider or a choice: Left and Right change it. Shift moves ten steps, Ctrl one unit.");
    ImGui::PopStyleColor();
    if (ImGui::BeginTable("##rows", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImguiSettingRow r;
        for (int i = 0; imgui_settings_row(page, i, &r); ++i) setting_row(r);
        ImGui::EndTable();
    }
    if (!strcmp(imgui_settings_page_name(page), KEYBIND_PAGE)) keybinds();
    if (!strcmp(imgui_settings_page_name(page), PADMAP_PAGE)) padmap();
}

/* a window with the tabs of the pages that window shows; the second window opens where the first does not hide it */
void settings_window(const char* title, int window, bool* shown, float offset) {
    ImGui::SetNextWindowPos(ImVec2(80 + offset, 80 + offset), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(SETTINGS_WIDTH, SETTINGS_HEIGHT), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(SETTINGS_MIN_WIDTH, SETTINGS_MIN_HEIGHT), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::Begin(title, shown) && ImGui::BeginTabBar("##pages")) {
        for (int p = 0; p < imgui_settings_pages(); ++p)
            if (imgui_settings_page_window(p) == window && ImGui::BeginTabItem(imgui_settings_page_name(p))) {
                settings_page(p);
                ImGui::EndTabItem();
            }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void input_settings_window() {
    g_capture_drawn = false;
    settings_window(INPUT_SETTINGS_TITLE, IMGUI_WINDOW_INPUT, &g_show_input_settings, 0.0f);
    if (!g_capture_drawn) capture_stop();
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
    g_show_input_settings = false;
    g_show_display_settings = false;
    capture_stop();
    keyboard_reset();
}

bool imgui_ui_draw(ImguiFrame* f) {
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
    if (g_show_input_settings)
        input_settings_window();
    else
        capture_stop();
    if (g_show_display_settings) settings_window(DISPLAY_SETTINGS_TITLE, IMGUI_WINDOW_DISPLAY, &g_show_display_settings, 30.0f);
    if (g_show_test) test_window(f->dt);
    keyboard_window(f->kb);
    return g_bar_shown;
}
