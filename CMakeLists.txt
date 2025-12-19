# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
project(hixl VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
option(ENABLE_TEST "Enable test" OFF)
option(ENABLE_EXAMPLES "Enable examples" OFF)
option(ENABLE_BENCHMARKS "Enable benchmarks" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_GCOV "Enable Coverage" OFF)

include(cmake/variables.cmake)
include(cmake/dependencies.cmake)

set(HIXL_VERSION_FILE ${CMAKE_CURRENT_BINARY_DIR}/version/version.info)
add_custom_command(
    OUTPUT ${HIXL_VERSION_FILE}
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/update_version_info/update_version_info.sh
            ${CMAKE_CURRENT_SOURCE_DIR}/version.info
            ${HIXL_VERSION_FILE}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/version.info
    VERBATIM
)
add_custom_target(generate_hixl_version_info
    DEPENDS ${HIXL_VERSION_FILE}
)

if (ENABLE_TEST)
    add_subdirectory(tests)
else ()
    add_subdirectory(src)
    if (BUILD_WITH_INSTALLED_DEPENDENCY_CANN_PKG)
        include(cmake/package.cmake)
    endif()
    if (ENABLE_EXAMPLES)
        add_subdirectory(examples)
    endif()
    if (ENABLE_BENCHMARKS)
        add_subdirectory(benchmarks)
    endif()
endif()
