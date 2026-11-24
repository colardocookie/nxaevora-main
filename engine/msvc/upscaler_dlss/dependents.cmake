# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
# After the module is linked: the libraries it depends on, by dumpbin /dependents, into the build's log. One named
# vulkan-1.dll fails the build: the module must take Vulkan only from the lookups it is handed. Given: DUMPBIN, MODULE.
execute_process(COMMAND "${DUMPBIN}" /nologo /dependents "${MODULE}" OUTPUT_VARIABLE listed RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "snx_upscaler_dlss: dumpbin /dependents failed (${result})")
endif()
# only the section of the dependencies: the lines before it name the module's own file
string(FIND "${listed}" "following dependencies:" from)
if(from LESS 0)
    message(FATAL_ERROR "snx_upscaler_dlss: dumpbin /dependents printed no list of dependencies")
endif()
string(SUBSTRING "${listed}" ${from} -1 section)
string(FIND "${section}" "Summary" to)
if(to GREATER 0)
    string(SUBSTRING "${section}" 0 ${to} section)
endif()
string(REGEX MATCHALL "[A-Za-z0-9_.-]+\\.[dD][lL][lL]" names "${section}")
list(REMOVE_DUPLICATES names)
foreach(name IN LISTS names)
    message(STATUS "snx_upscaler_dlss depends on: ${name}")
endforeach()
string(TOLOWER "${names}" lower)
if(lower MATCHES "(^|;)vulkan-1\\.dll(;|$)")
    message(FATAL_ERROR "snx_upscaler_dlss: the module imports vulkan-1.dll. It must take Vulkan from its lookups only")
endif()
