# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16.0)
project(hixl_benchmarks)

set(HIXL_INC_DIR "${HIXL_CODE_DIR}/include")
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)

set(BENCHMARK_COMMON_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/common/benchmark_config.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/tcp_client_server.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/client_runner.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/server_runner.cc"
)

# benchmark：主程序 + common 下各模块 .cpp
add_executable(benchmark benchmark.cpp ${BENCHMARK_COMMON_SOURCES})
target_include_directories(benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${HIXL_INC_DIR}
    ${CANN_INSTALL_PATH}/include
)
target_compile_options(benchmark PRIVATE
    -ftrapv
    -O2
    -fno-common
    -Wfloat-equal
    -Wall
    -Werror
    -Wextra
)
target_link_libraries(benchmark PRIVATE
    cann_hixl
    acl_rt_headers
    acl_rt
    -lpthread
    -lrt
)
target_link_ascend_hal(benchmark)

if(NOT TARGET fabric_mem_kv_benchmark)
    add_executable(fabric_mem_kv_benchmark fabric_mem_kv_benchmark.cpp)
    target_include_directories(fabric_mem_kv_benchmark PRIVATE
        ${HIXL_INC_DIR}
        ${HIXL_CODE_DIR}/src/llm_datadist
        ${HIXL_CODE_DIR}/src/llm_datadist/adxl
        ${HIXL_CODE_DIR}/src/llm_datadist/common
        ${HIXL_CODE_DIR}/src/hixl
        ${CANN_INSTALL_PATH}/include
    )
    target_compile_options(fabric_mem_kv_benchmark PRIVATE
        -ftrapv
        -O2
        -fno-common
        -Wfloat-equal
        -Wall
        -Werror
        -Wextra
    )
    target_link_libraries(fabric_mem_kv_benchmark PRIVATE
        llm_datadist
        acl_rt_headers
        acl_rt
        -lpthread
        -lrt
    )
    target_link_ascend_hal(fabric_mem_kv_benchmark)
endif()
