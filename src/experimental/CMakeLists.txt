# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

# Experimental features build configuration
#
# Python modules:
#   Place modules under src/experimental/python/<module_name>/
#   Each module MUST contain __init__.py to be discovered by setuptools find_packages()
#   Modules are copied into hixl wheel when ENABLE_EXPERIMENTAL=ON

target_compile_definitions(cann_hixl PRIVATE HIXL_ENABLE_EXPERIMENTAL)

file(GLOB EXPERIMENTAL_SRC_LIST CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
if (EXPERIMENTAL_SRC_LIST)
    target_sources(cann_hixl PRIVATE ${EXPERIMENTAL_SRC_LIST})
    target_include_directories(cann_hixl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    message(STATUS "HIXL experimental C++ sources: ${EXPERIMENTAL_SRC_LIST}")
endif()
