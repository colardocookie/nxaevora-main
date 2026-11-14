// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// DLSS's adapter (src/nx/gpu/vk/upscaler.h), a module of its own built against the builder's NGX SDK. This build is
// the discovery step: it asks NGX which instance and device extensions the feature needs and prints every name. No
// name is classified yet, so none is handed back: the instance is made without NGX's names, the device question
// always refuses, no device is made for NGX, and nothing of NGX is started. create, prepare and evaluate refuse.
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <type_traits>
#include <vulkan/vulkan_core.h>
#include "nx/gpu/vk/upscaler.h"
#include "nvsdk_ngx_vk.h"

namespace {
// ours, fixed, GUID-like as NGX asks of a project id (CUSTOM engine)
const char kProjectId[] = "0f297030-0b63-4170-ac30-3c0d1cb0de45";
const unsigned kNgxPrinted = 256; // NGX's messages printed in full, the rest counted
const uint32_t kNamesMost = 32;

SnxUpscalerLog g_log;
SRWLOCK g_lock = SRWLOCK_INIT; // every call into NGX, and the module's state
volatile LONG g_ngx_messages;

// one line through the host's log, without a line break at its end, and one cut to fit says so at its end
void say(const char* format, ...) {
    static const char cut[] = " (cut)";
    char line[1024];
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (n < 0 || !g_log) return;
    if ((size_t)n >= sizeof line) memcpy(line + sizeof line - sizeof cut, cut, sizeof cut);
    size_t end = strlen(line);
    while (end && (line[end - 1] == '\n' || line[end - 1] == '\r')) line[--end] = 0;
    g_log(line);
}

// NGX's messages: from any thread, at once, and with no lock of the module taken. One longer than a line keeps its
// first bytes up to a whole character and says how many of how many it shows.
void NVSDK_CONV ngx_log(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature) {
    const size_t room = 900;
    const LONG n = InterlockedIncrement(&g_ngx_messages);
    if ((unsigned)n > kNgxPrinted) {
        if ((unsigned)n == kNgxPrinted + 1)
            say("arceus-upscaler: dlss: NGX has said %u messages, the ones after them are counted and not printed",
                kNgxPrinted);
        return;
    }
    const char* text = message ? message : "";
    const size_t length = strlen(text);
    size_t shown = length;
    if (length > room)
        for (shown = room; shown && ((unsigned char)text[shown] & 0xC0) == 0x80;) shown--;
    if (shown < length)
        say("arceus-upscaler: dlss ngx: %.*s (cut: the first %zu of its %zu bytes)", (int)shown, text, shown, length);
    else
        say("arceus-upscaler: dlss ngx: %s", text);
}

// a path for a log line, in UTF-8
void utf8(const wchar_t* wide, char* out, int room) {
    if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, room, nullptr, nullptr)) snprintf(out, (size_t)room, "?");
}

// a path built into out, of kPath wide characters: 0 when it failed or did not fit whole
const unsigned kPath = 32768;
int join(wchar_t* out, const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    const int n = vswprintf(out, kPath, format, args);
    va_end(args);
    return n >= 0 && (unsigned)n < kPath;
}

// The folders, fixed before the instance exists: NGX looks for the feature library in the module's own folder (a
// request, not a guarantee), and writes its data in snx-ngx-data under it. 1 when both are there and writable.
wchar_t g_folder[kPath], g_data[kPath];
int g_folders; // 0 not asked yet, 1 ready, -1 refused
int folders() {
    if (g_folders) return g_folders > 0;
    g_folders = -1;
    HMODULE self = nullptr;
    char shown[3 * MAX_PATH];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&folders), &self)) {
        say("arceus-upscaler: dlss is not available: the module's own file is not known (%lu)", GetLastError());
        return 0;
    }
    const DWORD n = GetModuleFileNameW(self, g_folder, kPath);
    if (!n || n >= kPath - 32) {
        say("arceus-upscaler: dlss is not available: the module's own path is not known (%lu)", GetLastError());
        return 0;
    }
    DWORD cut = 0;
    for (DWORD i = 0; i < n; i++)
        if (g_folder[i] == L'\\' || g_folder[i] == L'/') cut = i;
    // Only a folder of the form X:\folder is taken: a drive's root, a share or any other form is refused
    const wchar_t drive = g_folder[0];
    const int letter = (drive >= L'A' && drive <= L'Z') || (drive >= L'a' && drive <= L'z');
    if (!letter || g_folder[1] != L':' || (g_folder[2] != L'\\' && g_folder[2] != L'/') || cut <= 2) {
        utf8(g_folder, shown, sizeof shown);
        say("arceus-upscaler: dlss is not available: the module is at %s, and only a module in a folder below a "
            "drive's root (X:\\folder) is taken", shown);
        return 0;
    }
    g_folder[cut] = 0;
    if (!join(g_data, L"%ls\\snx-ngx-data", g_folder)) {
        say("arceus-upscaler: dlss is not available: the path of NGX's data folder could not be made whole");
        return 0;
    }
    utf8(g_data, shown, sizeof shown);
    if (!CreateDirectoryW(g_data, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        say("arceus-upscaler: dlss is not available: NGX's data folder %s could not be made (%lu)", shown,
            GetLastError());
        return 0;
    }
    const DWORD attributes = GetFileAttributesW(g_data);
    // Writable: a new file of a name no other file has, made with CREATE_NEW so that nothing there is opened or
    // replaced, and deleted when its handle closes. Only a name already taken is tried again, four times at most.
    HANDLE file = INVALID_HANDLE_VALUE;
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
        for (unsigned attempt = 0; attempt < 4 && file == INVALID_HANDLE_VALUE; attempt++) {
            wchar_t probe[kPath];
            if (!join(probe, L"%ls\\snx-write-check-%lu-%llu-%u", g_data, GetCurrentProcessId(),
                      (unsigned long long)GetTickCount64(), attempt)) {
                say("arceus-upscaler: dlss is not available: the path of the data folder's write check could not "
                    "be made whole");
                return 0;
            }
            file = CreateFileW(probe, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) break;
        }
    if (file == INVALID_HANDLE_VALUE) {
        say("arceus-upscaler: dlss is not available: NGX's data folder %s is not a folder this process can write to "
            "(%lu)", shown, GetLastError());
        return 0;
    }
    CloseHandle(file);
    char library[3 * MAX_PATH];
    utf8(g_folder, library, sizeof library);
    say("arceus-upscaler: dlss: NGX is asked to look for the feature library in %s (a request: which file it loads "
        "is not known from it), and to write its data in %s", library, shown);
    g_folders = 1;
    return 1;
}

// What NGX is told of us, the same for both queries and for its start later
NVSDK_NGX_FeatureCommonInfo g_common;
NVSDK_NGX_FeatureDiscoveryInfo g_discovery;
const wchar_t* g_paths[1];
const NVSDK_NGX_FeatureDiscoveryInfo* discovery() {
    g_paths[0] = g_folder;
    g_common = {};
    g_common.PathListInfo.Path = g_paths;
    g_common.PathListInfo.Length = 1;
    g_common.LoggingInfo.LoggingCallback = ngx_log;
    g_common.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
    g_common.LoggingInfo.DisableOtherLoggingSinks = true;
    g_discovery = {};
    g_discovery.SDKVersion = NVSDK_NGX_Version_API;
    g_discovery.FeatureID = NVSDK_NGX_Feature_SuperSampling;
    g_discovery.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    g_discovery.Identifier.v.ProjectDesc.ProjectId = kProjectId;
    g_discovery.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    g_discovery.Identifier.v.ProjectDesc.EngineVersion = "1.0";
    g_discovery.ApplicationDataPath = g_data;
    g_discovery.FeatureInfo = &g_common;
    return &g_discovery;
}

// The names NGX returned, copied: they are the module's and stay valid until it is unloaded
struct Names {
    char name[kNamesMost][VK_MAX_EXTENSION_NAME_SIZE];
    const char* list[kNamesMost];
    uint32_t count;
};
Names g_instance, g_device;
int g_instance_asked;
// 0 when every name fits, and prints each with what becomes of it
int names_take(Names* to, const char* kind, uint32_t count, const VkExtensionProperties* properties,
               const char* fate) {
    if (count > kNamesMost) {
        say("arceus-upscaler: dlss is not available: NGX names %u %s extensions, more than the %u this module keeps",
            count, kind, kNamesMost);
        return 0;
    }
    if (count && !properties) {
        say("arceus-upscaler: dlss is not available: NGX names %u %s extensions and gives no list of them", count,
            kind);
        return 0;
    }
    to->count = count;
    for (uint32_t i = 0; i < count; i++) {
        memcpy(to->name[i], properties[i].extensionName, VK_MAX_EXTENSION_NAME_SIZE);
        to->name[i][VK_MAX_EXTENSION_NAME_SIZE - 1] = 0;
        to->list[i] = to->name[i];
        say("arceus-upscaler: dlss: NGX names the %s extension %u of %u: %s (version %u)%s", kind, i + 1, count,
            to->name[i], properties[i].specVersion, fate);
    }
    return 1;
}

// The extensions this module knows the needs of: what feature bits each one's use needs on, as core bits of the
// interface or as a structure of the chain. EMPTY in this build: no list is written from memory. It is filled from the
// names a run of this step printed and the Vulkan specification's text for each, in a step of its own.
struct Classified {
    const char* name;
    uint32_t core;
};
const Classified* classified(const char*) { return nullptr; }

// Every name NGX gives is printed and none is handed back, as none is classified: the list is empty, and the instance
// is made without them. The device's question is then asked on that instance.
int instance_extensions(SnxUpscalerExtensions* out) {
    AcquireSRWLockExclusive(&g_lock);
    int answer = SNX_UPSCALER_REFUSED;
    if (g_instance_asked) answer = SNX_UPSCALER_OK;
    else if (folders()) {
        uint32_t count = 0;
        VkExtensionProperties* properties = nullptr;
        const NVSDK_NGX_Result result =
            NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(discovery(), &count, &properties);
        if (NVSDK_NGX_FAILED(result))
            say("arceus-upscaler: dlss is not available: NGX's question of the instance's extensions failed "
                "(result 0x%08x)", (unsigned)result);
        else if (names_take(&g_instance, "instance", count, properties,
                            ", named by NGX, not enabled: not classified")) {
            g_instance_asked = 1;
            answer = SNX_UPSCALER_OK;
        }
    }
    if (answer == SNX_UPSCALER_OK) {
        out->count = 0;
        out->names = nullptr;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return answer;
}

// In this build always a refusal: every name NGX returns is printed, and none is classified. The instance was made
// without NGX's instance extensions: a failure or another answer here is a finding of the discovery run.
int device_extensions(VkInstance instance, VkPhysicalDevice device, PFN_vkGetInstanceProcAddr,
                      SnxUpscalerDeviceNeeds*) {
    AcquireSRWLockExclusive(&g_lock);
    if (!g_instance_asked || !folders()) {
        say("arceus-upscaler: dlss is not available: the device was asked about before the instance's extensions");
        ReleaseSRWLockExclusive(&g_lock);
        return SNX_UPSCALER_REFUSED;
    }
    uint32_t count = 0, unknown = 0;
    VkExtensionProperties* properties = nullptr;
    const NVSDK_NGX_Result result =
        NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, device, discovery(), &count, &properties);
    if (NVSDK_NGX_FAILED(result))
        say("arceus-upscaler: dlss is not available: NGX's question of the device's extensions failed "
            "(result 0x%08x)", (unsigned)result);
    else if (names_take(&g_device, "device", count, properties, "")) {
        for (uint32_t i = 0; i < g_device.count; i++)
            if (!classified(g_device.name[i])) {
                unknown++;
                say("arceus-upscaler: dlss: the device extension %s is not classified: what its use needs is not "
                    "known to this build", g_device.name[i]);
            }
        say("arceus-upscaler: dlss: needs discovered, not classified: unavailable (%u instance extensions named by NGX "
            "and not enabled, %u device extensions named by NGX on an instance without them, %u of those not "
            "classified, %ld messages of NGX)", g_instance.count, g_device.count, unknown, g_ngx_messages);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return SNX_UPSCALER_REFUSED;
}

int create(const SnxUpscalerCreate*, SnxUpscalerTraits*, SnxUpscaler** out) {
    *out = nullptr;
    say("arceus-upscaler: dlss: no context is made in this build, which only discovers NGX's needs");
    return SNX_UPSCALER_REFUSED;
}
int prepare(SnxUpscaler*, VkCommandBuffer) { return SNX_UPSCALER_REFUSED; }
int evaluate(SnxUpscaler*, const SnxUpscalerFrame*) { return SNX_UPSCALER_REFUSED; }
void destroy(SnxUpscaler*) {}
// nothing of NGX was started for any device
int shutdown(VkDevice) { return SNX_UPSCALER_OK; }

const SnxUpscalerApi g_api = {SNX_UPSCALER_ABI, "dlss", instance_extensions, device_extensions, create, prepare,
                              evaluate, destroy, shutdown};
} // namespace

// The one export: the table for our number, or nothing. It keeps the host's log and starts nothing.
extern "C" __declspec(dllexport) const SnxUpscalerApi* snx_upscaler_api(uint32_t abi, SnxUpscalerLog log) {
    if (abi != SNX_UPSCALER_ABI) return nullptr;
    g_log = log;
    return &g_api;
}
static_assert(std::is_same<decltype(&snx_upscaler_api), SnxUpscalerEntry>::value, "the export's type");
