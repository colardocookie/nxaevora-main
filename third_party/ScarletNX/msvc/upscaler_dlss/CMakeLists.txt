# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
# DLSS's adapter, a module of its own (src/nx/gpu/vk/upscaler.h): snx_upscaler_dlss.dll, built with Microsoft's
# compiler against the builder's own DLSS SDK, which is never in this repository. Given: SNX_DLSS_SDK_DIR (the SDK's
# root, with include/ and lib/) and SNX_RUNTIME_SRC (third_party/ScarletNX). Without the SDK nothing is built. The
# built module holds NVIDIA's static library: it stays on the machine that built it.
cmake_minimum_required(VERSION 3.24)
project(snx_upscaler_dlss LANGUAGES CXX)
if(NOT MSVC)
    message(FATAL_ERROR "this module is built with Microsoft's compiler only")
endif()
if(NOT SNX_DLSS_SDK_DIR)
    message(STATUS "snx_upscaler_dlss: no SNX_DLSS_SDK_DIR, nothing is built")
    return()
endif()
if(NOT SNX_RUNTIME_SRC)
    message(FATAL_ERROR "snx_upscaler_dlss: SNX_RUNTIME_SRC (third_party/ScarletNX) is needed for the interface")
endif()
set(snx_ngx_lib "${SNX_DLSS_SDK_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
foreach(need "${SNX_DLSS_SDK_DIR}/include/nvsdk_ngx_vk.h" "${snx_ngx_lib}")
    if(NOT EXISTS "${need}")
        message(FATAL_ERROR "snx_upscaler_dlss: ${need} is not there")
    endif()
endforeach()

# Vulkan's headers: the archive and hash the runtime pins (cmake/dependencies.cmake), never the SDK's
include(FetchContent)
FetchContent_Declare(snx_vulkan_headers
                     URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.365.tar.gz
                     URL_HASH SHA256=ed832b3292cce324ebba6522fd2d3004bf1b797fa49bfbcad0b302ff23d56abc
                     DOWNLOAD_EXTRACT_TIMESTAMP TRUE
                     EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(snx_vulkan_headers)

add_library(snx_upscaler_dlss MODULE upscaler_dlss.cpp)
set_target_properties(snx_upscaler_dlss PROPERTIES
                      PREFIX ""
                      OUTPUT_NAME snx_upscaler_dlss
                      CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
                      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
                      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
                      MSVC_RUNTIME_LIBRARY "MultiThreaded")
target_include_directories(snx_upscaler_dlss PRIVATE "${SNX_RUNTIME_SRC}/src" "${SNX_DLSS_SDK_DIR}/include")
target_compile_definitions(snx_upscaler_dlss PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
# every warning is an error, and the layout assertions of upscaler.h are compiled here by this compiler
target_compile_options(snx_upscaler_dlss PRIVATE /W4 /WX /utf-8)
# NVIDIA's loader for the static runtime (/MT), never a debug variant, and no vulkan-1: every Vulkan entry comes from
# the lookups the module is handed
target_link_libraries(snx_upscaler_dlss PRIVATE Vulkan::Headers "${snx_ngx_lib}")

# What the module depends on, printed in the build's log. vulkan-1.dll among them fails the build.
get_filename_component(snx_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
find_program(SNX_DUMPBIN dumpbin HINTS "${snx_cl_dir}" REQUIRED)
add_custom_command(TARGET snx_upscaler_dlss POST_BUILD
                   COMMAND "${CMAKE_COMMAND}" "-DDUMPBIN=${SNX_DUMPBIN}" "-DMODULE=$<TARGET_FILE:snx_upscaler_dlss>"
                           -P "${CMAKE_CURRENT_SOURCE_DIR}/dependents.cmake"
                   VERBATIM)
