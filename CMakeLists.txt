# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
include(cmake/fetch_cann_cmake.cmake)
project(hixl VERSION 1.0.0)

init_cann_project()

option(ENABLE_TEST "Enable test" OFF)
option(ENABLE_EXAMPLES "Enable examples" OFF)
option(ENABLE_BENCHMARKS "Enable benchmarks" OFF)
option(ENABLE_LCRGEN_TOOL "Enable lcrgen tool" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_GCOV "Enable Coverage" OFF)

set(HIXL_CODE_DIR ${PROJECT_SOURCE_DIR})

include(cmake/variables.cmake)
include(cmake/dependencies.cmake)

include(version.cmake)
check_cann_pkg_build_deps("hixl")
add_cann_version_info_targets()

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
    if (ENABLE_LCRGEN_TOOL)
        add_subdirectory(scripts/tools/lcrgen)
    endif()
endif()

message(STATUS "start build device")
if(NOT EXISTS "${ASCEND_INSTALL_PATH}/toolkit/toolchain/hcc")
    message(FATAL_ERROR "CANN toolchain path does not exist: ${ASCEND_INSTALL_PATH}/toolkit/toolchain/hcc")
endif()

add_cann_device_project(hixl)