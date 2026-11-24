# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
# The stand-in modules of tests/upscaler_boundary once more, as a project of its own for Microsoft's compiler:
# the runtime is built with llvm-mingw, and a module such as DLSS's has to be built with Microsoft's. The parent
# build configures this with a Visual Studio generator when it finds one (tests/CMakeLists.txt). Given: SNX_SOURCE
# (the runtime's src folder) and SNX_VULKAN_INCLUDE (Vulkan's headers).
cmake_minimum_required(VERSION 3.20)
project(snx_upscaler_standin_msvc LANGUAGES CXX)
if(NOT MSVC)
    message(FATAL_ERROR "this project is for Microsoft's compiler only")
endif()
foreach(kind 0 1 2 3 4)
    add_library(snx-upscaler-standin-msvc-${kind} MODULE "${CMAKE_CURRENT_SOURCE_DIR}/../standin.cpp")
    set_target_properties(snx-upscaler-standin-msvc-${kind} PROPERTIES
                          CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
                          RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
                          LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
                          LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/out"
                          RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/out"
                          MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_compile_definitions(snx-upscaler-standin-msvc-${kind} PRIVATE SNX_STANDIN=${kind})
    target_include_directories(snx-upscaler-standin-msvc-${kind} PRIVATE "${SNX_SOURCE}" "${SNX_VULKAN_INCLUDE}"
                                                                         "${CMAKE_CURRENT_SOURCE_DIR}/..")
    # every warning is an error, and the layout assertions of upscaler.h are compiled here by this compiler
    target_compile_options(snx-upscaler-standin-msvc-${kind} PRIVATE /W4 /WX /utf-8)
endforeach()
# NGX's lifetimes in DLSS's module (msvc/upscaler_dlss/dlss_core.h) against a stand-in NGX, by this compiler too
add_executable(snx-dlss-core-msvc "${CMAKE_CURRENT_SOURCE_DIR}/../dlss_core.cpp"
               "${CMAKE_CURRENT_SOURCE_DIR}/../dlss_eval.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../dlss_record.cpp")
set_target_properties(snx-dlss-core-msvc PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
                      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
                      RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/out" MSVC_RUNTIME_LIBRARY "MultiThreaded")
target_include_directories(snx-dlss-core-msvc PRIVATE "${SNX_SOURCE}/../msvc/upscaler_dlss" "${SNX_VULKAN_INCLUDE}")
target_compile_options(snx-dlss-core-msvc PRIVATE /W4 /WX /utf-8)
