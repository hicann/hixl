# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

if (ascendcl_FOUND)
    message(STATUS "Package ascendcl has been found.")
    return()
endif()

set(_cmake_targets_defined "")
set(_cmake_targets_not_defined "")
set(_cmake_expected_targets "")
foreach(_cmake_expected_target IN ITEMS ascendcl ascendcl_static ascendcl_headers)
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

find_path(_INCLUDE_DIR
    NAMES acl/acl.h
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH)

find_library(ascendcl_SHARED_LIBRARY
    NAMES libascendcl.so
    PATH_SUFFIXES lib64
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH)

find_library(ascendcl_STATIC_LIBRARY
    NAMES libascendcl.a
    PATH_SUFFIXES lib64
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ascendcl
    FOUND_VAR
        ascendcl_FOUND
    REQUIRED_VARS
        _INCLUDE_DIR
        ascendcl_SHARED_LIBRARY
        ascendcl_STATIC_LIBRARY
)

if(ascendcl_FOUND)
    set(ascendcl_INCLUDE_DIR "${_INCLUDE_DIR}")
    include(CMakePrintHelpers)
    message(STATUS "Variables in ascendcl module:")
    cmake_print_variables(ascendcl_INCLUDE_DIR)
    cmake_print_variables(ascendcl_SHARED_LIBRARY)
    cmake_print_variables(ascendcl_STATIC_LIBRARY)

    add_library(ascendcl SHARED IMPORTED)
    set_target_properties(ascendcl PROPERTIES
        INTERFACE_LINK_LIBRARIES "ascendcl_headers"
        IMPORTED_LOCATION "${ascendcl_SHARED_LIBRARY}"
    )

    add_library(ascendcl_static STATIC IMPORTED)
    set_target_properties(ascendcl_static PROPERTIES
        INTERFACE_LINK_LIBRARIES "ascendcl_headers"
        IMPORTED_LOCATION "${ascendcl_STATIC_LIBRARY}"
    )

    add_library(ascendcl_headers INTERFACE IMPORTED)
    set_target_properties(ascendcl_headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${ascendcl_INCLUDE_DIR};${ascendcl_INCLUDE_DIR}/acl"
    )

    include(CMakePrintHelpers)
    cmake_print_properties(TARGETS ascendcl
        PROPERTIES INTERFACE_LINK_LIBRARIES IMPORTED_LOCATION
    )
    cmake_print_properties(TARGETS ascendcl_static
        PROPERTIES INTERFACE_LINK_LIBRARIES IMPORTED_LOCATION
    )
    cmake_print_properties(TARGETS ascendcl_headers
        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
    )
endif()

# Cleanup temporary variables.
set(_INCLUDE_DIR)
