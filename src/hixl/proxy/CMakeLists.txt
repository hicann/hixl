# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

set(HIXL_INC_DIR
        "${CMAKE_CURRENT_LIST_DIR}"
        "${HIXL_CODE_DIR}/include"
        "${HIXL_CODE_DIR}/src/hixl"
)
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)

file(GLOB HCOMM_PROXY_SRC_LIST
        "${CMAKE_CURRENT_LIST_DIR}/*.cc"
)

add_library(hcomm_proxy_static STATIC ${HCOMM_PROXY_SRC_LIST})

target_include_directories(hcomm_proxy_static PRIVATE ${HIXL_INC_DIR})

target_compile_options(hcomm_proxy_static PRIVATE
        -ftrapv
        -O2
        -fno-common
        -Wfloat-equal
        -Wall
        -Werror
        -Wextra)

target_link_libraries(hcomm_proxy_static PRIVATE
        $<BUILD_INTERFACE:intf_pub>
        $<BUILD_INTERFACE:hccl_headers>
        $<BUILD_INTERFACE:slog_headers>
)
