# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

if (metadef_FOUND)
  message(STATUS "Package metadef has been found.")
  return()
endif()

set(_cmake_targets_defined "")
set(_cmake_targets_not_defined "")
set(_cmake_expected_targets "")
foreach(_cmake_expected_target IN ITEMS error_manager metadef)
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
    NAMES base/err_msg.h
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH)

find_library(metadef_SHARED_LIBRARY
    NAMES libmetadef.so
    PATH_SUFFIXES lib64
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH)

find_library(error_manager_SHARED_LIBRARY
    NAMES liberror_manager.so
    PATH_SUFFIXES lib64
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(metadef
    FOUND_VAR
        metadef_FOUND
    REQUIRED_VARS
        _INCLUDE_DIR
        metadef_SHARED_LIBRARY
        error_manager_SHARED_LIBRARY
)

if(metadef_FOUND)
    include(CMakePrintHelpers)
    message(STATUS "Variables in metadef module:")
    cmake_print_variables(metadef_SHARED_LIBRARY)
    cmake_print_variables(error_manager_SHARED_LIBRARY)

    add_library(metadef SHARED IMPORTED)
    set_target_properties(metadef PROPERTIES
        INTERFACE_LINK_LIBRARIES "metadef_headers"
        IMPORTED_LOCATION "${metadef_SHARED_LIBRARY}"
    )

    add_library(error_manager SHARED IMPORTED)
    set_target_properties(error_manager PROPERTIES
        INTERFACE_LINK_LIBRARIES "metadef_headers"
        IMPORTED_LOCATION "${error_manager_SHARED_LIBRARY}"
    )

    add_library(metadef_headers INTERFACE IMPORTED)
    set_target_properties(metadef_headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_INCLUDE_DIR};${_INCLUDE_DIR}/external"
    )

    include(CMakePrintHelpers)
    cmake_print_properties(TARGETS metadef
        PROPERTIES INTERFACE_LINK_LIBRARIES IMPORTED_LOCATION
    )
    cmake_print_properties(TARGETS error_manager
        PROPERTIES INTERFACE_LINK_LIBRARIES IMPORTED_LOCATION
    )
    cmake_print_properties(TARGETS metadef_headers
        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
    )
endif()

# Cleanup temporary variables.
set(_INCLUDE_DIR)
