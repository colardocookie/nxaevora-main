# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
# nxaevora-engine, the program, wherever the build has a driver: its main (main.c), the only one, and every driver the build
# has, each a library of its own folder, linked by name (the folders are configured after this one), run by the name the
# program is started under or by that name as its first argument. On Linux each driver's name is a link to it
# beside it. Windows has none: its launcher runs a driver with its name first, and its build folder alone has hard
# links, for the tests and a developer's shell.
add_executable(scarletnx main.c)
# the program file of every build
set_target_properties(scarletnx PROPERTIES OUTPUT_NAME nxaevora-engine)
# linked as C++: VMA, LLVM and the demangler need the C++ runtime. It goes in the build folder itself, where the
# driver and CI look for it
set_target_properties(scarletnx PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON LINKER_LANGUAGE CXX
                      RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}" LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_compile_options(scarletnx PRIVATE -Wall -Wextra -Werror=missing-prototypes)

# The drivers, each as its folder is configured (the top-level CMakeLists.txt): main.c's SNX_MULTICALL_<id>, the
# library, the program's name for it, and the variable of cmake/deploy.cmake whose games get a copy of scarletnx
set(snx_names "")
set(snx_deployed "")
macro(snx_multicall_driver id library name deploy)
    target_compile_definitions(scarletnx PRIVATE SNX_MULTICALL_${id})
    target_link_libraries(scarletnx PRIVATE ${library})
    list(APPEND snx_names ${name})
    if(NOT "${deploy}" STREQUAL "")
        list(APPEND snx_deployed "-D${deploy}=$<TARGET_FILE:scarletnx>")
    endif()
endmacro()
if(SNX_BUILD_NX)
    # the menu the Switch driver opens (src/nx), told which drivers the program is (main.c)
    if(SNX_BUILD_GUI)
        target_compile_definitions(scarletnx PRIVATE SNX_MULTICALL_GUI)
    endif()
    # a build of the host tools has the Switch's recompile, and no runtime to copy beside its games
    if(SNX_TOOLS_ONLY)
        snx_multicall_driver(SWITCH snx_switch_program nxaevora-engine-switch "")
    else()
        snx_multicall_driver(SWITCH snx_switch_program nxaevora-engine-switch RUNTIME)
    endif()
endif()

if(WIN32)
    # Prefer static toolchain/SDL libraries. LLVM may still use a DLL import library, resolved by cmake/llvm.cmake and
    # bundled by CI. user32 provides message boxes and advapi32 the registry (the log's system line); winmm is loaded at
    # run time, and the static SDL3 brings the libraries it needs
    target_link_libraries(scarletnx PRIVATE user32 advapi32)
    # The application manifest (src/common/platform/runtime.manifest): UTF-8 as the process's code page, long paths,
    # and the icon
    if(CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
        target_link_options(scarletnx PRIVATE
                            "LINKER:/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/src/common/platform/runtime.manifest")
    else()
        enable_language(RC)
        target_sources(scarletnx PRIVATE "${PROJECT_SOURCE_DIR}/src/common/platform/runtime.rc")
        # where the resource compiler finds the manifest and the icon; C includes from src/ alone
        target_include_directories(scarletnx PRIVATE
                                   "$<$<COMPILE_LANGUAGE:RC>:${PROJECT_SOURCE_DIR}/src/common/platform>")
    endif()
    if(NOT SNX_TOOLS_ONLY)
        target_link_options(scarletnx PRIVATE -static -mwindows)
    endif()
else()
    # No -rdynamic: the game module gets the runtime through RtAPI and imports nothing from it, and exporting every
    # global would let a library loaded later (a GPU driver) bind its own symbols to ours by name
    target_link_libraries(scarletnx PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
    # but GDB's two JIT names (gdb_jit.c), which it finds by name: kept where a strip (quick-sharun's, the AppImage's)
    # leaves no .symtab. A shared libLLVM's references to them bind to these already
    target_link_options(scarletnx PRIVATE "LINKER:--export-dynamic-symbol=__jit_debug_descriptor"
                        "LINKER:--export-dynamic-symbol=__jit_debug_register_code")
endif()

# a sanitized build (SNX_TEST_SANITIZERS) is a test build, its runtime instrumented: it doesn't go next to the games.
# Else after each build a copy goes beside every game of a driver it has, so a runtime fix reaches them without
# recompiling
if(SNX_DEPLOY AND SNX_TEST_SANITIZERS AND BUILD_TESTING)
    message(STATUS "SNX_TEST_SANITIZERS: the runtime is not deployed to the games")
elseif(SNX_DEPLOY AND snx_deployed)
    add_custom_command(TARGET scarletnx POST_BUILD
        COMMAND "${CMAKE_COMMAND}" ${snx_deployed} "-DGAMES=${SNX_RECOMPILED_DIR}" "-DWINDOWS=${WIN32}"
                -P "${PROJECT_SOURCE_DIR}/cmake/deploy.cmake"
        VERBATIM)
endif()

include(GNUInstallDirs)
install(TARGETS scarletnx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# each driver's name beside it, in place of the program of that name an older build made in this folder
foreach(name IN LISTS snx_names)
    if(WIN32)
        add_custom_command(TARGET scarletnx POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E rm -f "${PROJECT_BINARY_DIR}/${name}.exe"
            COMMAND "${CMAKE_COMMAND}" -E create_hardlink "$<TARGET_FILE:scarletnx>"
                    "${PROJECT_BINARY_DIR}/${name}.exe"
            VERBATIM)
    else()
        add_custom_command(TARGET scarletnx POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E rm -f "${PROJECT_BINARY_DIR}/${name}"
            COMMAND "${CMAKE_COMMAND}" -E create_symlink "$<TARGET_FILE_NAME:scarletnx>"
                    "${PROJECT_BINARY_DIR}/${name}"
            VERBATIM)
        install(PROGRAMS "${PROJECT_BINARY_DIR}/${name}" DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endforeach()
