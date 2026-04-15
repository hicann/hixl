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

add_library(hcomm_proxy_static STATIC "${CMAKE_CURRENT_LIST_DIR}/hcomm_proxy.cc")

if(PRODUCT_SIDE STREQUAL "device")
        # AICPU/device 子工程（cmake/device）：不编译 hccp_proxy；供源码侧 #if defined(PRODUCT_SIDE_DEVICE) 等使用
        target_compile_definitions(hcomm_proxy_static PRIVATE PRODUCT_SIDE_DEVICE=1)
        set(_HIXL_PROXY_TARGETS hcomm_proxy_static)
else()
        add_library(hccp_proxy_static STATIC "${CMAKE_CURRENT_LIST_DIR}/hccp_proxy.cc")
        set(_HIXL_PROXY_TARGETS hcomm_proxy_static hccp_proxy_static)

        target_link_libraries(hccp_proxy_static PRIVATE
                $<BUILD_INTERFACE:intf_pub>
                $<BUILD_INTERFACE:acl_rt_headers>
                $<BUILD_INTERFACE:runtime_headers>
                $<BUILD_INTERFACE:slog_headers>
                $<BUILD_INTERFACE:mmpa_headers>
        )
endif()

foreach(_proxy_target ${_HIXL_PROXY_TARGETS})
        target_include_directories(${_proxy_target} PRIVATE ${HIXL_INC_DIR})
        target_compile_options(${_proxy_target} PRIVATE
                -ftrapv
                -O2
                -fno-common
                -Wfloat-equal
                -Wall
                -Werror
                -Wextra)
endforeach()

target_link_libraries(hcomm_proxy_static PRIVATE
        $<BUILD_INTERFACE:intf_pub>
        $<BUILD_INTERFACE:hccl_headers>
        $<BUILD_INTERFACE:slog_headers>
)