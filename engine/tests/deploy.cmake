# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
# cmake/deploy.cmake copies the runtime beside each game of SNX_RECOMPILED_DIR, else of the data folder a build tree's
# driver uses (snx_data_dir): a .nxaevora/ beside the runtime, else the user's ($XDG_DATA_HOME/nxaevora, a relative
# one ignored). Without a home folder it deploys nothing and says so. Run by ctest as runtime_deploy, with no
# runtime: any file is copied the same way
set(dir "${CMAKE_CURRENT_BINARY_DIR}/runtime-deploy")
set(script "${CMAKE_CURRENT_LIST_DIR}/../cmake/deploy.cmake")
file(REMOVE_RECURSE "${dir}")
file(WRITE "${dir}/bin/scarletnx" "runtime")
# a recompiled game in games, with its update's module beside its own, and another in a folder whose name a glob
# would read as a pattern
function(game games)
    file(WRITE "${games}/Game/Game_code.so" "")
    file(WRITE "${games}/Game/Game_01000000000AB800_code.so" "")
    file(WRITE "${games}/Game [JP]/Game [JP]_code.so" "")
endfunction()
# deploy.cmake with SNX_RECOMPILED_DIR games and WINDOWS=windows, in an environment without HOME and XDG_DATA_HOME
# but for ARGN's NAME=VALUE
function(deploy games windows)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=XDG_DATA_HOME --unset=HOME ${ARGN}
                            "${CMAKE_COMMAND}" "-DRUNTIME=${dir}/bin/scarletnx" "-DGAMES=${games}" "-DWINDOWS=${windows}"
                            -P "${script}"
                    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
    set(result "${result}" PARENT_SCOPE)
    set(out "${out}" PARENT_SCOPE)
endfunction()
function(deployed games what)
    if(NOT result EQUAL 0 OR NOT EXISTS "${games}/Game/Game" OR NOT EXISTS "${games}/Game/Game_01000000000AB800"
       OR NOT EXISTS "${games}/Game [JP]/Game [JP]")
        file(REMOVE_RECURSE "${dir}")
        message(FATAL_ERROR "${what}: the runtime is not beside each game module in ${games} (${result}):\n${out}")
    endif()
endfunction()

game("${dir}/named")
deploy("${dir}/named" 0)
deployed("${dir}/named" "SNX_RECOMPILED_DIR")
game("${dir}/na[me]d*")
deploy("${dir}/na[me]d*" 0)
deployed("${dir}/na[me]d*" "an SNX_RECOMPILED_DIR whose name a glob would read as a pattern")
# a console this build has no word for: no runtime, and the line says which
file(WRITE "${dir}/odd/Odd/Odd_code.so" "")
file(WRITE "${dir}/odd/Odd/Odd.ini" "system=nds\n")
deploy("${dir}/odd" 0)
if(NOT result EQUAL 0 OR EXISTS "${dir}/odd/Odd/Odd" OR NOT out MATCHES "Odd.ini says system=nds")
    file(REMOVE_RECURSE "${dir}")
    message(FATAL_ERROR "a game of system=nds got a runtime, or no line said why not (${result}):\n${out}")
endif()
game("${dir}/xdg/nxaevora/recompiled")
deploy("" 0 "XDG_DATA_HOME=${dir}/xdg" "HOME=${dir}/home")
deployed("${dir}/xdg/nxaevora/recompiled" "the user's data folder")
game("${dir}/home/.local/share/nxaevora/recompiled")
deploy("" 0 "XDG_DATA_HOME=relative" "HOME=${dir}/home")
deployed("${dir}/home/.local/share/nxaevora/recompiled" "a relative XDG_DATA_HOME")
game("${dir}/bin/.nxaevora/recompiled")
deploy("" 0 "XDG_DATA_HOME=${dir}/xdg")
deployed("${dir}/bin/.nxaevora/recompiled" "a portable copy's")
file(REMOVE_RECURSE "${dir}/bin/.nxaevora")
# a Windows build with an empty APPDATA ignores an absolute XDG_DATA_HOME: no runtime, and the line says so
file(WRITE "${dir}/xdg-win/nxaevora/recompiled/Game/Game_code.dll" "")
deploy("" 1 "APPDATA=" "XDG_DATA_HOME=${dir}/xdg-win")
if(NOT result EQUAL 0 OR EXISTS "${dir}/xdg-win/nxaevora/recompiled/Game/Game.exe"
   OR NOT out MATCHES "Runtime not deployed")
    file(REMOVE_RECURSE "${dir}")
    message(FATAL_ERROR "a Windows build with an empty APPDATA deployed with XDG_DATA_HOME (${result}):\n${out}")
endif()
deploy("" 0)
file(REMOVE_RECURSE "${dir}")
if(NOT result EQUAL 0 OR NOT out MATCHES "Runtime not deployed: no home folder to find the games in")
    message(FATAL_ERROR "without a home folder deploy.cmake ended with ${result}, not saying so:\n${out}")
endif()
