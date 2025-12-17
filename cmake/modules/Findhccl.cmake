# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

if (hccl_FOUND)
    message(STATUS "Package hccl has been found.")
    return()
endif()

set(_cmake_targets_defined "")
set(_cmake_targets_not_defined "")
set(_cmake_expected_targets "")
foreach(_cmake_expected_target IN ITEMS hccl hccl_headers)
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

find_path(_EX_HCCL_PATH "experiment/hccl/base.h"
          NO_CMAKE_SYSTEM_PATH
          NO_CMAKE_FIND_ROOT_PATH)
find_path(_HCCL_PATH "../pkg_inc/hccl/base.h"
          NO_CMAKE_SYSTEM_PATH
          NO_CMAKE_FIND_ROOT_PATH)

if(_EX_HCCL_PATH)
    set(_INCLUDE_DIR "${_EX_HCCL_PATH}/experiment")
elseif(_HCCL_PATH)
    set(_INCLUDE_DIR "${_HCCL_PATH}/../pkg_inc")
else()
    unset(_INCLUDE_DIR)
endif()

find_library(hccl_SHARED_LIBRARY
    NAMES libhccl.so
    PATH_SUFFIXES lib64
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(hccl
    FOUND_VAR
        hccl_FOUND
    REQUIRED_VARS
        _INCLUDE_DIR
        hccl_SHARED_LIBRARY
)

if(hccl_FOUND)
    set(hccl_INCLUDE_DIR "${_INCLUDE_DIR}")
    include(CMakePrintHelpers)
    message(STATUS "Variables in hccl module:")
    cmake_print_variables(hccl_INCLUDE_DIR)
    cmake_print_variables(hccl_SHARED_LIBRARY)

    add_library(hccl SHARED IMPORTED)
    set_target_properties(hccl PROPERTIES
        INTERFACE_LINK_LIBRARIES "hccl_headers"
        IMPORTED_LINK_DEPENDENT_LIBRARIES "c_sec;slog;mmpa;runtime;qos_manager;tsdclient;error_manager;exe_graph;lowering;hccl_alg;hccl_plf"
        IMPORTED_LOCATION "${hccl_SHARED_LIBRARY}"
    )

    add_library(hccl_headers INTERFACE IMPORTED)
    if (EXISTS "${hccl_INCLUDE_DIR}/../pkg_inc/hccl")
        set_target_properties(hccl_headers PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${hccl_INCLUDE_DIR};${hccl_INCLUDE_DIR}/hccl;${hccl_INCLUDE_DIR}/../pkg_inc;${hccl_INCLUDE_DIR}/../pkg_inc/hccl"
        )
    else ()
        set_target_properties(hccl_headers PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${hccl_INCLUDE_DIR};${hccl_INCLUDE_DIR}/hccl"
        )
    endif ()

    include(CMakePrintHelpers)
    cmake_print_properties(TARGETS hccl
        PROPERTIES INTERFACE_LINK_LIBRARIES IMPORTED_LINK_DEPENDENT_LIBRARIES IMPORTED_LOCATION
    )
    cmake_print_properties(TARGETS hccl_headers
        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
    )
endif()

# Cleanup temporary variables.
set(_INCLUDE_DIR)
