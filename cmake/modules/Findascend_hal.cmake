# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

if (ascend_hal_FOUND)
    message(STATUS "Package ascend_hal has been found.")
    return()
endif()

set(_cmake_targets_defined "")
set(_cmake_targets_not_defined "")
set(_cmake_expected_targets "")
foreach(_cmake_expected_target IN ITEMS ascend_hal_stub ascend_hal_headers)
    list(APPEND _cmake_expected_targets "${_cmake_expected_target}")
    if(TARGET "${_cmake_expected_target}")
        list(APPEND _cmake_targets_defined "${_cmake_expected_target}")
    else()
        list(APPEND _cmake_targets_not_defined "${_cmake_expected_target}")
    endif()
endforeach()
unset(_cmake_expected_target)

if(_cmake_targets_defined STREQUAL _cmake_expected_targets)
    unset(_cmake_targets_defined)
    unset(_cmake_targets_not_defined)
    unset(_cmake_expected_targets)
    unset(CMAKE_IMPORT_FILE_VERSION)
    cmake_policy(POP)
    return()
endif()

if(NOT _cmake_targets_defined STREQUAL "")
    string(REPLACE ";" ", " _cmake_targets_defined_text "${_cmake_targets_defined}")
    string(REPLACE ";" ", " _cmake_targets_not_defined_text "${_cmake_targets_not_defined}")
    message(FATAL_ERROR "Some (but not all) targets in this export set were already defined.\nTargets Defined: ${_cmake_targets_defined_text}\nTargets not yet defined: ${_cmake_targets_not_defined_text}\n")
endif()
unset(_cmake_targets_defined)
unset(_cmake_targets_not_defined)
unset(_cmake_expected_targets)

find_path(_HAL_PATH
    NAMES driver/ascend_hal.h
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH)

if(_HAL_PATH)
    set(_INCLUDE_DIR "${_HAL_PATH}")
else()
    unset(_INCLUDE_DIR)
endif()

find_path(ascend_hal_LIBRARY_DIR
    NAMES libascend_hal.so
    PATH_SUFFIXES lib64/stub runtime/lib64/stub devlib
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ascend_hal
    FOUND_VAR
        ascend_hal_FOUND
    REQUIRED_VARS
        _INCLUDE_DIR
        ascend_hal_LIBRARY_DIR
)

if(ascend_hal_FOUND)
    set(ascend_hal_INCLUDE_DIR "${_INCLUDE_DIR}")
    include(CMakePrintHelpers)
    message(STATUS "Variables in ascend_hal module:")
    cmake_print_variables(ascend_hal_INCLUDE_DIR)
    cmake_print_variables(ascend_hal_LIBRARY_DIR)

    add_library(ascend_hal_headers INTERFACE IMPORTED)
    set_target_properties(ascend_hal_headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${ascend_hal_INCLUDE_DIR};${ascend_hal_INCLUDE_DIR}/driver"
    )

    function(target_link_ascend_hal target)
        target_include_directories(${target} PRIVATE ${ascend_hal_INCLUDE_DIR})
        target_link_options(${target} PRIVATE "-L${ascend_hal_LIBRARY_DIR}")
        target_link_libraries(${target} PRIVATE "-lascend_hal")
    endfunction()

    include(CMakePrintHelpers)
    cmake_print_properties(TARGETS ascend_hal_headers
        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
    )
endif()

# Cleanup temporary variables.
set(_INCLUDE_DIR)
unset(ascend_hal_LIBRARY_DIR)