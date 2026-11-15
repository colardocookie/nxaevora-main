// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// DLSS's adapter (src/nx/gpu/vk/upscaler.h), a module of its own built against the builder's NGX SDK. This build is
// the step that classifies NGX's needs: the extensions NGX names are taken by a table of what each one needs, held
// against what the instance and the device advertise, and handed to the device's maker with their core bits.
// Nothing of NGX is started yet: create, prepare and evaluate refuse.
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

// The names NGX returned, copied with the revision it asks for: the module's, valid until it is unloaded
struct Names {
    char name[kNamesMost][VK_MAX_EXTENSION_NAME_SIZE];
    const char* list[kNamesMost];
    uint32_t version[kNamesMost];
    uint32_t count;
};
Names g_instance, g_device;
int g_instance_asked;
// 0 when every name fits
int names_take(Names* to, const char* kind, uint32_t count, const VkExtensionProperties* properties) {
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
        to->version[i] = properties[i].specVersion;
    }
    return 1;
}

// The extensions this module knows the needs of (UPSCALER-DESIGN.md, DLSS's needs classified, from run DL-S1 and the
// registry): the revision DL-S1 saw as the least taken, and the core feature bits each one's use needs on. A name
// not here, or a revision below the larger of NGX's request and this one, makes DLSS unavailable.
struct Classified {
    const char* name;
    int instance;
    uint32_t least, core;
};
const Classified kClassified[] = {
    {"VK_KHR_get_physical_device_properties2", 1, 2, 0},
    {"VK_NVX_binary_import", 0, 1, 0},
    {"VK_NVX_image_view_handle", 0, 2, 0},
    {"VK_KHR_buffer_device_address", 0, 1, SNX_UPSCALER_CORE_BUFFER_DEVICE_ADDRESS},
    {"VK_KHR_push_descriptor", 0, 2, 0},
};
const Classified* classified(const char* name, int instance) {
    for (const Classified& c : kClassified)
        if (c.instance == instance && !strcmp(c.name, name)) return &c;
    return nullptr;
}
// 1 when every name is classified, each printed with what it needs
int names_classified(const Names* names, int instance) {
    const char* kind = instance ? "instance" : "device";
    int all = 1;
    for (uint32_t i = 0; i < names->count; i++) {
        const Classified* c = classified(names->name[i], instance);
        if (!c) {
            say("arceus-upscaler: dlss: NGX names the %s extension %u of %u: %s (version %u), which is not classified",
                kind, i + 1, names->count, names->name[i], names->version[i]);
            all = 0;
            continue;
        }
        say("arceus-upscaler: dlss: NGX names the %s extension %u of %u: %s (version %u), classified: %s, taken from "
            "version %u", kind, i + 1, names->count, names->name[i], names->version[i],
            c->core & SNX_UPSCALER_CORE_BUFFER_DEVICE_ADDRESS ? "needs buffer device addresses" : "no feature bit",
            names->version[i] > c->least ? names->version[i] : c->least);
    }
    if (!all)
        say("arceus-upscaler: dlss is not available: NGX names a %s extension this build has not classified", kind);
    return all;
}

// What the instance or the device advertises, read whole: NULL with a line when it cannot be
VkExtensionProperties* advertised(PFN_vkEnumerateInstanceExtensionProperties of_instance,
                                  PFN_vkEnumerateDeviceExtensionProperties of_device, VkPhysicalDevice device,
                                  uint32_t* count) {
    for (int attempt = 0; attempt < 3; attempt++) {
        uint32_t n = 0;
        VkResult r = of_device ? of_device(device, nullptr, &n, nullptr) : of_instance(nullptr, &n, nullptr);
        if (r != VK_SUCCESS) break;
        VkExtensionProperties* list = static_cast<VkExtensionProperties*>(calloc(n ? n : 1, sizeof *list));
        if (!list) break;
        r = of_device ? of_device(device, nullptr, &n, list) : of_instance(nullptr, &n, list);
        if (r == VK_SUCCESS) {
            *count = n;
            return list;
        }
        free(list);
        if (r != VK_INCOMPLETE) break;
    }
    say("arceus-upscaler: dlss is not available: the %s's list of extensions could not be read whole",
        of_device ? "physical device" : "instance");
    return nullptr;
}
// 1 when each name is advertised at a revision of at least the larger of NGX's request and the table's least
int names_advertised(const Names* names, int instance, const VkExtensionProperties* list, uint32_t listed) {
    int all = 1;
    for (uint32_t i = 0; i < names->count; i++) {
        const Classified* c = classified(names->name[i], instance);
        const uint32_t least = names->version[i] > c->least ? names->version[i] : c->least;
        uint32_t k = 0;
        while (k < listed && strcmp(list[k].extensionName, names->name[i])) k++;
        if (k == listed) {
            say("arceus-upscaler: dlss is not available: the %s does not list %s", instance ? "instance" : "device",
                names->name[i]);
            all = 0;
        } else if (list[k].specVersion < least) {
            say("arceus-upscaler: dlss is not available: the %s lists %s at version %u, below %u",
                instance ? "instance" : "device", names->name[i], list[k].specVersion, least);
            all = 0;
        }
    }
    return all;
}

// NGX's instance names, each classified and handed back, or a refusal. Their revisions are held against the
// instance's at the device's question, where the module has a lookup.
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
        else if (names_take(&g_instance, "instance", count, properties) && names_classified(&g_instance, 1)) {
            g_instance_asked = 1;
            answer = SNX_UPSCALER_OK;
        }
    }
    if (answer == SNX_UPSCALER_OK) {
        out->count = g_instance.count;
        out->names = g_instance.list;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return answer;
}

// NGX's device names, each classified, the instance's and the device's revisions held against them, and the core
// bits they need. No feature chain: NGX names extensions only.
int device_extensions(VkInstance instance, VkPhysicalDevice device, PFN_vkGetInstanceProcAddr lookup,
                      SnxUpscalerDeviceNeeds* out) {
    AcquireSRWLockExclusive(&g_lock);
    int answer = SNX_UPSCALER_REFUSED;
    uint32_t count = 0, core = 0;
    VkExtensionProperties* properties = nullptr;
    const PFN_vkEnumerateInstanceExtensionProperties of_instance =
        lookup ? reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
                     lookup(nullptr, "vkEnumerateInstanceExtensionProperties"))
               : nullptr;
    const PFN_vkEnumerateDeviceExtensionProperties of_device =
        lookup ? reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
                     lookup(instance, "vkEnumerateDeviceExtensionProperties"))
               : nullptr;
    if (!g_instance_asked || !folders())
        say("arceus-upscaler: dlss is not available: the device was asked about before the instance's extensions");
    else if (!of_instance || !of_device)
        say("arceus-upscaler: dlss is not available: the lookup it was handed gives no list of extensions");
    else {
        const NVSDK_NGX_Result result =
            NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, device, discovery(), &count, &properties);
        uint32_t listed_instance = 0, listed_device = 0;
        VkExtensionProperties* on_instance = nullptr;
        VkExtensionProperties* on_device = nullptr;
        if (NVSDK_NGX_FAILED(result))
            say("arceus-upscaler: dlss is not available: NGX's question of the device's extensions failed "
                "(result 0x%08x)", (unsigned)result);
        else if (names_take(&g_device, "device", count, properties) && names_classified(&g_device, 0) &&
                 (on_instance = advertised(of_instance, nullptr, nullptr, &listed_instance)) != nullptr &&
                 (on_device = advertised(nullptr, of_device, device, &listed_device)) != nullptr &&
                 names_advertised(&g_instance, 1, on_instance, listed_instance) &&
                 names_advertised(&g_device, 0, on_device, listed_device)) {
            for (uint32_t i = 0; i < g_device.count; i++) core |= classified(g_device.name[i], 0)->core;
            out->extensions.count = g_device.count;
            out->extensions.names = g_device.list;
            out->features = nullptr;
            out->core = core;
            say("arceus-upscaler: dlss: needs classified: %u instance and %u device extensions, %s, %ld messages of "
                "NGX", g_instance.count, g_device.count,
                core & SNX_UPSCALER_CORE_BUFFER_DEVICE_ADDRESS ? "buffer device addresses on" : "no feature bit",
                g_ngx_messages);
            answer = SNX_UPSCALER_OK;
        }
        free(on_instance);
        free(on_device);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return answer;
}

int create(const SnxUpscalerCreate*, SnxUpscalerTraits*, SnxUpscaler** out) {
    *out = nullptr;
    say("arceus-upscaler: dlss: no context is made in this build: NGX's start is not in it yet");
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
