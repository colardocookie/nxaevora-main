/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Only the callback fields the extracted code reads. No Vulkan loader or GPU is used. */
#include <stdint.h>
typedef uint32_t u32;
typedef int32_t i32;
typedef int VkResult;
typedef int VkBool32;
typedef unsigned VkDebugUtilsMessageSeverityFlagBitsEXT;
typedef unsigned VkDebugUtilsMessageTypeFlagsEXT;
typedef struct { const char* pLabelName; } VkDebugUtilsLabelEXT;
typedef struct { int objectType; const char* pObjectName; } VkDebugUtilsObjectNameInfoEXT;
typedef struct {
    const char* pMessageIdName;
    const char* pMessage;
    int32_t messageIdNumber;
    u32 cmdBufLabelCount, objectCount;
    const VkDebugUtilsLabelEXT* pCmdBufLabels;
    const VkDebugUtilsObjectNameInfoEXT* pObjects;
} VkDebugUtilsMessengerCallbackDataEXT;
#define VKAPI_ATTR
#define VKAPI_CALL
#define VK_FALSE 0
#define VK_SUCCESS 0
#define VK_OBJECT_TYPE_IMAGE 10
#define VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT 256
