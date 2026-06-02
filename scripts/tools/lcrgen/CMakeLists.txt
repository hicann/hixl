# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
project(hixl_lcrgen_tool)

add_executable(lcrgen local_comm_res_generator.cc)

target_compile_features(lcrgen PRIVATE cxx_std_17)

target_include_directories(lcrgen PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HIXL_CODE_DIR}/include
        ${HIXL_CODE_DIR}/src/hixl
        ${HIXL_CODE_DIR}/src/hixl/engine
        ${HIXL_CODE_DIR}/src/hixl/proxy
        ${HIXL_CODE_DIR}/src/hixl/common
        ${HIXL_CODE_DIR}/third_party/json/include
        ${CANN_INSTALL_PATH}/include
)

target_compile_options(lcrgen PRIVATE
        -ftrapv
        -O2
        -fno-common
        -Wfloat-equal
        -Wall
        -Werror
        -Wextra
)

target_link_libraries(lcrgen PRIVATE
        cann_hixl
        acl_rt_headers
        acl_rt
        -lpthread
        stdc++fs
)

target_link_ascend_hal(lcrgen)