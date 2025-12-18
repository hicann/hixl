# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.7)
project(llm_datadist)
set(CMAKE_SKIP_RPATH TRUE)
set(INC_DIR
        "${HIXL_CODE_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/link_mgr"
        "${CMAKE_CURRENT_LIST_DIR}/cache_mgr"
        "${CMAKE_CURRENT_LIST_DIR}/hccl"
        "${CMAKE_CURRENT_LIST_DIR}/common"
        "${CMAKE_CURRENT_LIST_DIR}/fsm"
        )
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)

file(GLOB LLM_SRC_LIST
        "${CMAKE_CURRENT_LIST_DIR}/api/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/adxl/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/common/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/cache_mgr/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/link_mgr/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/hccl/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/fsm/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/utils/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/data_transfer/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/memory/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/memory/allocator/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/memory/config/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/memory/span/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/memory/type/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/memory/util/*.cc"
        )

add_library(llm_datadist SHARED ${LLM_SRC_LIST})

target_include_directories(llm_datadist PRIVATE ${INC_DIR})

target_compile_options(llm_datadist PRIVATE
        -ftrapv
        -fno-common
        -Wfloat-equal
        -Wall
        -Werror
        -Wextra)

target_link_libraries(llm_datadist PRIVATE
        $<BUILD_INTERFACE:intf_pub>
        $<BUILD_INTERFACE:slog_headers>
        $<BUILD_INTERFACE:mmpa_headers>
        $<BUILD_INTERFACE:hccl_headers>
        $<BUILD_INTERFACE:runtime_headers>
        $<BUILD_INTERFACE:msprof_headers>
        PUBLIC c_sec
        static_mmpa
        alog
        error_manager
        metadef # for acend string
        runtime
        json
        -ldl
        -fPIC -Wextra -Wfloat-equal
        )

#set(INSTALL_LIBRARY_DIR lib)
install(TARGETS llm_datadist OPTIONAL
        EXPORT llm_datadist-targets
        LIBRARY DESTINATION ${INSTALL_LIBRARY_DIR}
        RUNTIME DESTINATION ${INSTALL_LIBRARY_DIR})

set(HIXL_INC_DIR
        "${HIXL_CODE_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/hccl"
        "${CMAKE_CURRENT_LIST_DIR}/common"
        )
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)

file(GLOB HIXL_SRC_LIST
        "${CMAKE_CURRENT_LIST_DIR}/api/hixl_impl.cc"
        "${CMAKE_CURRENT_LIST_DIR}/adxl/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/llm_datadist_timer.cc"
        "${CMAKE_CURRENT_LIST_DIR}/statistic_manager.cc"
        "${CMAKE_CURRENT_LIST_DIR}/common/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/hccl/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/memory/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/memory/allocator/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/memory/config/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/memory/span/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/memory/type/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/memory/util/*.cc"
        )

add_library(cann_hixl SHARED ${HIXL_SRC_LIST})

target_include_directories(cann_hixl PRIVATE ${HIXL_INC_DIR})

target_compile_options(cann_hixl PRIVATE
        -ftrapv
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
        $<BUILD_INTERFACE:runtime_headers>
        $<BUILD_INTERFACE:msprof_headers>
        PUBLIC c_sec
        static_mmpa
        alog
        error_manager
        metadef # for acend string
        runtime
        json
        -ldl
        -fPIC -Wextra -Wfloat-equal
        )

#set(INSTALL_LIBRARY_DIR lib)
install(TARGETS cann_hixl OPTIONAL
        EXPORT cann_hixl-targets
        LIBRARY DESTINATION ${INSTALL_LIBRARY_DIR}
        RUNTIME DESTINATION ${INSTALL_LIBRARY_DIR})

