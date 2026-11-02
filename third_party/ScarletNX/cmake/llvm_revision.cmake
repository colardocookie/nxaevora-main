# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
# LLVM's revision as llvm/Support/VCSRevision.h under one of include_dirs names it, into out: "" when the header is not
# there or defines none. The header is C++ (newer LLVMs write the revision as a raw string), so the C that keys on it
# (src/nx/loader/unit_cache.c) takes it from here. A define it cannot read stops the configure, naming the line
function(snx_llvm_revision out include_dirs)
    set(revision "")
    foreach(dir ${include_dirs})
        set(header "${dir}/llvm/Support/VCSRevision.h")
        if(revision OR NOT EXISTS "${header}")
            continue()
        endif()
        file(STRINGS "${header}" line REGEX "^#define LLVM_REVISION ")
        if(NOT line)
            continue()
        endif()
        if(NOT line MATCHES "^#define LLVM_REVISION (R\"\\(([^)\"]+)\\)\"|\"([^\"]+)\")$")
            message(FATAL_ERROR "LLVM's revision cannot be read from ${header}: ${line}")
        endif()
        set(revision "${CMAKE_MATCH_2}${CMAKE_MATCH_3}")
    endforeach()
    set(${out} "${revision}" PARENT_SCOPE)
endfunction()
