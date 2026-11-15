// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// DLSS's adapter (src/nx/gpu/vk/upscaler.h), a module of its own built against the builder's NGX SDK. This build is
// NGX's needs by a table, NGX's lifetimes by dlss_core.h, its numbers by dlss_eval.h and what is recorded around its
// evaluation by dlss_record.h. DLAA only, at the native size: the render size equals the output's. The self-test
// alone loads the module (the game's guard).
#include <windows.h>
#include <psapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>
#include <vulkan/vulkan_core.h>
#include "nx/gpu/vk/upscaler.h"
#include "nvsdk_ngx_vk.h"
#include "nvsdk_ngx_helpers.h"
#include "nvsdk_ngx_helpers_vk.h"
#include "dlss_core.h"
#include "dlss_eval.h"
#include "dlss_record.h"

namespace {
// ours, fixed, GUID-like as NGX asks of a project id (CUSTOM engine)
const char kProjectId[] = "0f297030-0b63-4170-ac30-3c0d1cb0de45";
const unsigned kNgxPrinted = 256; // NGX's messages printed in full, the rest counted
const uint32_t kNamesMost = 32;

SnxUpscalerLog g_log;
SRWLOCK g_lock = SRWLOCK_INIT; // every call into NGX, and the module's state
volatile LONG g_ngx_messages, g_ngx_config; // printed or counted; configuration lines, counted only

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
    if (snx_dlss::ngx_config_line(message)) { // not printed, and not among the printed ones (DL2 point 6)
        InterlockedIncrement(&g_ngx_config);
        return;
    }
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
                "NGX and %ld configuration lines counted, not printed", g_instance.count, g_device.count,
                core & SNX_UPSCALER_CORE_BUFFER_DEVICE_ADDRESS ? "buffer device addresses needed"
                                                               : "no feature bit needed",
                g_ngx_messages, g_ngx_config);
            answer = SNX_UPSCALER_OK;
        }
        free(on_instance);
        free(on_device);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return answer;
}

// the mirrored numbers of dlss_eval.h are the interface's, Vulkan's and NGX's
static_assert(snx_dlss::kLinearHdr == SNX_UPSCALER_COLOUR_LINEAR_HDR &&
                  snx_dlss::kDepthReversed == SNX_UPSCALER_DEPTH_REVERSED &&
                  snx_dlss::kUnjittered == SNX_UPSCALER_VECTORS_UNJITTERED &&
                  snx_dlss::kExposureAutomatic == SNX_UPSCALER_EXPOSURE_AUTOMATIC &&
                  snx_dlss::kFarInfinite == SNX_UPSCALER_FAR_PLANE_INFINITE,
              "the interface's flags");
static_assert(snx_dlss::kMotionR16G16Float == VK_FORMAT_R16G16_SFLOAT &&
                  snx_dlss::kMotionR32G32Float == VK_FORMAT_R32G32_SFLOAT,
              "Vulkan's formats");
static_assert(snx_dlss::kNgxIsHdr == NVSDK_NGX_DLSS_Feature_Flags_IsHDR &&
                  snx_dlss::kNgxMvLowRes == NVSDK_NGX_DLSS_Feature_Flags_MVLowRes &&
                  snx_dlss::kNgxMvJittered == NVSDK_NGX_DLSS_Feature_Flags_MVJittered &&
                  snx_dlss::kNgxDepthInverted == NVSDK_NGX_DLSS_Feature_Flags_DepthInverted &&
                  snx_dlss::kNgxAutoExposure == NVSDK_NGX_DLSS_Feature_Flags_AutoExposure,
              "NGX's flags");

// ---- NGX's lifetimes (dlss_core.h): started at a device's first context with the handles of the create that asks
const SnxUpscalerCreate* g_starting; // the create that begins a context, while core_begin runs
int ngx_start(void*, uint64_t) {
    const SnxUpscalerCreate* c = g_starting;
    const NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_Init_with_ProjectID(
        kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", g_data, c->instance, c->physical_device, c->device,
        c->instance_lookup, c->device_lookup, &g_common, NVSDK_NGX_Version_API);
    if (NVSDK_NGX_FAILED(r))
        say("arceus-upscaler: dlss: NGX's start for the device failed (result 0x%08x)", (unsigned)r);
    return !NVSDK_NGX_FAILED(r);
}
int ngx_stop(void*, uint64_t device) {
    VkDevice d;
    memcpy(&d, &device, sizeof d);
    const NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_Shutdown1(d);
    if (NVSDK_NGX_FAILED(r)) say("arceus-upscaler: dlss: NGX's shutdown failed (result 0x%08x)", (unsigned)r);
    return !NVSDK_NGX_FAILED(r);
}
struct Context {
    VkDevice device;
    NVSDK_NGX_Parameter* params;
    NVSDK_NGX_Handle* feature; // what NGX's creation returned
    int prepared;              // the creation succeeded: only then is a frame evaluated
    NVSDK_NGX_DLSS_Create_Params made;
    SnxUpscalerSizes sizes;
    snx_dlss::Made made_with; // what each frame is held to
    PFN_vkCmdPipelineBarrier barrier;
    uint64_t frames;
};
int ngx_release(void*, void* context) {
    Context* x = static_cast<Context*>(context);
    int ok = 1;
    if (x->feature) {
        const NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_ReleaseFeature(x->feature);
        if (NVSDK_NGX_FAILED(r))
            say("arceus-upscaler: dlss: NGX's release of a feature failed (result 0x%08x)", (unsigned)r);
        ok = ok && !NVSDK_NGX_FAILED(r);
    }
    if (x->params) {
        const NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_DestroyParameters(x->params);
        if (NVSDK_NGX_FAILED(r))
            say("arceus-upscaler: dlss: NGX's parameters could not be destroyed (result 0x%08x)", (unsigned)r);
        ok = ok && !NVSDK_NGX_FAILED(r);
    }
    return ok;
}
void core_line(void*, const char* line) { say("%s", line); }
snx_dlss::Core g_core = {{nullptr, ngx_start, ngx_stop, ngx_release, core_line}, {}};
uint64_t device_key(VkDevice d) {
    uint64_t key = 0;
    memcpy(&key, &d, sizeof d);
    return key;
}

// What NGX says of DLSS on this device (the capability parameters, printed whole), and the render sizes of each
// quality mode at the output size. 1 when DLSS is available and DLAA takes the render size, 0 when not, -1 when a
// call of NGX failed: each with a line.
int capabilities(const SnxUpscalerCreate* c) {
    NVSDK_NGX_Parameter* cap = nullptr;
    NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_GetCapabilityParameters(&cap);
    if (snx_dlss::core_call(&g_core, device_key(c->device), "NGX's capability parameters", !NVSDK_NGX_FAILED(r),
                            cap != nullptr) != snx_dlss::CORE_OK) {
        say("arceus-upscaler: dlss is not available: NGX's capability parameters could not be had (result 0x%08x)",
            (unsigned)r);
        return -1;
    }
    int i_value = 0;
    unsigned u_value = 0;
    NVSDK_NGX_Result got = NVSDK_NGX_Parameter_GetI(cap, NVSDK_NGX_Parameter_SuperSampling_Available, &i_value);
    const snx_dlss::Field available = {!NVSDK_NGX_FAILED(got), i_value, (uint32_t)got};
    i_value = 0;
    got = NVSDK_NGX_Parameter_GetI(cap, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &i_value);
    const snx_dlss::Field needs_driver = {!NVSDK_NGX_FAILED(got), i_value, (uint32_t)got};
    got = NVSDK_NGX_Parameter_GetUI(cap, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &u_value);
    const snx_dlss::Field major = {!NVSDK_NGX_FAILED(got), u_value, (uint32_t)got};
    u_value = 0;
    got = NVSDK_NGX_Parameter_GetUI(cap, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &u_value);
    const snx_dlss::Field minor = {!NVSDK_NGX_FAILED(got), u_value, (uint32_t)got};
    i_value = 0;
    got = NVSDK_NGX_Parameter_GetI(cap, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &i_value);
    const snx_dlss::Field init = {!NVSDK_NGX_FAILED(got), i_value, (uint32_t)got};
    char fields[5][48];
    const snx_dlss::Field* all[5] = {&available, &needs_driver, &major, &minor, &init};
    for (int k = 0; k < 5; k++)
        if (all[k]->got && k == 4) /* NGX's result: its 32 bits */
            snprintf(fields[k], sizeof fields[k], "0x%08x", (unsigned)(uint32_t)all[k]->value);
        else if (all[k]->got) snprintf(fields[k], sizeof fields[k], "%lld", all[k]->value);
        else snprintf(fields[k], sizeof fields[k], "not given (result 0x%08x)", (unsigned)all[k]->result);
    say("arceus-upscaler: dlss: NGX's capabilities: available %s, needs an updated driver %s, the least driver %s.%s, "
        "the feature's init result %s", fields[0], fields[1], fields[2], fields[3], fields[4]);
    const int verdict = snx_dlss::cap_verdict(available, needs_driver);
    static const struct {
        NVSDK_NGX_PerfQuality_Value mode;
        const char* name;
    } modes[] = {{NVSDK_NGX_PerfQuality_Value_UltraPerformance, "ultra performance"},
                 {NVSDK_NGX_PerfQuality_Value_MaxPerf, "performance"},
                 {NVSDK_NGX_PerfQuality_Value_Balanced, "balanced"},
                 {NVSDK_NGX_PerfQuality_Value_MaxQuality, "quality"},
                 {NVSDK_NGX_PerfQuality_Value_UltraQuality, "ultra quality"},
                 {NVSDK_NGX_PerfQuality_Value_DLAA, "DLAA"}};
    int dlaa = 0;
    for (const auto& m : modes) {
        unsigned w = 0, h = 0, max_w = 0, max_h = 0, min_w = 0, min_h = 0;
        float sharpness = 0;
        r = NGX_DLSS_GET_OPTIMAL_SETTINGS(cap, c->sizes.output_w, c->sizes.output_h, m.mode, &w, &h, &max_w, &max_h,
                                          &min_w, &min_h, &sharpness);
        if (NVSDK_NGX_FAILED(r) || !w || !h) {
            say("arceus-upscaler: dlss: %s at %ux%u: not available (result 0x%08x, optimal %ux%u)", m.name,
                c->sizes.output_w, c->sizes.output_h, (unsigned)r, w, h);
            continue;
        }
        say("arceus-upscaler: dlss: %s at %ux%u: optimal %ux%u, from %ux%u to %ux%u", m.name, c->sizes.output_w,
            c->sizes.output_h, w, h, min_w, min_h, max_w, max_h);
        if (m.mode == NVSDK_NGX_PerfQuality_Value_DLAA)
            dlaa = c->sizes.render_w >= min_w && c->sizes.render_w <= max_w && c->sizes.render_h >= min_h &&
                   c->sizes.render_h <= max_h;
    }
    r = NVSDK_NGX_VULKAN_DestroyParameters(cap);
    if (NVSDK_NGX_FAILED(r)) { /* the device's shutdown answers FAILED for it */
        say("arceus-upscaler: dlss: NGX's capability parameters could not be destroyed (result 0x%08x)", (unsigned)r);
        snx_dlss::core_latch(&g_core, device_key(c->device));
    }
    if (verdict != snx_dlss::CAP_OK) {
        say("arceus-upscaler: dlss is not available: %s", snx_dlss::cap_text(verdict));
        return verdict == snx_dlss::CAP_NOT_READ ? -1 : 0;
    }
    if (!dlaa)
        say("arceus-upscaler: dlss is not available: DLAA does not take the render size %ux%u", c->sizes.render_w,
            c->sizes.render_h);
    return dlaa;
}

// A context, no GPU work: NGX started for the device (once), its capabilities and modes printed, DLAA at the native
// size only, the flags as NGX's, and the context's own parameters. The feature is made by prepare.
int create(const SnxUpscalerCreate* c, SnxUpscalerTraits* traits, SnxUpscaler** out) {
    *out = nullptr;
    const int check = snx_dlss::create_check(c->flags, (int)c->motion_format, c->sizes.render_w, c->sizes.render_h,
                                             c->sizes.image_w, c->sizes.image_h, c->sizes.output_w, c->sizes.output_h);
    if (check != snx_dlss::CREATE_OK) {
        say("arceus-upscaler: dlss: no context: %s (flags 0x%x, vectors' format %d, render %ux%u, images %ux%u, "
            "output %ux%u)", snx_dlss::create_text(check), c->flags, (int)c->motion_format, c->sizes.render_w,
            c->sizes.render_h, c->sizes.image_w, c->sizes.image_h, c->sizes.output_w, c->sizes.output_h);
        return SNX_UPSCALER_REFUSED;
    }
    const PFN_vkVoidFunction found = c->device_lookup ? c->device_lookup(c->device, "vkCmdPipelineBarrier") : nullptr;
    const PFN_vkCmdPipelineBarrier barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(found);
    if (!barrier) {
        say("arceus-upscaler: dlss: no context: the device lookup gives no vkCmdPipelineBarrier");
        return SNX_UPSCALER_REFUSED;
    }
    AcquireSRWLockExclusive(&g_lock);
    const uint64_t key = device_key(c->device);
    g_starting = c;
    const int begun = snx_dlss::core_begin(&g_core, key);
    g_starting = nullptr;
    if (begun != snx_dlss::CORE_OK) {
        ReleaseSRWLockExclusive(&g_lock);
        return begun == snx_dlss::CORE_FAILED ? SNX_UPSCALER_FAILED : SNX_UPSCALER_REFUSED;
    }
    Context* x = static_cast<Context*>(calloc(1, sizeof *x));
    int answer = SNX_UPSCALER_REFUSED;
    const int can = x ? capabilities(c) : 0;
    if (!x) say("arceus-upscaler: dlss: no context: no memory");
    else if (can < 0) answer = SNX_UPSCALER_FAILED; /* a call of NGX failed */
    else if (can) {
        const NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_AllocateParameters(&x->params);
        if (snx_dlss::core_call(&g_core, key, "NGX's parameters for a context", !NVSDK_NGX_FAILED(r),
                                x->params != nullptr) != snx_dlss::CORE_OK) {
            say("arceus-upscaler: dlss: no context: NGX gave no parameters (result 0x%08x)", (unsigned)r);
            x->params = nullptr; /* a pointer handed out on a failure is kept by NGX's record, not released here */
            answer = SNX_UPSCALER_FAILED;
        } else
            answer = SNX_UPSCALER_OK;
    }
    if (answer != SNX_UPSCALER_OK) {
        snx_dlss::core_end(&g_core, key, x && x->params ? x : nullptr); /* the begun context ends here */
        free(x);
        ReleaseSRWLockExclusive(&g_lock);
        return answer;
    }
    const int flags = snx_dlss::ngx_flags(c->flags);
    x->device = c->device;
    x->sizes = c->sizes;
    x->made_with = snx_dlss::Made{c->sizes.render_w, c->sizes.render_h, c->sizes.image_w, c->sizes.image_h,
                                  c->sizes.output_w, c->sizes.output_h,
                                  {(int)c->colour_format, (int)c->depth_format, (int)c->motion_format,
                                   (int)c->output_format}};
    x->barrier = barrier;
    x->made = NVSDK_NGX_DLSS_Create_Params{};
    x->made.Feature.InWidth = c->sizes.render_w;
    x->made.Feature.InHeight = c->sizes.render_h;
    x->made.Feature.InTargetWidth = c->sizes.output_w;
    x->made.Feature.InTargetHeight = c->sizes.output_h;
    x->made.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA;
    x->made.InFeatureCreateFlags = flags;
    traits->submits_itself = 0;
    traits->takes_rectangle = 1;
    traits->rectangle_at_origin = 1;
    traits->colour_usage = traits->depth_usage = traits->motion_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    traits->output_usage = VK_IMAGE_USAGE_STORAGE_BIT;
    say("arceus-upscaler: dlss: a context for DLAA at %ux%u, NGX's flags 0x%x", c->sizes.output_w, c->sizes.output_h,
        (unsigned)flags);
    *out = reinterpret_cast<SnxUpscaler*>(x);
    ReleaseSRWLockExclusive(&g_lock);
    return SNX_UPSCALER_OK;
}

// The feature's creation, recorded into the runtime's command buffer, with the presets left at their defaults
int prepare(SnxUpscaler* u, VkCommandBuffer cb) {
    Context* x = reinterpret_cast<Context*>(u);
    static int presets_said;
    AcquireSRWLockExclusive(&g_lock);
    if (!presets_said++)
        say("arceus-upscaler: dlss: the SDK's header names the presets default %d, E %d and F %d (deprecated), J %d, "
            "K %d, L %d, M %d: the defaults are left", NVSDK_NGX_DLSS_Hint_Render_Preset_Default,
            NVSDK_NGX_DLSS_Hint_Render_Preset_E, NVSDK_NGX_DLSS_Hint_Render_Preset_F,
            NVSDK_NGX_DLSS_Hint_Render_Preset_J, NVSDK_NGX_DLSS_Hint_Render_Preset_K,
            NVSDK_NGX_DLSS_Hint_Render_Preset_L, NVSDK_NGX_DLSS_Hint_Render_Preset_M);
    int answer = SNX_UPSCALER_FAILED;
    if (!snx_dlss::core_known(&g_core, device_key(x->device)))
        say("arceus-upscaler: dlss: a context not prepared: NGX's state for its device is not known");
    else if (x->prepared || x->feature) say("arceus-upscaler: dlss: a context was prepared twice");
    else {
        const NVSDK_NGX_Result r = NGX_VULKAN_CREATE_DLSS_EXT1(x->device, cb, 1, 1, &x->feature, x->params, &x->made);
        const int outcome = snx_dlss::core_call(&g_core, device_key(x->device), "NGX's creation of the feature",
                                                !NVSDK_NGX_FAILED(r), x->feature != nullptr);
        if (outcome != snx_dlss::CORE_OK)
            say("arceus-upscaler: dlss: NGX's creation of the feature failed (result 0x%08x, a handle %s)",
                (unsigned)r, x->feature ? "returned, kept" : "not returned");
        else {
            x->prepared = 1;
            answer = SNX_UPSCALER_OK;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return answer;
}

// Which loaded module NGX's feature library is, by its name in the process: a log line, no check. "None" and "the
// only one" are said only when every module's name was read whole; a cut list or a name not read is said as such.
void provenance() {
    HMODULE modules[1024];
    DWORD bytes = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof modules, &bytes)) {
        say("arceus-upscaler: dlss: observed in the process: not known, the list of modules could not be read (%lu)",
            GetLastError());
        return;
    }
    const DWORD listed = bytes / sizeof modules[0], read = listed < 1024 ? listed : 1024;
    int found = 0, unread = 0;
    wchar_t path[MAX_PATH * 4], first[MAX_PATH * 4] = L"";
    for (DWORD i = 0; i < read; i++) {
        const DWORD n = GetModuleFileNameW(modules[i], path, MAX_PATH * 4);
        if (!n || n >= MAX_PATH * 4) { /* failed, or cut to the buffer */
            unread++;
            continue;
        }
        const wchar_t* name = wcsrchr(path, L'\\');
        if (_wcsicmp(name ? name + 1 : path, L"nvngx_dlss.dll")) continue;
        if (!found++) wcscpy_s(first, path);
    }
    const int whole = listed <= 1024 && !unread;
    char gaps[160] = "";
    if (!whole)
        snprintf(gaps, sizeof gaps, " (of %lu modules %lu listed here, %d names not read whole)",
                 (unsigned long)listed, (unsigned long)read, unread);
    if (found > 1) {
        say("arceus-upscaler: dlss: observed in the process: %d modules named nvngx_dlss.dll: ambiguous%s", found,
            gaps);
        return;
    }
    if (!found) {
        say(whole ? "arceus-upscaler: dlss: observed in the process: no module named nvngx_dlss.dll%s"
                  : "arceus-upscaler: dlss: observed in the process: none named nvngx_dlss.dll among the names read, "
                    "so whether one is loaded is not known%s",
            gaps);
        return;
    }
    char shown[MAX_PATH * 12];
    utf8(first, shown, sizeof shown);
    char version[64] = "version not known";
    DWORD handle = 0, size = GetFileVersionInfoSizeW(first, &handle);
    unsigned char* info = size ? static_cast<unsigned char*>(malloc(size)) : nullptr;
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT length = 0;
    if (info && GetFileVersionInfoW(first, 0, size, info) &&
        VerQueryValueW(info, L"\\", reinterpret_cast<void**>(&fixed), &length) && fixed && length >= sizeof *fixed)
        snprintf(version, sizeof version, "version %u.%u.%u.%u", HIWORD(fixed->dwFileVersionMS),
                 LOWORD(fixed->dwFileVersionMS), HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS));
    free(info);
    say("arceus-upscaler: dlss: observed in the process: nvngx_dlss.dll at %s, %s%s%s", shown, version,
        whole ? "" : ", not known to be the only one", gaps);
}

// NGX's evaluation for one frame: the numbers (dlss_eval.h, the jitter's signs +1 and +1: a self-test's variants
// multiply the frame's own), the images as NGX's resources, the barriers and the call (dlss_record.h). UNKNOWN when
// NGX answered with a failure: the command buffer must not be submitted.
struct Evaluation {
    NVSDK_NGX_Handle* feature;
    NVSDK_NGX_Parameter* params;
    NVSDK_NGX_VK_DLSS_Eval_Params* eval;
    VkCommandBuffer cb;
    NVSDK_NGX_Result result;
};
int ngx_evaluate(void* user) {
    Evaluation* e = static_cast<Evaluation*>(user);
    e->result = NGX_VULKAN_EVALUATE_DLSS_EXT(e->cb, e->feature, e->params, e->eval);
    return !NVSDK_NGX_FAILED(e->result);
}
int has_stencil(VkFormat f) {
    return f == VK_FORMAT_D16_UNORM_S8_UINT || f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT;
}
NVSDK_NGX_Resource_VK resource(const SnxUpscalerImage& im, VkImageAspectFlags aspect, bool write) {
    return NVSDK_NGX_Create_ImageView_Resource_VK(im.view, im.image,
                                                  VkImageSubresourceRange{aspect, 0, 1, 0, 1}, im.format, im.w, im.h,
                                                  write);
}
int evaluate(SnxUpscaler* u, const SnxUpscalerFrame* f) {
    Context* x = reinterpret_cast<Context*>(u);
    static const int plus[2] = {1, 1};
    const SnxUpscalerImage* frame_images[4] = {&f->colour, &f->depth, &f->motion, &f->output};
    snx_dlss::Handed handed[4];
    for (int k = 0; k < 4; k++) {
        handed[k] = snx_dlss::Handed{0, 0, (int)frame_images[k]->format, frame_images[k]->w, frame_images[k]->h};
        memcpy(&handed[k].image, &frame_images[k]->image, sizeof frame_images[k]->image);
        memcpy(&handed[k].view, &frame_images[k]->view, sizeof frame_images[k]->view);
    }
    int which = -1;
    const int held = snx_dlss::frame_check(x->made_with, f->render_w, f->render_h, handed, &which);
    if (held != snx_dlss::FRAME_OK) { /* nothing recorded */
        static const char* const names[4] = {"colour", "depth", "vectors", "output"};
        say("arceus-upscaler: dlss: a frame not evaluated: %s%s%s", snx_dlss::frame_text(held),
            which >= 0 ? ": the " : "", which >= 0 ? names[which] : "");
        return SNX_UPSCALER_FAILED;
    }
    snx_dlss::Eval n;
    const int why = snx_dlss::eval_numbers(f->vector_scale, f->jitter, plus, f->render_x, f->render_y, f->render_w,
                                           f->render_h, f->colour.w, f->colour.h, f->reset, f->frame_seconds, &n);
    if (why != snx_dlss::EVAL_OK) { /* nothing recorded */
        say("arceus-upscaler: dlss: a frame not evaluated: %s", snx_dlss::eval_text(why));
        return SNX_UPSCALER_FAILED;
    }
    AcquireSRWLockExclusive(&g_lock);
    if (!x->prepared || !snx_dlss::core_known(&g_core, device_key(x->device))) {
        ReleaseSRWLockExclusive(&g_lock);
        say("arceus-upscaler: dlss: a frame not evaluated: the context was not prepared, or NGX's state for its "
            "device is not known");
        return SNX_UPSCALER_FAILED;
    }
    if (!x->frames) provenance();
    NVSDK_NGX_Resource_VK colour = resource(f->colour, VK_IMAGE_ASPECT_COLOR_BIT, false),
                          depth = resource(f->depth, VK_IMAGE_ASPECT_DEPTH_BIT, false),
                          motion = resource(f->motion, VK_IMAGE_ASPECT_COLOR_BIT, false),
                          output = resource(f->output, VK_IMAGE_ASPECT_COLOR_BIT, true);
    NVSDK_NGX_VK_DLSS_Eval_Params eval = {};
    eval.Feature.pInColor = &colour;
    eval.Feature.pInOutput = &output;
    eval.pInDepth = &depth;
    eval.pInMotionVectors = &motion;
    eval.InJitterOffsetX = n.jitter[0];
    eval.InJitterOffsetY = n.jitter[1];
    eval.InRenderSubrectDimensions = NVSDK_NGX_Dimensions{n.render_w, n.render_h};
    eval.InReset = n.reset;
    eval.InMVScaleX = n.mv_scale[0];
    eval.InMVScaleY = n.mv_scale[1];
    eval.InPreExposure = n.pre_exposure;
    eval.InExposureScale = n.exposure_scale;
    eval.InFrameTimeDeltaInMsec = n.frame_ms;
    const VkImageAspectFlags depth_aspect =
        VK_IMAGE_ASPECT_DEPTH_BIT | (has_stencil(f->depth.format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
    const snx_dlss::Image images[4] = {
        {f->colour.image, VK_IMAGE_ASPECT_COLOR_BIT, f->colour.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {f->depth.image, depth_aspect, f->depth.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {f->motion.image, VK_IMAGE_ASPECT_COLOR_BIT, f->motion.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {f->output.image, VK_IMAGE_ASPECT_COLOR_BIT, f->output.layout, VK_IMAGE_LAYOUT_GENERAL}};
    if (f->trace)
        say("arceus-upscaler: dlss dispatch: jitter %.9g %.9g, vector scale %.9g %.9g, render %ux%u, reset %d, "
            "%.9g ms, pre-exposure %.9g, exposure scale %.9g, created for %ux%u to %ux%u, NGX's flags 0x%x",
            (double)n.jitter[0],
            (double)n.jitter[1], (double)n.mv_scale[0], (double)n.mv_scale[1], n.render_w, n.render_h, n.reset,
            (double)n.frame_ms, (double)n.pre_exposure, (double)n.exposure_scale, x->made.Feature.InWidth,
            x->made.Feature.InHeight, x->made.Feature.InTargetWidth, x->made.Feature.InTargetHeight,
            (unsigned)x->made.InFeatureCreateFlags);
    Evaluation e = {x->feature, x->params, &eval, f->cb, NVSDK_NGX_Result_Success};
    const int recorded = snx_dlss::record_evaluation(x->barrier, f->cb, images, ngx_evaluate, &e);
    if (++x->frames % 600 == 0)
        say("arceus-upscaler: dlss: %llu frames of this context, %ld messages of NGX and %ld configuration lines so "
            "far", (unsigned long long)x->frames, g_ngx_messages, g_ngx_config);
    ReleaseSRWLockExclusive(&g_lock);
    if (recorded == snx_dlss::RECORD_UNKNOWN) {
        say("arceus-upscaler: dlss: NGX's evaluation failed (result 0x%08x): what it recorded is not known",
            (unsigned)e.result);
        return SNX_UPSCALER_UNKNOWN;
    }
    return SNX_UPSCALER_OK;
}

// The runtime calls it after the context's work completed or was discarded: its feature and parameters released
void destroy(SnxUpscaler* u) {
    Context* x = reinterpret_cast<Context*>(u);
    if (!x) return;
    AcquireSRWLockExclusive(&g_lock);
    snx_dlss::core_end(&g_core, device_key(x->device), x);
    ReleaseSRWLockExclusive(&g_lock);
    free(x);
}
int shutdown(VkDevice device) {
    AcquireSRWLockExclusive(&g_lock);
    const int answer = snx_dlss::core_shutdown(&g_core, device_key(device));
    ReleaseSRWLockExclusive(&g_lock);
    return answer == snx_dlss::CORE_OK ? SNX_UPSCALER_OK : SNX_UPSCALER_FAILED;
}

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
