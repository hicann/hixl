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

init_cann_project(PRODUCT_SIDE "device")
add_definitions(-DPRODUCT_SIDE_DEVICE)
set(USE_CXX11_ABI OFF)
add_cann_target_options()
include(../function.cmake)
include(../intf_pub_linux.cmake)

get_filename_component(HIXL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(HIXL_CODE_DIR ${HIXL_DIR})

include(${HIXL_DIR}/cmake/variables.cmake)
include(${HIXL_DIR}/version.cmake)

find_package_if_target_not_exists(acl_rt MODULE REQUIRED)
find_package_if_target_not_exists(error_manager MODULE REQUIRED)
find_package_if_target_not_exists(hccl MODULE REQUIRED)
find_package_if_target_not_exists(metadef MODULE REQUIRED)
find_package_if_target_not_exists(securec MODULE REQUIRED)
find_package_if_target_not_exists(slog MODULE REQUIRED)
find_package_if_target_not_exists(metadef MODULE REQUIRED)
find_package_if_target_not_exists(securec MODULE REQUIRED)

generate_cann_stub_library(slog_dev OUTPUT_NAME slog)
# ccl_kernel是device包，依赖cann包编译的时候找不到，需要打桩
generate_cann_stub_library(ccl_kernel)

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
  DESTINATION opp/built-in/op_impl/aicpu/kernel
  COMPONENT hixl
)

set_cann_subprj_package()
