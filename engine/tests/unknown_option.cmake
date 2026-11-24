# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
# A --key=value that no table of runtime/options.c knows is refused before anything runs; a known one reaches the
# input's check. Run by ctest as runtime_unknown_option
set(dir "${CMAKE_CURRENT_BINARY_DIR}/unknown-option")
file(REMOVE_RECURSE "${dir}")
file(MAKE_DIRECTORY "${dir}")
foreach(option --resoltion=2 --render_resolution=2 --seconds=5)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY SDL_VIDEO_DRIVER=dummy
                            "${RUNTIME}" ${option} missing.nro
                    WORKING_DIRECTORY "${dir}" TIMEOUT 20 RESULT_VARIABLE result OUTPUT_VARIABLE out
                    ERROR_VARIABLE out)
    if(option STREQUAL "--resoltion=2")
        set(expected "unknown option: --resoltion=2")
        set(expected_code 2)
    else()
        set(expected "missing.nro is not a file")
        set(expected_code 3)
    endif()
    string(FIND "${out}" "${expected}" at)
    if(NOT result EQUAL expected_code OR at LESS 0)
        file(REMOVE_RECURSE "${dir}")
        message(FATAL_ERROR "scarletnx ${option} missing.nro ended with ${result}, not ${expected_code} with \"${expected}\":\n${out}")
    endif()
endforeach()
# an option nothing takes is refused by its name, with no game given too; an option's older name is said once, with
# its name now, and taken as that. The codes are the exit-code table: 2 for an unknown option, 0 for -r --help
foreach(case "--bogus|2|unknown option: --bogus (nxaevora-engine --help lists them)"
             "--settings=bogus|2|--settings=bogus: not --settings (the game's) or --settings=recompile"
             "--recomp --recomp --help|0|--recomp is --recompile now: this run takes it as --recompile")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 args)
    list(GET fields 1 expected_code)
    list(GET fields 2 expected)
    separate_arguments(args_list UNIX_COMMAND "${args}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY SDL_VIDEO_DRIVER=dummy
                            "${RUNTIME}" ${args_list}
                    WORKING_DIRECTORY "${dir}" TIMEOUT 20 RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
    string(FIND "${out}" "${expected}" at)
    string(REPLACE "${expected}" "" rest "${out}")
    string(FIND "${rest}" "is --recompile now" again)
    if(NOT result EQUAL expected_code OR at LESS 0 OR NOT again LESS 0)
        file(REMOVE_RECURSE "${dir}")
        message(FATAL_ERROR "scarletnx ${args} ended with ${result}, not ${expected_code}, and did not say \"${expected}\" once:\n${out}")
    endif()
endforeach()
file(REMOVE_RECURSE "${dir}")
