/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The player's input for the Vita game, as the 3DS driver's (3ds/input/input.c) by where the buttons are: Z or Space
   Cross (south), X Circle (east), V Square (west), C Triangle (north), Q L, E R, Enter Start, Backspace Select, the
   arrows the D-pad, WASD the left stick and IJKL the right. The window's mouse is the touch screen: the left button
   touches the front panel, the right the rear one. A gamepad's buttons go by where they are; gamepad_swap_ab swaps
   Cross and Circle. The script (vita_script) is ms:hex presses, each held for vita_press_len ms, its bits 24 to 27 the
   left stick pushed right, left, up or down */
#include "vita/platform/input.h"
#include "common/utilities/common.h"
#include "common/utilities/script.h"
#include "snx_settings.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_PADS 4

static struct {
    SDL_Gamepad* pads[INPUT_PADS];
    SnxPress* script;
    size_t presses;
    double start;
    bool gamepads;
} g_input;

#define SCRIPT_BUTTONS     0x00ffffffu
#define SCRIPT_STICK_RIGHT 0x01000000u
#define SCRIPT_STICK_LEFT  0x02000000u
#define SCRIPT_STICK_UP    0x04000000u
#define SCRIPT_STICK_DOWN  0x08000000u
void vita_input_init(void) {
    g_input.start = snx_seconds();
    /* its buttons, and the left stick's directions in bits 24 to 27 */
    g_input.presses = snx_script_parse("scarletnx-vita", g_settings.vita_script,
                                       VITA_CTRL_BUTTONS | SCRIPT_STICK_RIGHT | SCRIPT_STICK_LEFT | SCRIPT_STICK_UP |
                                           SCRIPT_STICK_DOWN,
                                       &g_input.script);
    g_input.gamepads = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
}
void vita_input_event(const union SDL_Event* event) {
    if (event->type == SDL_EVENT_GAMEPAD_ADDED) {
        for (unsigned i = 0; i < INPUT_PADS; ++i)
            if (g_input.pads[i] && SDL_GetGamepadID(g_input.pads[i]) == event->gdevice.which) return;
        for (unsigned i = 0; i < INPUT_PADS; ++i)
            if (!g_input.pads[i]) {
                g_input.pads[i] = SDL_OpenGamepad(event->gdevice.which);
                break;
            }
    } else if (event->type == SDL_EVENT_GAMEPAD_REMOVED)
        for (unsigned i = 0; i < INPUT_PADS; ++i)
            if (g_input.pads[i] && SDL_GetGamepadID(g_input.pads[i]) == event->gdevice.which) {
                SDL_CloseGamepad(g_input.pads[i]);
                g_input.pads[i] = NULL;
            }
}
/* an axis, -1 to 1, to the stick's 0 to 255 */
static uint8_t stick(float v) {
    if (v < -1) v = -1;
    if (v > 1) v = 1;
    return (uint8_t)lrintf(127.5f + v * 127.5f);
}
static float axis(SDL_Gamepad* pad, SDL_GamepadAxis which) { return SDL_GetGamepadAxis(pad, which) / 32767.0f; }
void vita_input_read(VitaInput* input) {
    static const struct {
        SDL_Scancode key;
        uint32_t bit;
    } keys[] = {
        {SDL_SCANCODE_Z, VITA_CTRL_CROSS},       {SDL_SCANCODE_SPACE, VITA_CTRL_CROSS},
        {SDL_SCANCODE_X, VITA_CTRL_CIRCLE},      {SDL_SCANCODE_V, VITA_CTRL_SQUARE},
        {SDL_SCANCODE_C, VITA_CTRL_TRIANGLE},    {SDL_SCANCODE_Q, VITA_CTRL_LTRIGGER},
        {SDL_SCANCODE_E, VITA_CTRL_RTRIGGER},    {SDL_SCANCODE_RETURN, VITA_CTRL_START},
        {SDL_SCANCODE_BACKSPACE, VITA_CTRL_SELECT}, {SDL_SCANCODE_LEFT, VITA_CTRL_LEFT},
        {SDL_SCANCODE_UP, VITA_CTRL_UP},         {SDL_SCANCODE_RIGHT, VITA_CTRL_RIGHT},
        {SDL_SCANCODE_DOWN, VITA_CTRL_DOWN},
    };
    static const struct {
        SDL_GamepadButton button;
        uint32_t bit;
    } buttons[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, VITA_CTRL_CROSS},          {SDL_GAMEPAD_BUTTON_EAST, VITA_CTRL_CIRCLE},
        {SDL_GAMEPAD_BUTTON_WEST, VITA_CTRL_SQUARE},          {SDL_GAMEPAD_BUTTON_NORTH, VITA_CTRL_TRIANGLE},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, VITA_CTRL_LTRIGGER}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, VITA_CTRL_RTRIGGER},
        {SDL_GAMEPAD_BUTTON_START, VITA_CTRL_START},          {SDL_GAMEPAD_BUTTON_BACK, VITA_CTRL_SELECT},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, VITA_CTRL_LEFT},       {SDL_GAMEPAD_BUTTON_DPAD_UP, VITA_CTRL_UP},
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, VITA_CTRL_RIGHT},     {SDL_GAMEPAD_BUTTON_DPAD_DOWN, VITA_CTRL_DOWN},
    };
    float sticks[4] = {0, 0, 0, 0}; /* left x, y, right x, y; y down as the Vita has it */
    *input = (VitaInput){0};
    const bool* held = SDL_WasInit(SDL_INIT_VIDEO) ? SDL_GetKeyboardState(NULL) : NULL;
    if (held) {
        for (size_t i = 0; i < sizeof keys / sizeof *keys; ++i)
            if (held[keys[i].key]) input->buttons |= keys[i].bit;
        sticks[0] = (float)(held[SDL_SCANCODE_D] - held[SDL_SCANCODE_A]);
        sticks[1] = (float)(held[SDL_SCANCODE_S] - held[SDL_SCANCODE_W]);
        sticks[2] = (float)(held[SDL_SCANCODE_L] - held[SDL_SCANCODE_J]);
        sticks[3] = (float)(held[SDL_SCANCODE_K] - held[SDL_SCANCODE_I]);
        /* the mouse over the window: its place scaled to the panel's */
        float x, y;
        SDL_MouseButtonFlags mouse = SDL_GetMouseState(&x, &y);
        SDL_Window* window = SDL_GetMouseFocus();
        int w = 0, h = 0;
        if (window && SDL_GetWindowSize(window, &w, &h) && w > 0 && h > 0 && (mouse & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK))) {
            float u = fminf(fmaxf(x / (float)w, 0), 1), v = fminf(fmaxf(y / (float)h, 0), 1);
            if (mouse & SDL_BUTTON_LMASK) {
                input->front = true;
                input->front_x = (uint16_t)lrintf(u * 1919), input->front_y = (uint16_t)lrintf(v * 1087);
            }
            if (mouse & SDL_BUTTON_RMASK) {
                input->back = true;
                input->back_x = (uint16_t)lrintf(u * 1919), input->back_y = (uint16_t)lrintf(v * 889);
            }
        }
    }
    float dead = (float)g_settings.stick_dead_zone / 100.0f;
    for (unsigned p = 0; p < INPUT_PADS; ++p) {
        SDL_Gamepad* pad = g_input.pads[p];
        if (!pad) continue;
        for (size_t i = 0; i < sizeof buttons / sizeof *buttons; ++i)
            if (SDL_GetGamepadButton(pad, buttons[i].button)) {
                uint32_t bit = buttons[i].bit;
                if (g_settings.swap_a_b && (bit == VITA_CTRL_CROSS || bit == VITA_CTRL_CIRCLE))
                    bit ^= VITA_CTRL_CROSS | VITA_CTRL_CIRCLE;
                input->buttons |= bit;
            }
        SDL_GamepadAxis axes[4] = {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX,
                                   SDL_GAMEPAD_AXIS_RIGHTY};
        for (unsigned s = 0; s < 2; ++s) {
            float x = axis(pad, axes[s * 2]), y = axis(pad, axes[s * 2 + 1]), length = sqrtf(x * x + y * y);
            if (length <= dead) continue;
            float scale = (fminf(length, 1) - dead) / (1 - dead) / length;
            sticks[s * 2] = x * scale, sticks[s * 2 + 1] = y * scale;
        }
    }
    /* the script's presses: buttons, and the left stick pushed all the way by bits 24 to 27 (right, left, up, down) */
    if (g_input.presses) {
        double ms = (snx_seconds() - g_input.start) * 1000.0;
        uint32_t mask = snx_script_held(g_input.script, g_input.presses, ms, (uint32_t)g_settings.vita_press_length);
        input->buttons |= mask & SCRIPT_BUTTONS;
        if (mask & SCRIPT_STICK_RIGHT) sticks[0] = 1;
        if (mask & SCRIPT_STICK_LEFT) sticks[0] = -1;
        if (mask & SCRIPT_STICK_UP) sticks[1] = -1;
        if (mask & SCRIPT_STICK_DOWN) sticks[1] = 1;
    }
    input->lx = stick(sticks[0]), input->ly = stick(sticks[1]);
    input->rx = stick(sticks[2]), input->ry = stick(sticks[3]);
}
void vita_input_free(void) {
    for (unsigned i = 0; i < INPUT_PADS; ++i)
        if (g_input.pads[i]) SDL_CloseGamepad(g_input.pads[i]);
    if (g_input.gamepads) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    free(g_input.script);
    memset(&g_input, 0, sizeof g_input);
}
