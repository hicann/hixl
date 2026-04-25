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
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_GCOV "Enable Coverage" OFF)

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
endif()

message(STATUS "start build device")
if(NOT EXISTS "${CANN_INSTALL_PATH}/toolkit/toolchain/hcc")
    message(FATAL_ERROR "CANN toolchain path does not exist: ${CANN_INSTALL_PATH}/toolkit/toolchain/hcc")
endif()
set(TOOLCHAIN_DIR "${CANN_INSTALL_PATH}/toolkit/toolchain/hcc")
# 对于libc_sec，只有makefile，需要传递编译器路径
set(CMAKE_CXX_COMPILER_ "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-g++")
set(CMAKE_C_COMPILER_ "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-gcc")
set(CMAKE_C_AR "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-ar")
set(CHILD_INSTALL_DIR ${CMAKE_BINARY_DIR}/device_install)
include(ExternalProject)
ExternalProject_Add(device
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cmake/device
    BINARY_DIR ${CMAKE_BINARY_DIR}/device_build
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DTOOLCHAIN_DIR=${TOOLCHAIN_DIR}
        -DCMAKE_INSTALL_PREFIX=${CHILD_INSTALL_DIR}
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/aarch64-hcc-toolchain.cmake
        -DCMAKE_C_AR=${CMAKE_C_AR}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER_}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER_}
        -DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}
        -DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}
        -DENABLE_SIGN=${ENABLE_SIGN}
        -DCUSTOM_SIGN_SCRIPT=${CUSTOM_SIGN_SCRIPT}
        -DVERSION_INFO=${VERSION_INFO}
        -DCANN_INSTALL_PATH=${CANN_INSTALL_PATH}
        -DENABLE_ASAN=${ENABLE_ASAN}
        -DENABLE_GCOV=${ENABLE_GCOV}
        -DCANN_VERSION_hixl_VERSION=${CANN_VERSION_hixl_VERSION}
        -DCANN_3RD_LIB_PATH=${CANN_3RD_LIB_PATH}
    BUILD_ALWAYS TRUE
)