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
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    message(STATUS "Build hixl with dependency cann pkgs installed")
    option(BUILD_WITH_INSTALLED_DEPENDENCY_CANN_PKG "Build hixl with dependency cann pkgs installed" TRUE)
else()
    message(STATUS "Build hixl with cann source exist")
    option(BUILD_WITH_INSTALLED_DEPENDENCY_CANN_PKG "Build hixl need cann source code" FALSE)
endif()

set(OPEN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../open_source")
if(NOT EXISTS ${OPEN_SOURCE_DIR})
    message(STATUS "Build hixl with dependency communtiy cann pkgs installed")
    option(BUILD_WITH_INSTALLED_DEPENDENCY_CANN_PKG_COMMUNITY "Build hixl with dependency communtiy cann pkgs installed" TRUE)
else()
    message(STATUS "Build hixl with dependency communtiy cann pkgs not installed")
    option(BUILD_WITH_INSTALLED_DEPENDENCY_CANN_PKG_COMMUNITY "Build hixl with dependency communtiy cann pkgs not installed" FALSE)
endif()

option(ENABLE_TEST "Enable test" OFF)
option(ENABLE_EXAMPLES "Enable examples" ON)

include(cmake/variables.cmake)
include(cmake/intf_pub_linux.cmake)
include(cmake/dependencies.cmake)

if (ENABLE_TEST)
    add_subdirectory(tests)
else ()
    add_subdirectory(src)
    include(cmake/package.cmake)
    if (ENABLE_EXAMPLES)
        add_subdirectory(examples)
    endif()
endif()
