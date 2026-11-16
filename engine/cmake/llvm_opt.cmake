# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
# The pipelines LLVM's opt prints, for the LLVM settings (settings.cmake, tools/settingsgen): a function, so a test can
# run it on opts of its own (tests/llvm_opt.cmake)
include_guard(GLOBAL)

# snx_llvm_pipelines(OPT VERSION TRIPLE WORK TEXT_VAR WHY_VAR): OPT's pipelines for -O0 to -O3, and O2's for s and z
# (ir.c runs -Os and -Oz as O2 over size-marked functions), as "<level> <pipeline>" lines in TEXT_VAR, for the target
# TRIPLE the backend makes code for: a target adds passes of its own to the pipelines (AArch64's loop-idiom-vectorize),
# which the text would lack without it. OPT must be "LLVM version VERSION" exactly, the LLVM the backend links: another
# LLVM's pipelines name passes this one may not have, or leave out its own. WHY_VAR says what went wrong (OPT not that
# LLVM, a level that printed nothing), empty when all is well; the levels that printed are in TEXT_VAR all the same.
# WORK is a folder for opt's empty input
function(snx_llvm_pipelines opt version triple work text_var why_var)
    set(text "")
    set(why "")
    execute_process(COMMAND "${opt}" --version OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
    string(REGEX REPLACE "([.+*?^$()])" "\\\\\\1" escaped "${version}")
    if(NOT result EQUAL 0)
        set(why "${opt} does not run (${result}): ${error}")
    elseif(NOT output MATCHES "LLVM version ${escaped}[ \r\n]")
        string(REGEX MATCH "LLVM version [^ \r\n]*" found "${output}")
        set(why "${opt} is ${found}, not LLVM version ${version}, the LLVM the backend links: point SNX_LLVM_OPT at "
                "that LLVM's opt")
    else()
        file(WRITE "${work}/empty.ll" "") # opt's input: no module, only the pipelines printed
        foreach(level 0 1 2 3)
            execute_process(COMMAND "${opt}" "-mtriple=${triple}" "-passes=default<O${level}>" -print-pipeline-passes
                                    -disable-output
                            INPUT_FILE "${work}/empty.ll" OUTPUT_VARIABLE pipeline ERROR_VARIABLE error
                            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result)
            if(result EQUAL 0 AND NOT pipeline STREQUAL "")
                string(APPEND text "${level} ${pipeline}\n")
                if(level STREQUAL "2")
                    string(APPEND text "s ${pipeline}\nz ${pipeline}\n")
                endif()
            else()
                string(APPEND why "${opt} printed no pipeline for -O${level}, whose passes LLVM Internals can't turn "
                                  "off: ${result} ${error}\n")
            endif()
        endforeach()
    endif()
    set(${text_var} "${text}" PARENT_SCOPE)
    set(${why_var} "${why}" PARENT_SCOPE)
endfunction()
