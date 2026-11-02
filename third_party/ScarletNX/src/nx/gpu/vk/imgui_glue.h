/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Dear ImGui's windows over the game. imgui_glue.cpp is the only C++ here: it holds the ImGui context and its Vulkan
   backend, and takes and gives plain Vulkan handles. imgui.c decides when a frame is drawn and records it (the lock
   and the recording thread are described there). Every function is called under vk_lock, so the context has one
   user at a time */
#ifndef SNX_IMGUI_GLUE_H
#define SNX_IMGUI_GLUE_H
#include <stdint.h>
#include "volk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    VkInstance instance;
    VkPhysicalDevice phys;
    VkDevice dev;
    uint32_t family;
    VkQueue queue;
    uint32_t image_count; /* the buffers of the backend: at least the frames in flight */
    VkFormat format;      /* of the images drawn on */
    const char* latin_font;    /* a TTF file, NULL for ImGui's own font */
    const char* japanese_font; /* merged into the first, NULL for none */
    float font_px;
} ImguiGlueInit;

int imgui_glue_init(const ImguiGlueInit* in); /* 1 when ready */
void imgui_glue_shutdown(void);               /* the device is idle */
int imgui_glue_ready(void);
int imgui_glue_vk_failed(void); /* the backend got a Vulkan error since init: the caller shuts it down */
/* one frame for a picture w x h, dt seconds after the last one. open_test is the test window's first state. Returns 1
   when there is something to draw */
/* the game's keyboard box, as the window shows it (overlay.h's OverlayKeyboardView) and what the window answers. The
   sizes are those of the box's texts */
enum { IMGUI_KB_HEADER = 193, IMGUI_KB_SUB = 385, IMGUI_KB_GUIDE = 769, IMGUI_KB_OK = 25, IMGUI_KB_TEXT = 3073 };
enum { IMGUI_KB_NONE, IMGUI_KB_ENTER, IMGUI_KB_CANCEL };
typedef struct {
    int shown;
    unsigned ticket, units, min, max;
    int password, numbers;
    char header[IMGUI_KB_HEADER], sub[IMGUI_KB_SUB], guide[IMGUI_KB_GUIDE], ok[IMGUI_KB_OK];
    char text[IMGUI_KB_TEXT]; /* in: the box's line. out: the window's, when text_changed */
    int pad_answer;           /* in: a controller's A or B (IMGUI_KB_ENTER or IMGUI_KB_CANCEL), not yet answered */
    int text_changed, answer; /* out: the line to take, and IMGUI_KB_ENTER or IMGUI_KB_CANCEL */
} ImguiKeyboard;
typedef struct {
    float w, h, dt;
    int open_test;
    /* what the Debug window reports of the display and the game */
    int window_w, window_h;
    int game_w, game_h;     /* the game's picture, as the renderer makes it: the console's size times the render scale */
    int vsync;
    const char* fps_limit;  /* the setting's text: game, 0 or a number */
    const unsigned char* gpu_uuid; /* the rendering device: 16 bytes */
    ImguiKeyboard kb;
} ImguiFrame;
int imgui_glue_frame(ImguiFrame* f); /* the keyboard's answer goes in f->kb */
void imgui_glue_unwanted(void); /* the windows are off: they want no input */
/* the frame's draw data into cb, inside a rendering scope on the picture. A new texture is uploaded first, on the
   queue, which the caller keeps idle for it (vk_rec_sync) */
void imgui_glue_record(VkCommandBuffer cb);

#ifdef __cplusplus
}
#endif
#endif
