# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

if (runtime_static_FOUND)
    message(STATUS "Package runtime_static has been found.")
    return()
endif()

find_library(runtime_STATIC_LIBRARY
    NAMES libruntime.a
    PATH_SUFFIXES lib64/c
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(runtime_static
    FOUND_VAR
        runtime_static_FOUND
    REQUIRED_VARS
        runtime_STATIC_LIBRARY
)

if(runtime_static_FOUND)
    include(CMakePrintHelpers)
    message(STATUS "Variables in runtime_static module:")
    cmake_print_variables(runtime_STATIC_LIBRARY)

    add_library(runtime_static STATIC IMPORTED)
    set_target_properties(runtime_static PROPERTIES
        INTERFACE_LINK_LIBRARIES "\$<LINK_ONLY:c_sec_headers>;runtime_headers"
        IMPORTED_LOCATION "${runtime_STATIC_LIBRARY}"
    )

    include(CMakePrintHelpers)
    cmake_print_properties(TARGETS runtime_static
        PROPERTIES INTERFACE_LINK_LIBRARIES IMPORTED_LOCATION
    )
endif()

# Cleanup temporary variables.
set(_INCLUDE_DIR)
