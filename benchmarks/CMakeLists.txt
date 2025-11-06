# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16.0)
project(hixl_benchmarks)

set(HIXL_INC_DIR "${HIXL_CODE_DIR}/include")
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)

file(GLOB COMMON_FILES "${HIXL_CODE_DIR}/benchmarks/common/*.cpp")

# 只定义目标名称列表（与cpp文件名前缀相同）
set(targets_list
    "benchmark"
)

foreach(target_name IN LISTS targets_list)
    # 自动构建源文件路径（目标名.cpp）
    set(source_file "${target_name}.cpp")

    if(NOT TARGET ${target_name})
        add_executable(${target_name} ${source_file} ${COMMON_FILES})
        target_include_directories(${target_name} PRIVATE
            ${HIXL_INC_DIR}
            ${CANN_INSTALL_PATH}/include
        )
        target_compile_options(${target_name} PRIVATE
            -ftrapv
            -O2
            -fno-common
            -Wfloat-equal
            -Wall
            -Werror
            -Wextra
        )
        target_link_libraries(${target_name} PRIVATE
            llm_datadist
            ascendcl
            -lpthread
            -lrt
        )
        target_link_ascend_hal(${target_name})
    endif()
endforeach()
