# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
#
#   cmake -DSRC=<source dir> -DWORK=<scratch dir> -P tests/llvm_revision_test.cmake
#
# LLVM's revision for the unit cache's key (cmake/llvm_revision.cmake): read from a plain string and from the raw string
# newer LLVMs write, "" from a header that undefines it or from none, and a define of another form refused
foreach(var SRC WORK)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "llvm_revision_test.cmake needs -D${var}=...")
    endif()
endforeach()
include("${SRC}/cmake/llvm_revision.cmake")
file(REMOVE_RECURSE "${WORK}")

function(header name text)
    file(WRITE "${WORK}/${name}/llvm/Support/VCSRevision.h" "${text}")
endfunction()
function(expect dirs want)
    snx_llvm_revision(got "${dirs}")
    if(NOT got STREQUAL want)
        message(FATAL_ERROR "llvm_revision: ${dirs} gave \"${got}\", not \"${want}\"")
    endif()
endfunction()

header(plain "#define LLVM_REVISION \"5378f7c7cabdd5ba17b2b23d1dd536d40633e37f\"\n#define LLVM_REPOSITORY \"x\"\n")
header(raw "#define LLVM_REVISION R\"(9a1b2c3d)\"\n")
header(undefined "#undef LLVM_REVISION\n")
expect("${WORK}/plain" 5378f7c7cabdd5ba17b2b23d1dd536d40633e37f)
expect("${WORK}/raw" 9a1b2c3d)
expect("${WORK}/undefined" "")
expect("${WORK}/none" "")
expect("${WORK}/none;${WORK}/undefined;${WORK}/raw" 9a1b2c3d) # the first include dir that names one
expect("${WORK}/plain;${WORK}/raw" 5378f7c7cabdd5ba17b2b23d1dd536d40633e37f) # as the compiler finds the header

# a form it cannot read stops the configure: run apart, as a FATAL_ERROR ends this script
header(other "#define LLVM_REVISION LLVM_GIT_HASH\n")
file(WRITE "${WORK}/other.cmake" "include(\"${SRC}/cmake/llvm_revision.cmake\")\nsnx_llvm_revision(r \"${WORK}/other\")\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -P "${WORK}/other.cmake" RESULT_VARIABLE result ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT error MATCHES "LLVM's revision cannot be read from.*#define LLVM_REVISION LLVM_GIT_HASH")
    message(FATAL_ERROR "llvm_revision: an unreadable define gave ${result}: ${error}")
endif()
message(STATUS "llvm_revision: ok")
