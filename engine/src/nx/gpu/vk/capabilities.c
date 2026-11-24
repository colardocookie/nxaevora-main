/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* --arceus-capabilities: whether this PC can run each upscaler of the Arceus graphics page, asked as the game asks at
   its start (upscaler_host.c), in a process of its own that the launcher runs. A Vulkan instance for each upscaler,
   no device, no window, no game, and nothing written but its lines. */
#include <stdio.h>
#include <string.h>
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/vk/upscaler.h"
#include "common/gpu/vk/vk_common.h"

/* the game's rule (device.c device_score) without a window to present to: Vulkan 1.3, the swapchain, a queue of
   graphics and compute, and a discrete card before an integrated one before any other */
static int rating(VkPhysicalDevice pd, const VkPhysicalDeviceProperties* p, void* user) {
    SnxVkExtensions e;
    (void)user;
    const int swapchain =
        snx_vk_device_extensions(pd, &e) && snx_vk_extensions_has(&e, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    snx_vk_extensions_free(&e);
    if (!swapchain || snx_vk_queue_family(pd, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, VK_NULL_HANDLE, 0) == ~0u)
        return -1;
    return p->deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
           : p->deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                                                     : 1;
}

/* an instance with names, and the card the rule picks with its name: 1, else 0 with why */
static int instance_make(const char* const* names, u32 count, SnxVkInstance* made, VkPhysicalDevice* card,
                         char* card_name, size_t size, char* why, size_t why_size) {
    const SnxVkInstanceInfo info = {.app_name = "ScarletNX capabilities",
                                    .extensions = count ? names : NULL,
                                    .extension_count = count,
                                    .portability = true};
    const VkResult r = snx_vk_instance_create(&info, made);
    if (r != VK_SUCCESS) {
        snprintf(why, why_size, "the Vulkan instance could not be made (%s)", snx_vk_result_str(r));
        return 0;
    }
    *card = snx_vk_best_device(made->instance, VK_API_VERSION_1_3, rating, NULL);
    if (!*card) {
        snprintf(why, why_size, "no Vulkan 1.3 graphics card was found");
        snx_vk_instance_free(made);
        return 0;
    }
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(*card, &p);
    snprintf(card_name, size, "%s", p.deviceName);
    return 1;
}

/* one upscaler's line: available on the card, or not and why */
static void check(const char* name, const char* plain_card) {
    char why[512], card[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    const char* names[32];
    u32 count = 0;
    SnxVkInstance made;
    VkPhysicalDevice phys;
    VkUpscalerCatch c;
    snprintf(card, sizeof card, "%s", plain_card);
    if (!vk_upscaler_present(name, why, sizeof why)) {
        printf("arceus-capabilities: %s: not available on %s: %s\n", name, card, why);
        return;
    }
    vk_upscaler_catch_begin(&c);
    const int asked = vk_upscaler_instance_ask(name, names, &count, 32);
    vk_upscaler_catch_end(&c, why, sizeof why);
    if (!asked) {
        printf("arceus-capabilities: %s: not available on %s: %s\n", name, card, why);
        return;
    }
    if (!instance_make(names, count, &made, &phys, card, sizeof card, why, sizeof why)) {
        printf("arceus-capabilities: %s: not available on %s: %s\n", name, card, why);
        return;
    }
    VkUpscalerDeviceAsk ask;
    vk_upscaler_catch_begin(&c);
    const int ok = vk_upscaler_device_ask(name, made.instance, phys, vkGetInstanceProcAddr, vk_upscaler_core_has(phys),
                                          &ask);
    vk_upscaler_catch_end(&c, why, sizeof why);
    snx_vk_instance_free(&made);
    if (ok)
        printf("arceus-capabilities: %s: available on %s\n", name, card);
    else
        printf("arceus-capabilities: %s: not available on %s: %s\n", name, card, why);
}

int vk_arceus_capabilities(void) {
    static const char* const upscalers[] = {"fsr", "dlss", "xess"};
    char why[256], card[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = "this PC";
    SnxVkInstance plain;
    VkPhysicalDevice phys;
    const int vulkan = volkInitialize() == VK_SUCCESS;
    /* the card's name for every line, from an instance with no upscaler's names */
    if (!vulkan)
        snprintf(why, sizeof why, "no Vulkan loader was found");
    else if (instance_make(NULL, 0, &plain, &phys, card, sizeof card, why, sizeof why))
        snx_vk_instance_free(&plain);
    else
        snprintf(card, sizeof card, "this PC");
    for (size_t i = 0; i < sizeof upscalers / sizeof *upscalers; i++) {
        if (!vulkan || !strcmp(card, "this PC"))
            printf("arceus-capabilities: %s: not available on %s: %s\n", upscalers[i], card, why);
        else
            check(upscalers[i], card);
    }
    fflush(stdout);
    return 0;
}
