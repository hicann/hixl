# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

include(cmake/function.cmake)
find_package_if_target_not_exists(securec MODULE REQUIRED)

if (NOT ENABLE_TEST)
    find_package_if_target_not_exists(slog MODULE REQUIRED)
    find_package_if_target_not_exists(runtime MODULE REQUIRED)
    find_package_if_target_not_exists(mmpa MODULE REQUIRED)
    find_package_if_target_not_exists(msprof MODULE REQUIRED)
    find_package_if_target_not_exists(hccl MODULE REQUIRED)
    find_package_if_target_not_exists(runtime_static MODULE REQUIRED)
    find_package_if_target_not_exists(ascendcl MODULE REQUIRED)
    find_package_if_target_not_exists(metadef MODULE REQUIRED)
    find_package_if_target_not_exists(ascend_hal MODULE REQUIRED)
else ()
    add_library(hccl_headers INTERFACE)
    target_include_directories(hccl_headers INTERFACE
        ${CANN_INSTALL_PATH}/include/experiment
        ${CANN_INSTALL_PATH}/include/experiment/hccl
        ${CANN_INSTALL_PATH}/include/experiment/hccl/external
        ${CANN_INSTALL_PATH}/include/experiment/hccl/external/hccl
    )

    add_library(mmpa_headers INTERFACE)
    target_include_directories(mmpa_headers INTERFACE
        ${CANN_INSTALL_PATH}/include/experiment
        ${CANN_INSTALL_PATH}/include/experiment/mmpa
        ${CANN_INSTALL_PATH}/include/experiment/mmpa/sub_inc
    )

    add_library(msprof_headers INTERFACE)
    target_include_directories(msprof_headers INTERFACE
        ${CANN_INSTALL_PATH}/include/experiment
        ${CANN_INSTALL_PATH}/include/experiment/msprof
        ${CANN_INSTALL_PATH}/include/experiment/msprof/toolchain
    )

    add_library(metadef_headers INTERFACE)
    target_include_directories(metadef_headers INTERFACE
        ${CANN_INSTALL_PATH}/include/
        ${CANN_INSTALL_PATH}/include/external
    )

    add_library(runtime_headers INTERFACE)
    target_include_directories(runtime_headers INTERFACE
        ${CANN_INSTALL_PATH}/include/experiment
        ${CANN_INSTALL_PATH}/include/experiment/runtime
        ${CANN_INSTALL_PATH}/include/experiment/runtime/external
        ${CANN_INSTALL_PATH}/include/experiment/runtime/external/runtime
    )

    add_library(slog_headers INTERFACE)
    target_include_directories(slog_headers INTERFACE
        ${CANN_INSTALL_PATH}/include/experiment
        ${CANN_INSTALL_PATH}/include/experiment/slog
        ${CANN_INSTALL_PATH}/include/experiment/slog/toolchain
    )

    add_library(ascendcl_headers INTERFACE)
    target_include_directories(ascendcl_headers INTERFACE
        ${CANN_INSTALL_PATH}/include
        ${CANN_INSTALL_PATH}/include/acl
    )
endif()
