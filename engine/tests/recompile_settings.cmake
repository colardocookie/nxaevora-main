# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
# scarletnx -r --KEY=VALUE, a setting of the recompile (settings.txt's recompile and llvm tables): a value the setting
# does not take is refused before anything runs, saying what it takes; one it takes reaches the input's check. Run by
# ctest as runtime_recompile_settings
set(dir "${CMAKE_CURRENT_BINARY_DIR}/recompile-settings")
file(REMOVE_RECURSE "${dir}")
file(MAKE_DIRECTORY "${dir}")
# a bad option or a bad --KEY=VALUE is exit 2 (SNX_EXIT_USAGE), a missing exefs/main is exit 3 (SNX_EXIT_GAME_FILES)
foreach(case "--aot_threads=abc|2|aot_threads=abc is not a whole number from 0 to 1024"
             "--aot_threads=1.5|2|aot_threads=1.5 is not a whole number from 0 to 1024"
             "--aot_threads=2000|2|aot_threads=2000 is not a whole number from 0 to 1024"
             "--aot_threads=|2|aot_threads= is not a whole number from 0 to 1024"
             "--llvm_inliner_threshold=50x|2|llvm_inliner_threshold=50x is not a whole number from -2147483647 to 2147483647"
             "--opt=5|2|opt = 5: not 0, 1, 2, 3, s or z"
             "--unit_cache_gb=-1|2|unit_cache_gb=-1 is not a whole number from 0 to 100000"
             "--unit_cache_gb=2.5|2|unit_cache_gb=2.5 is not a whole number from 0 to 100000"
             "--aot_threads=4|3|holds no exefs/main" "--llvm_inliner_threshold=50|3|holds no exefs/main"
             "--opt=z|3|holds no exefs/main" "--unit_cache_gb=0|3|holds no exefs/main")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 option)
    list(GET fields 1 expected_code)
    list(GET fields 2 expected)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY SDL_VIDEO_DRIVER=dummy
                            "${RUNTIME}" -r missing.nro ${option}
                    WORKING_DIRECTORY "${dir}" TIMEOUT 20 RESULT_VARIABLE result OUTPUT_VARIABLE out
                    ERROR_VARIABLE out)
    string(FIND "${out}" "${expected}" at)
    if(NOT result EQUAL expected_code OR at LESS 0)
        file(REMOVE_RECURSE "${dir}")
        message(FATAL_ERROR "scarletnx -r missing.nro ${option} ended with ${result}, not ${expected_code} with \"${expected}\":\n${out}")
    endif()
endforeach()
# AOT pack build: a prepared game, recompiled with the cache the launcher names. --jit-cache and
# --hook-direct-calls are the recompiler's, not extra dump inputs; before this case the automatic
# preparation check took them for game arguments and stopped with "accepts one dump file".
set(aot "${dir}/aot")
set(aot_game "${aot}/0100000000000000")
file(MAKE_DIRECTORY "${aot_game}" "${aot_game}/0100000000000000_data")
file(WRITE "${aot_game}/0100000000000000" "")
file(WRITE "${aot}/profile.jitcache" "")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY SDL_VIDEO_DRIVER=dummy
                        "${RUNTIME}" --repo "${aot}" --build-dir "${aot}" -r "${aot_game}"
                        --jit-cache "${aot}/profile.jitcache" --hook-direct-calls -O 2
                WORKING_DIRECTORY "${aot}" TIMEOUT 20 RESULT_VARIABLE result OUTPUT_VARIABLE out
                ERROR_VARIABLE out)
string(FIND "${out}" "automatic preparation accepts one dump file" bad)
if(bad GREATER -1)
    message(FATAL_ERROR "AOT build's --jit-cache/--hook-direct-calls were taken for extra dumps:\n${out}")
endif()
string(FIND "${out}" "holds no exefs/main" reached)
if(reached LESS 0 OR NOT result EQUAL 3)
    message(FATAL_ERROR "AOT build did not reach the recompiler with exit 3 (ended with ${result}):\n${out}")
endif()
file(REMOVE_RECURSE "${dir}")
