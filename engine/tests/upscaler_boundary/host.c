/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The runtime's loader (gpu/vk/upscaler_module.c) loads five stand-ins (standin.cpp), no SDK, no GPU. Four are
   refused with nothing of their tables called (a call ends this process with 99). With the fifth, sentinels go
   through every call of the table, shutdown and the host's log. Arguments: the five full paths, in the order
   answers, other version, lacks a call, no table, no export. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "nx/gpu/vk/upscaler_module.h"
#include "sentinels.h"

/* what the module calls back: each lookup hands out a function, and each of those counts its calls */
static int g_instance_asked, g_device_asked, g_instance_called, g_device_called;
static void instance_function(void) { g_instance_called++; }
static void device_function(void) { g_device_called++; }
static PFN_vkVoidFunction instance_lookup(VkInstance instance, const char* name) {
    if (instance != SENTINEL_INSTANCE || strcmp(name, SENTINEL_LOOKUP_INSTANCE)) return NULL;
    g_instance_asked++;
    return instance_function;
}
static PFN_vkVoidFunction device_lookup(VkDevice device, const char* name) {
    if (device != SENTINEL_DEVICE || strcmp(name, SENTINEL_LOOKUP_DEVICE)) return NULL;
    g_device_asked++;
    return device_function;
}

/* the host's log for the module: the lines it was handed, the last one kept */
static int g_lines;
static char g_line[256];
static void host_log(const char* line) {
    g_lines++;
    snprintf(g_line, sizeof g_line, "%s", line);
}

/* the file's name without its folders: a name that is not a full path */
static const char* bare(const char* path) {
    const char* name = path;
    for (const char* p = path; *p; p++)
        if (*p == '/' || *p == '\\') name = p + 1;
    return name;
}

int main(int argc, char** argv) {
    int why = -1;
    if (argc != 6) {
        fprintf(stderr, "usage: %s <answers> <other version> <lacks a call> <no table> <no export>\n", argv[0]);
        return 2;
    }
    /* refused before anything is loaded: a name that is not a full path, also the good module's own */
    assert(!vk_upscaler_module_load(bare(argv[1]), host_log, &why) && why == SNX_MODULE_NOT_FULL_PATH);
    assert(!vk_upscaler_module_load("", host_log, &why) && why == SNX_MODULE_NOT_FULL_PATH);
    /* a full path to no file */
    {
        char missing[4096];
        snprintf(missing, sizeof missing, "%s.is-not-there", argv[1]);
        assert(!vk_upscaler_module_load(missing, host_log, &why) && why == SNX_MODULE_NOT_LOADED);
    }
    /* the four that are refused. For the two with a table, any call of it would have ended this process. */
    assert(!vk_upscaler_module_load(argv[2], host_log, &why) && why == SNX_MODULE_OTHER_VERSION);
    assert(!vk_upscaler_module_load(argv[3], host_log, &why) && why == SNX_MODULE_MISSING_CALL);
    assert(!vk_upscaler_module_load(argv[4], host_log, &why) && why == SNX_MODULE_NO_TABLE);
    assert(!vk_upscaler_module_load(argv[5], host_log, &why) && why == SNX_MODULE_NO_EXPORT);

    /* the one that answers: its number was read first by the loader, and is ours */
    const SnxUpscalerApi* api = vk_upscaler_module_load(argv[1], host_log, &why);
    assert(api && why == SNX_MODULE_OK && api->abi == SNX_UPSCALER_ABI && !strcmp(api->name, SENTINEL_NAME));
    assert(api->shutdown && !g_lines); /* the exports wrote nothing through the log */

    /* needs of the instance: the module's own arrays, read here */
    SnxUpscalerExtensions instance = {0};
    assert(api->instance_extensions(&instance) == SNX_UPSCALER_OK && instance.count == 2 && instance.names);
    assert(g_lines == 1 && !strcmp(g_line, SENTINEL_LOG_LINE)); /* the module's line, byte for byte */
    assert(!strcmp(instance.names[0], SENTINEL_INSTANCE_EXTENSION_A));
    assert(!strcmp(instance.names[1], SENTINEL_INSTANCE_EXTENSION_B));

    /* needs of the device: three handles in, a function of ours called back, and every member of the answer */
    SnxUpscalerDeviceNeeds needs = {0};
    assert(api->device_extensions(SENTINEL_PHYSICAL ? SENTINEL_INSTANCE : NULL, SENTINEL_PHYSICAL, instance_lookup,
                                  &needs) == SNX_UPSCALER_OK);
    assert(g_instance_asked == 1 && g_instance_called == 1);
    assert(needs.extensions.count == 1 && !strcmp(needs.extensions.names[0], SENTINEL_DEVICE_EXTENSION));
    assert(needs.features && !memcmp(needs.features, SENTINEL_FEATURES, sizeof SENTINEL_FEATURES));
    assert(needs.core == SENTINEL_CORE);
    /* the module tells a wrong handle by its place: the checks on its side are not empty */
    assert(api->device_extensions(SENTINEL_INSTANCE, (VkPhysicalDevice)SENTINEL_INSTANCE, instance_lookup,
                                  &needs) == 102);
    assert(api->device_extensions(SENTINEL_INSTANCE, SENTINEL_PHYSICAL, NULL, &needs) == 103);

    /* create: every member of the structure, both lookups, the traits back, a context */
    SnxUpscalerCreate make;
    SnxUpscalerTraits traits;
    SnxUpscaler* context = NULL;
    sentinel_create(&make, instance_lookup, device_lookup);
    memset(&traits, 0, sizeof traits);
    assert(api->create(&make, &traits, &context) == SNX_UPSCALER_OK && context);
    assert(g_device_asked == 1 && g_device_called == 1 && !sentinel_traits_wrong(&traits));
    /* each member of the create structure alone changed is told as that member, and no context is made */
    {
#define AT(member) offsetof(SnxUpscalerCreate, member)
        static const struct {
            size_t at;
            int member;
        } members[] = {{AT(sizes.render_w), 1}, {AT(sizes.render_h), 2}, {AT(sizes.image_w), 3}, {AT(sizes.image_h), 4},
                       {AT(sizes.output_w), 5}, {AT(sizes.output_h), 6}, {AT(instance), 7}, {AT(physical_device), 8},
                       {AT(device), 9}, {AT(queue), 12}, {AT(queue_family), 13}, {AT(flags), 14},
                       {AT(colour_format), 15}, {AT(depth_format), 16},
                       {AT(motion_format), 17}, {AT(output_format), 18}};
#undef AT
        for (size_t k = 0; k < sizeof members / sizeof members[0]; k++) {
            SnxUpscalerCreate changed = make;
            SnxUpscaler* none = context;
            ((unsigned char*)&changed)[members[k].at] ^= 0x40;
            assert(api->create(&changed, &traits, &none) == 200 + members[k].member && !none);
        }
        SnxUpscalerCreate changed = make;
        SnxUpscaler* none = context;
        changed.folder = "another folder";
        assert(api->create(&changed, &traits, &none) == 219 && !none);
    }

    /* prepare */
    assert(api->prepare(context, SENTINEL_CB) == SNX_UPSCALER_OK);
    assert(api->prepare(context, (VkCommandBuffer)SENTINEL_QUEUE) == 302);

    /* a frame: every member, the negative rectangle corner, the floats bit for bit, the infinite far plane */
    SnxUpscalerFrame frame;
    sentinel_frame(&frame);
    assert(api->evaluate(context, &frame) == SNX_UPSCALER_OK);
    /* each 4 bytes of the frame alone changed are told, as some member: no part of the frame goes unread */
    for (size_t at = 0; at < sizeof frame; at += 4) {
        SnxUpscalerFrame changed = frame;
        ((unsigned char*)&changed)[at + 1] ^= 0x10;
        const int answer = api->evaluate(context, &changed);
        assert(answer > 400 && answer <= 463);
    }
    {
        SnxUpscalerFrame changed = frame;
        changed.trace++;
        assert(api->evaluate(context, &changed) == 463); /* the last member, at the structure's end */
        changed = frame;
        changed.far_plane = 5000.0f;
        assert(api->evaluate(context, &changed) == 461);
        changed = frame;
        changed.render_x = 7; /* the sign */
        assert(api->evaluate(context, &changed) == 450);
        changed = frame;
        changed.output.h++;
        assert(api->evaluate(context, &changed) == 446);
    }

    /* shutdown is refused while a context is alive, then destroy, and the context knows it afterwards */
    assert(api->shutdown(SENTINEL_DEVICE) == 502);
    api->destroy(context);
    assert(api->prepare(context, SENTINEL_CB) == SENTINEL_DESTROYED);
    /* shutdown for another device is told, for this one it is done, and a second time is told */
    assert(api->shutdown((VkDevice)SENTINEL_QUEUE) == 501);
    assert(api->shutdown(SENTINEL_DEVICE) == SNX_UPSCALER_OK);
    assert(api->shutdown(SENTINEL_DEVICE) == 503);
    assert(g_instance_asked == 1 && g_instance_called == 1 && g_device_asked == 1 && g_device_called == 1);
    puts("upscaler_boundary: ok (four modules refused with nothing of them called, sentinels through every call "
         "of the fifth, its shutdown and its log)");
    return 0;
}
