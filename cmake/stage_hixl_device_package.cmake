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

if(NOT HIXL_DEVICE_PACKAGE_SOURCE)
    message(FATAL_ERROR "HIXL_DEVICE_PACKAGE_SOURCE is not set")
endif()

if(NOT HIXL_DEVICE_PACKAGE_OUTPUT)
    message(FATAL_ERROR "HIXL_DEVICE_PACKAGE_OUTPUT is not set")
endif()

if(NOT EXISTS "${HIXL_DEVICE_PACKAGE_SOURCE}")
    message(FATAL_ERROR "HIXL device package does not exist: ${HIXL_DEVICE_PACKAGE_SOURCE}")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar tf "${HIXL_DEVICE_PACKAGE_SOURCE}"
    OUTPUT_VARIABLE HIXL_DEVICE_PACKAGE_FILES
    ERROR_VARIABLE HIXL_DEVICE_PACKAGE_ERROR
    RESULT_VARIABLE HIXL_DEVICE_PACKAGE_RET
)

if(NOT HIXL_DEVICE_PACKAGE_RET EQUAL 0)
    message(FATAL_ERROR "List HIXL device package failed: ${HIXL_DEVICE_PACKAGE_ERROR}")
endif()

set(HIXL_DEVICE_KERNEL "opp/built-in/op_impl/aicpu/kernel/cann-hixl-compat.tar.gz")
set(HIXL_DEVICE_CONFIG "opp/built-in/op_impl/aicpu/config/libcann_hixl_kernel.json")

foreach(required_file IN ITEMS "${HIXL_DEVICE_KERNEL}" "${HIXL_DEVICE_CONFIG}")
    if(NOT HIXL_DEVICE_PACKAGE_FILES MATCHES "(^|\n)(\\./)?${required_file}(\n|$)")
        message(FATAL_ERROR "HIXL device package missing required file: ${required_file}")
    endif()
endforeach()

get_filename_component(HIXL_DEVICE_PACKAGE_OUTPUT_DIR "${HIXL_DEVICE_PACKAGE_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${HIXL_DEVICE_PACKAGE_OUTPUT_DIR}")

if(NOT "${HIXL_DEVICE_PACKAGE_SOURCE}" STREQUAL "${HIXL_DEVICE_PACKAGE_OUTPUT}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy "${HIXL_DEVICE_PACKAGE_SOURCE}" "${HIXL_DEVICE_PACKAGE_OUTPUT}"
        ERROR_VARIABLE HIXL_DEVICE_PACKAGE_COPY_ERROR
        RESULT_VARIABLE HIXL_DEVICE_PACKAGE_COPY_RET
    )
    if(NOT HIXL_DEVICE_PACKAGE_COPY_RET EQUAL 0)
        message(FATAL_ERROR "Copy HIXL device package failed: ${HIXL_DEVICE_PACKAGE_COPY_ERROR}")
    endif()
endif()

message(STATUS "Prepared HIXL device package: ${HIXL_DEVICE_PACKAGE_OUTPUT}")
