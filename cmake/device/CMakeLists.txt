# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
include(../fetch_cann_cmake.cmake)
project (device_hixl_ops)

init_cann_project()

set(PRODUCT_SIDE device)
include(../function.cmake)
include(../intf_pub_linux.cmake)

set(HIXL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../" CACHE PATH "Project top directory")

set(PROJECT_SOURCE_DIR ${HIXL_DIR})
include(../variables.cmake)

find_package_if_target_not_exists(hccl MODULE REQUIRED)
find_package_if_target_not_exists(slog MODULE REQUIRED)

set(STUBS
    slog
    ccl_kernel
)
foreach(STUB ${STUBS})
    if(NOT TARGET ${STUB})
        generate_stub(${STUB})
    endif()
endforeach()

add_subdirectory(${HIXL_DIR}/src/hixl/proxy ${CMAKE_BINARY_DIR}/src/hixl/proxy)
add_subdirectory(${HIXL_DIR}/src/ops ${CMAKE_BINARY_DIR}/src/ops)
pack_targets_and_files(
    OUTPUT_TARGET "pack_aicpu_hixl"
    OUTPUT cann-hixl-compat.tar.gz
    FILES ""
    TARGETS cann_hixl_kernel
)

sign_file(
    INPUT "cann-hixl-compat.tar.gz"
    CONFIG "${HIXL_DIR}/scripts/sign/hixl_check_cfg.xml"
    RESULT_VAR "aicpu_hixl_sign_file"
    DEPENDS pack_aicpu_hixl
)

install(FILES
  ${aicpu_hixl_sign_file}
  DESTINATION hixl/aicpu_kernel
  COMPONENT hixl
)