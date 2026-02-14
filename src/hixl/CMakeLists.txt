# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.7)
project(hixl)

set(HIXL_INC_DIR
        "${HIXL_CODE_DIR}/include"
        "${HIXL_CODE_DIR}/src/llm_datadist"
        "${CMAKE_CURRENT_LIST_DIR}"
        )
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)

file(GLOB HIXL_SRC_LIST
        "${CMAKE_CURRENT_LIST_DIR}/engine/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/common/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/cs/*.cc"
        )

add_library(cann_hixl SHARED ${HIXL_SRC_LIST})

target_include_directories(cann_hixl PRIVATE ${HIXL_INC_DIR})

target_compile_options(cann_hixl PRIVATE
        -ftrapv
        -O2
        -fno-common
        -Wfloat-equal
        -Wall
        -Werror
        -Wextra)

target_link_libraries(cann_hixl PRIVATE
        $<BUILD_INTERFACE:intf_pub>
        $<BUILD_INTERFACE:slog_headers>
        $<BUILD_INTERFACE:mmpa_headers>
        $<BUILD_INTERFACE:hccl_headers>
        $<BUILD_INTERFACE:acl_rt_headers>
        $<BUILD_INTERFACE:runtime_headers>
        $<BUILD_INTERFACE:msprof_headers>
        PUBLIC c_sec
        static_mmpa
        adxl_static
        alog
        error_manager
        acl_rt
        runtime
        json
        -ldl
        -O2 -fPIC -Wextra -Wfloat-equal
        -Wl,--no-as-needed
        hccl
        -Wl,--as-needed
        )

install(TARGETS cann_hixl OPTIONAL
        EXPORT cann_hixl-targets
        LIBRARY DESTINATION ${INSTALL_LIBRARY_DIR}
        RUNTIME DESTINATION ${INSTALL_LIBRARY_DIR})

