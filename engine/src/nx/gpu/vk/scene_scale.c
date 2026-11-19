/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* What the game makes of SNX_ARCEUS_SCENE_SCALE (runtime/arceus_scene_scale.h), read only. The switch writes a
   number into the game's table and the game does the rest, so the proof is here: the used fraction the game puts
   into its scene block for the samplers, and the viewports its G-buffer draws use. One line per change. Without
   the switch nothing here runs. */
#include <stdlib.h>
#include <string.h>
#include "nx/gpu/gpu.h"
#include "nx/gpu/vk/vk.h"
#include "nx/runtime/arceus_scene_scale.h"
#include "nx/runtime/platform/log.h"

#define SCENE_BANK 3         /* the constant bank of the scene block */
#define SCENE_FRACTION 0x628 /* the used fraction of the targets, width then height, two floats */
#define SCENE_BYTES 0x630
#define SCENE_TARGETS 8      /* depth targets whose viewports are followed */
#define SCENE_LINES 96       /* lines at most, so a viewport that changes every frame does not fill the log */

static struct {
    int on; /* -1 until the switch is read */
    float asked;
    u32 lines;
    int have_fraction, stage;
    u32 fraction[2];
    struct {
        u64 gva;
        u32 w, h;
        float view[4];
    } target[SCENE_TARGETS];
    u32 targets;
} g_s = {.on = -1};

static int line_left(void) {
    if (g_s.lines > SCENE_LINES) return 0;
    if (g_s.lines++ < SCENE_LINES) return 1;
    LOG("arceus-scene-scale: %d lines written, no more follow\n", SCENE_LINES);
    return 0;
}

/* Every draw with three colour targets or more and a depth target is shown here: a G-buffer draw by its looks. */
void vk_scene_scale_note(const GpuChannel* ch, const Surf* zt, u32 colours) {
    if (g_s.on < 0) g_s.on = arceus_scene_scale_parse(getenv("SNX_ARCEUS_SCENE_SCALE"), &g_s.asked) ==
                             ARCEUS_SCENE_SCALE_USED;
    if (!g_s.on || colours < 3 || !zt) return;
    /* the used fraction, from the fragment stage's scene block or else the vertex stage's */
    static const int stages[2] = {ST_FS, ST_VS};
    for (int i = 0; i < 2; i++) {
        const u64 address = ch->cbind[stages[i]][SCENE_BANK];
        const u32 size = ch->cbind_size[stages[i]][SCENE_BANK];
        if (!address || size < SCENE_BYTES) continue;
        const u32 words[2] = {gmem_r32(address + SCENE_FRACTION), gmem_r32(address + SCENE_FRACTION + 4)};
        if (g_s.have_fraction && !memcmp(words, g_s.fraction, sizeof words) && g_s.stage == stages[i]) break;
        g_s.have_fraction = 1;
        g_s.stage = stages[i];
        memcpy(g_s.fraction, words, sizeof words);
        float used[2];
        memcpy(used, words, sizeof used);
        if (line_left())
            LOG("arceus-scene-scale: asked %g. The scene block (bank %d of the %s stage, at %llx, %x bytes) says "
                "the used fraction is %g by %g\n",
                (double)g_s.asked, SCENE_BANK, stages[i] == ST_FS ? "fragment" : "vertex",
                (unsigned long long)address, size, (double)used[0], (double)used[1]);
        break;
    }
    /* the draw's viewport in its depth target, followed per target */
    const VkViewport v = vk_render_viewport(ch->r3d);
    const float view[4] = {v.x, v.y, v.width, v.height};
    u32 t = 0;
    while (t < g_s.targets && g_s.target[t].gva != zt->gva) t++;
    if (t == SCENE_TARGETS) return;
    if (t < g_s.targets && g_s.target[t].w == zt->sw && g_s.target[t].h == zt->sh &&
        !memcmp(g_s.target[t].view, view, sizeof view))
        return;
    if (t == g_s.targets) g_s.targets++;
    g_s.target[t].gva = zt->gva;
    g_s.target[t].w = zt->sw;
    g_s.target[t].h = zt->sh;
    memcpy(g_s.target[t].view, view, sizeof view);
    if (line_left())
        LOG("arceus-scene-scale: asked %g. A G-buffer draw into the depth target %llx (%ux%u) has the viewport "
            "%g,%g %gx%g: %.4f by %.4f of the target\n",
            (double)g_s.asked, (unsigned long long)zt->gva, zt->sw, zt->sh, (double)v.x, (double)v.y,
            (double)v.width, (double)v.height, zt->sw ? (double)(v.width < 0 ? -v.width : v.width) / zt->sw : 0.0,
            zt->sh ? (double)(v.height < 0 ? -v.height : v.height) / zt->sh : 0.0);
}
