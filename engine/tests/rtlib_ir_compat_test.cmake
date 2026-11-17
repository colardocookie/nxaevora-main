# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
# rtlib_ir_compat.cmake on spellings an older llvm-as refuses: what it rewrites, and what it leaves.
# usage: cmake -DSCRIPT=<rtlib_ir_compat.cmake> -DWORK=<folder> -P rtlib_ir_compat_test.cmake
set(in "${WORK}/rtlib_ir_compat_in.ll")
set(out "${WORK}/rtlib_ir_compat_out.ll")
file(WRITE "${in}" [[
attributes #7 = { nofree memory(read, argmem: readwrite, inaccessiblemem: none, target_mem: none) uwtable }
attributes #8 = { memory(target_mem: read, argmem: write) nounwind }
attributes #9 = { memory(target_mem: none) nounwind }
attributes #10 = { memory(argmem: read) nounwind }
define i1 @f(i32 %a) {
  %c = icmp samesign ult i32 %a, 7
  ret i1 %c
}
]])
execute_process(COMMAND "${CMAKE_COMMAND}" -DINPUT=${in} -DOUTPUT=${out} -P "${SCRIPT}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "rtlib_ir_compat.cmake exited ${rc}")
endif()
file(READ "${out}" got)
set(want [[
attributes #7 = { nofree memory(read, argmem: readwrite, inaccessiblemem: none) uwtable }
attributes #8 = { memory(argmem: write) nounwind }
attributes #9 = { nounwind }
attributes #10 = { memory(argmem: read) nounwind }
define i1 @f(i32 %a) {
  %c = icmp ult i32 %a, 7
  ret i1 %c
}
]])
if(NOT got STREQUAL want)
    message(FATAL_ERROR "rtlib_ir_compat.cmake wrote:\n${got}\nnot:\n${want}")
endif()
