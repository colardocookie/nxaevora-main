# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
if(WIN32)
    set(gui_process process_win32.c)
elseif(CMAKE_SYSTEM_NAME MATCHES "^(Linux|FreeBSD)$")
    set(gui_process process_posix.c)
else()
    message(FATAL_ERROR "scarletnx-gui currently supports Linux, FreeBSD and Windows")
endif()
include("${PROJECT_SOURCE_DIR}/cmake/gui_themes.cmake")
# The menu's languages (i18n.h): snx_tr and the catalogs' reader, strict ISO C99 as the build info; each po/*.po a
# catalog compiled in, checked and made by scripts/build-languages.py against po/scarletnx.pot
add_library(snx_i18n STATIC i18n.c)
set_target_properties(snx_i18n PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_options(snx_i18n PRIVATE -pedantic-errors -Wall -Wextra -Werror=missing-prototypes)
target_link_libraries(snx_i18n PUBLIC scarletnx-common) # the tags' fit (common/utilities/language_tag.h)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# target, a library of out made from the .po files of folder
function(snx_gui_languages target folder out)
    file(GLOB pos CONFIGURE_DEPENDS "${folder}/*.po")
    add_custom_command(OUTPUT "${out}"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/build-languages.py" --out "${out}" ${pos}
        DEPENDS "${PROJECT_SOURCE_DIR}/scripts/build-languages.py" "${PROJECT_SOURCE_DIR}/po/scarletnx.pot" ${pos}
                "${PROJECT_SOURCE_DIR}/scripts/build-glyphs.py" "${CMAKE_CURRENT_SOURCE_DIR}/unifont-latin.hex"
                "${CMAKE_CURRENT_SOURCE_DIR}/unifont-wide.hex" "${PROJECT_SOURCE_DIR}/src/common/utilities/font.c"
        COMMENT "The menu's languages from ${folder}" VERBATIM)
    add_library(${target} STATIC "${out}")
endfunction()
snx_gui_languages(snx_gui_languages "${PROJECT_SOURCE_DIR}/po" "${PROJECT_BINARY_DIR}/generated/gui_languages.c")
# The letters past the VGA font's CP437: GNU Unifont's, the Latin ones (latin.h) kept in unifont-latin.hex and the wide
# ones the languages show (wide.h) in unifont-wide.hex
add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/generated/gui_glyphs.c"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/build-glyphs.py"
            --out "${PROJECT_BINARY_DIR}/generated/gui_glyphs.c"
    DEPENDS "${PROJECT_SOURCE_DIR}/scripts/build-glyphs.py" "${CMAKE_CURRENT_SOURCE_DIR}/unifont-latin.hex"
            "${CMAKE_CURRENT_SOURCE_DIR}/unifont-wide.hex"
    COMMENT "The menu's Latin and wide glyphs" VERBATIM)
# the launcher's scenes, generated from ui.txt by tools/uigen: gui/ui.h and gui/ui.c, whose drawing and callbacks
# main.c defines, so they go into what compiles main.c
include("${PROJECT_SOURCE_DIR}/cmake/ui.cmake")
add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/generated/gui/ui.h" "${PROJECT_BINARY_DIR}/generated/gui/ui.c"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_BINARY_DIR}/generated/gui"
    COMMAND snx-uigen --out "${PROJECT_BINARY_DIR}/generated" "${CMAKE_CURRENT_SOURCE_DIR}/ui.txt"
    DEPENDS snx-uigen "${CMAKE_CURRENT_SOURCE_DIR}/ui.txt"
    COMMENT "Generating the launcher's scenes from src/gui/ui.txt" VERBATIM)
add_library(snx_gui_ui OBJECT "${PROJECT_BINARY_DIR}/generated/gui/ui.c")
target_include_directories(snx_gui_ui PUBLIC "${PROJECT_BINARY_DIR}/generated")
target_link_libraries(snx_gui_ui PRIVATE SDL3::SDL3) # its platform macros and keycodes
set_target_properties(snx_gui_ui PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(snx_gui_ui PRIVATE -Wall -Wextra -Werror=missing-prototypes)
add_library(snx_gui_support STATIC aot.c model.c browser.c steps.c theme.c recent.c editor.c settings.c place.c clean.c
            queue.c latin.c wide.c log.c logzip.c ${gui_process}
            "${PROJECT_BINARY_DIR}/generated/gui_themes.c" "${PROJECT_BINARY_DIR}/generated/gui_glyphs.c")
target_link_libraries(snx_gui_support PUBLIC scarletnx-common snx_setup snx_i18n snx_settings snx_miniz)
target_include_directories(snx_gui_support PRIVATE "${PROJECT_BINARY_DIR}/generated")
set_target_properties(snx_gui_support PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(snx_gui_support PRIVATE -Wall -Wextra -Werror=missing-prototypes)
if(WIN32)
    target_compile_definitions(snx_gui_support PRIVATE _WIN32_WINNT=0x0602 _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(snx_gui_support PUBLIC psapi)
endif()
include("${PROJECT_SOURCE_DIR}/cmake/window_icon.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/build_info.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/pad_sdl.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/splash.cmake")
# the menu's pads, their presses its keys (pad_menu.h)
add_library(snx_gui_pad STATIC pad_menu.c)
target_link_libraries(snx_gui_pad PUBLIC scarletnx-common snx_pad_sdl SDL3::SDL3)
set_target_properties(snx_gui_pad PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(snx_gui_pad PRIVATE -Wall -Wextra -Werror=missing-prototypes)
include(GNUInstallDirs)
# The menu: scarletnx, the Switch runtime, links it and opens it when started with nothing to run (#243); a build
# without that runtime makes it a program of its own, beside the other runtimes
if(SNX_BUILD_NX)
    add_library(scarletnx-gui STATIC main.c)
    target_compile_definitions(scarletnx-gui PRIVATE SNX_GUI_NO_MAIN)
else()
    add_executable(scarletnx-gui main.c)
    install(TARGETS scarletnx-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(NOT WIN32)
        install(FILES "${PROJECT_SOURCE_DIR}/dist/scarletnx-gui.desktop"
                DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT desktop)
    endif()
endif()
set_target_properties(scarletnx-gui PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_compile_options(scarletnx-gui PRIVATE -Wall -Wextra -Werror=missing-prototypes)
target_compile_definitions(scarletnx-gui PRIVATE SNX_SOURCE_DIR="${PROJECT_SOURCE_DIR}" SNX_BUILD_DIR="${PROJECT_BINARY_DIR}")
target_link_libraries(scarletnx-gui PUBLIC snx_gui_support PRIVATE snx_gui_ui scarletnx-common SDL3::SDL3
                      snx_window_icon snx_build_info snx_pad_sdl snx_gui_pad snx_splash snx_gui_languages snx_demangle)
if(WIN32 AND NOT SNX_BUILD_NX)
    enable_language(RC)
    target_sources(scarletnx-gui PRIVATE ../common/platform/runtime.rc)
    # where the resource compiler finds the manifest and the icon; C includes from src/ alone
    target_include_directories(scarletnx-gui PRIVATE
                               "$<$<COMPILE_LANGUAGE:RC>:${CMAKE_CURRENT_SOURCE_DIR}/../common/platform>")
    target_link_options(scarletnx-gui PRIVATE -mwindows -static)
endif()
if(SNX_SDL3_FROM_SOURCE AND NOT SNX_BUILD_NX)
    install(FILES "${SDL3_SOURCE_DIR}/LICENSE.txt" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/scarletnx/SDL3)
endif()
install(FILES FONT-NOTICE.txt MUSL-NOTICE.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/scarletnx)
# The built-in themes as files to copy and edit: beside the GUI in the build, in share/scarletnx installed, where it
# looks for them
file(GLOB_RECURSE gui_themes RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS themes/*/theme.ini)
foreach(theme IN LISTS gui_themes)
    configure_file(${theme} "${PROJECT_BINARY_DIR}/${theme}" COPYONLY)
endforeach()
install(DIRECTORY themes DESTINATION ${CMAKE_INSTALL_DATADIR}/scarletnx)
if(BUILD_TESTING)
    add_executable(snx-test-gui-model "${PROJECT_SOURCE_DIR}/tests/gui_model.c")
    target_link_libraries(snx-test-gui-model PRIVATE snx_gui_support)
    add_test(NAME gui_model COMMAND snx-test-gui-model)
    # what tools/uigen makes of tests/ui_test.txt, built and run: layouts, drawing, hit tests, actions, hotkeys
    set(gui_ui_test "${CMAKE_CURRENT_BINARY_DIR}/uitest")
    add_custom_command(OUTPUT "${gui_ui_test}/gui/ui.h" "${gui_ui_test}/gui/ui.c"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${gui_ui_test}/gui"
        COMMAND snx-uigen --out "${gui_ui_test}" "${PROJECT_SOURCE_DIR}/tests/ui_test.txt"
        DEPENDS snx-uigen "${PROJECT_SOURCE_DIR}/tests/ui_test.txt"
        COMMENT "Generating tests/ui_test.txt's scenes" VERBATIM)
    add_executable(snx-test-gui-ui "${PROJECT_SOURCE_DIR}/tests/gui_ui.c" "${gui_ui_test}/gui/ui.c")
    target_include_directories(snx-test-gui-ui PRIVATE "${gui_ui_test}")
    target_link_libraries(snx-test-gui-ui PRIVATE snx_gui_support SDL3::SDL3) # SDL3: its keycodes' header
    add_test(NAME gui_ui COMMAND snx-test-gui-ui)
    add_executable(snx-test-gui-recent "${PROJECT_SOURCE_DIR}/tests/gui_recent.c")
    target_link_libraries(snx-test-gui-recent PRIVATE snx_gui_support scarletnx-common)
    add_test(NAME gui_recent COMMAND snx-test-gui-recent)
    add_executable(snx-test-gui-queue "${PROJECT_SOURCE_DIR}/tests/gui_queue.c")
    target_link_libraries(snx-test-gui-queue PRIVATE snx_gui_support)
    add_test(NAME gui_queue COMMAND snx-test-gui-queue)
    add_executable(snx-test-gui-pad "${PROJECT_SOURCE_DIR}/tests/gui_pad.c")
    target_link_libraries(snx-test-gui-pad PRIVATE snx_gui_pad)
    add_test(NAME gui_pad COMMAND snx-test-gui-pad)
    add_executable(snx-test-gui-steps "${PROJECT_SOURCE_DIR}/tests/gui_steps.c")
    target_link_libraries(snx-test-gui-steps PRIVATE snx_gui_support)
    add_test(NAME gui_steps COMMAND snx-test-gui-steps)
    add_executable(snx-test-gui-theme "${PROJECT_SOURCE_DIR}/tests/gui_theme.c")
    target_link_libraries(snx-test-gui-theme PRIVATE snx_gui_support scarletnx-common)
    target_compile_definitions(snx-test-gui-theme PRIVATE SNX_GUI_THEMES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/themes")
    target_compile_definitions(snx-test-gui-theme PRIVATE SNX_GUI_DOCS="${PROJECT_SOURCE_DIR}/docs/GUI.md")
    target_include_directories(snx-test-gui-theme PRIVATE "${PROJECT_BINARY_DIR}/generated")
    add_test(NAME gui_theme COMMAND snx-test-gui-theme)
    add_executable(snx-test-gui-theme-write "${PROJECT_SOURCE_DIR}/tests/gui_theme_write.c")
    target_link_libraries(snx-test-gui-theme-write PRIVATE snx_gui_support scarletnx-common)
    add_test(NAME gui_theme_write COMMAND snx-test-gui-theme-write)
    add_executable(snx-test-gui-editor "${PROJECT_SOURCE_DIR}/tests/gui_editor.c")
    target_link_libraries(snx-test-gui-editor PRIVATE snx_gui_support scarletnx-common)
    add_test(NAME gui_editor COMMAND snx-test-gui-editor)
    add_executable(snx-test-gui-clean "${PROJECT_SOURCE_DIR}/tests/gui_clean.c")
    target_link_libraries(snx-test-gui-clean PRIVATE snx_gui_support scarletnx-common)
    add_test(NAME gui_clean COMMAND snx-test-gui-clean)
    add_executable(snx-test-gui-settings "${PROJECT_SOURCE_DIR}/tests/gui_settings.c")
    target_link_libraries(snx-test-gui-settings PRIVATE snx_gui_support scarletnx-common)
    if(SNX_BUILD_NX) # and the runtime's own list
        add_test(NAME gui_settings COMMAND snx-test-gui-settings $<TARGET_FILE:scarletnx>)
    else()
        add_test(NAME gui_settings COMMAND snx-test-gui-settings)
    endif()
    add_executable(snx-test-gui-i18n "${PROJECT_SOURCE_DIR}/tests/gui_i18n.c")
    target_link_libraries(snx-test-gui-i18n PRIVATE snx_i18n snx_gui_languages_test)
    add_test(NAME gui_i18n COMMAND snx-test-gui-i18n)
    # po/scarletnx.pot is the code's phrases, and every po/*.po compiles; the script refuses what it should
    add_test(NAME gui_languages_pot COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/build-languages.py"
             --check)
    add_test(NAME gui_languages_script COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/languages_test.py")
    if(NOT CMAKE_CROSSCOMPILING)
        # tools/uigen on a ui.txt of its own: what it generates, and each file it refuses, at its line
        add_test(NAME uigen COMMAND "${CMAKE_COMMAND}" "-DUIGEN=$<TARGET_FILE:snx-uigen>"
                 "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/uigen-test" -P "${PROJECT_SOURCE_DIR}/tests/uigen.cmake")
    endif()
    # unifont-latin.hex holds the Latin letters CP437 lacks, unifont-wide.hex the wide ones the languages show, each
    # once, in order
    add_test(NAME gui_glyphs_kept COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/build-glyphs.py" --check)
    add_test(NAME gui_glyphs_script COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/glyphs_test.py")
    add_executable(snx-test-gui-log "${PROJECT_SOURCE_DIR}/tests/gui_log.c")
    target_link_libraries(snx-test-gui-log PRIVATE snx_gui_support scarletnx-common)
    add_test(NAME gui_log COMMAND snx-test-gui-log)
    add_executable(snx-test-gui-logzip "${PROJECT_SOURCE_DIR}/tests/gui_logzip.c")
    target_link_libraries(snx-test-gui-logzip PRIVATE snx_gui_support scarletnx-common)
    add_test(NAME gui_logzip COMMAND snx-test-gui-logzip)
    # cmake/gui_icons.cmake compiles icons.hex, and refuses a glyph missing, blank or out of its place by its icon
    add_test(NAME gui_icons COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${PROJECT_SOURCE_DIR}"
             "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/gui-icons-test" -P "${PROJECT_SOURCE_DIR}/tests/gui_icons.cmake")
    add_executable(snx-test-gui-process "${PROJECT_SOURCE_DIR}/tests/gui_process.c")
    target_link_libraries(snx-test-gui-process PRIVATE snx_gui_support scarletnx-common)
    add_test(NAME gui_process_tree COMMAND snx-test-gui-process)
    # qemu-user answers /proc/<pid>/stat for its own process with no resident size, which the test measures
    set_tests_properties(gui_process_tree PROPERTIES TIMEOUT 30)
    if(CMAKE_CROSSCOMPILING_EMULATOR)
        set_tests_properties(gui_process_tree PROPERTIES DISABLED TRUE)
    endif()
    add_executable(snx-test-gui-sdl "${PROJECT_SOURCE_DIR}/tests/gui_sdl.c")
    if(WIN32) # a crash there says where it was, not only ctest's SegFault
        target_sources(snx-test-gui-sdl PRIVATE "${PROJECT_SOURCE_DIR}/tests/win32_crash.c")
    endif()
    # a language of its own (tests/po), not the menu's: the screens' checks hold whatever po/ has
    snx_gui_languages(snx_gui_languages_test "${PROJECT_SOURCE_DIR}/tests/po"
                      "${PROJECT_BINARY_DIR}/generated/gui_languages_test.c")
    # ui.txt read back: its phones' rows a finger's, as main.c's GUI_FINGER_ROW
    target_compile_definitions(snx-test-gui-sdl PRIVATE SNX_GUI_UI_TXT="${PROJECT_SOURCE_DIR}/src/gui/ui.txt")
    target_link_libraries(snx-test-gui-sdl PRIVATE snx_gui_support snx_gui_ui scarletnx-common SDL3::SDL3
                          snx_window_icon snx_build_info snx_pad_sdl snx_gui_pad snx_splash snx_gui_languages_test snx_demangle)
    # the runtime's sections and pages, each of which has an icon in the Settings
    target_compile_definitions(snx-test-gui-sdl PRIVATE
                               SNX_GUI_SETTINGS_TXT="${PROJECT_SOURCE_DIR}/src/nx/runtime/settings.txt")
    # TODO: fix this
    add_test(NAME gui_sdl_events COMMAND snx-test-gui-sdl)
    # no file picker of the desktop's: the portal would open one on the screen, DBus being there without a display
    set_tests_properties(gui_sdl_events PROPERTIES TIMEOUT 30
                         ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software;SDL_FILE_DIALOG_DRIVER=none;SDL_PREFERRED_LOCALES=en_US")
    # the setup's pages (gui_setup.c), as gui_sdl's: the real renderer, a language of the tests' own, no picker
    add_executable(snx-test-gui-setup "${PROJECT_SOURCE_DIR}/tests/gui_setup.c")
    target_link_libraries(snx-test-gui-setup PRIVATE snx_gui_support snx_gui_ui scarletnx-common SDL3::SDL3
                          snx_window_icon snx_build_info snx_pad_sdl snx_gui_pad snx_splash snx_gui_languages_test snx_demangle)
    add_test(NAME gui_setup COMMAND snx-test-gui-setup)
    set_tests_properties(gui_setup PROPERTIES TIMEOUT 30
                         ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software;SDL_FILE_DIALOG_DRIVER=none;SDL_PREFERRED_LOCALES=en_US")
    # every test here, as snx_test_target does: warnings are errors, the asserts stay in any build type, and the
    # sanitizers come with SNX_TEST_SANITIZERS
    get_directory_property(gui_tests BUILDSYSTEM_TARGETS)
    list(FILTER gui_tests INCLUDE REGEX "^snx-test-")
    foreach(test IN LISTS gui_tests)
        target_compile_options(${test} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    endforeach()
    snx_test_sanitize(${gui_tests})
endif()
if(COMMAND snx_sanitizer_path) # tests/CMakeLists.txt: this folder's tests
    snx_sanitizer_path()
endif()
