# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
# svcgen's registers for every SVC in hos.svc, in both ABIs, against svcgen_layout.txt. When svcgen replaced svc.c's
# hand-written 32-bit table, its layout matched that table for each of the 115 SVCs the table listed
#
# usage: cmake -DSVCGEN=<svcgen> -DSPEC=<hos.svc> -DEXPECTED=<svcgen_layout.txt> -P svcgen_layout.cmake
execute_process(COMMAND "${SVCGEN}" --layout "${SPEC}" RESULT_VARIABLE failed OUTPUT_VARIABLE got ERROR_VARIABLE err)
if(failed)
    message(FATAL_ERROR "svcgen refused ${SPEC}: ${err}")
endif()
file(READ "${EXPECTED}" want)
if(NOT got STREQUAL want)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/svcgen_layout.got" "${got}")
    message(FATAL_ERROR "svcgen's registers differ from ${EXPECTED}: see ${CMAKE_CURRENT_BINARY_DIR}/svcgen_layout.got")
endif()
message(STATUS "svcgen_layout passed")
