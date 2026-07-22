# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

include_guard(GLOBAL)

if(NOT CANN_VERSION_hixl_VERSION)
    message(FATAL_ERROR "CANN_VERSION_hixl_VERSION is not set")
endif()

set(HIXL_DEVICE_PACKAGE_NAME "cann-hixl_${CANN_VERSION_hixl_VERSION}_device.tar.gz")
set(HIXL_DEVICE_PACKAGE_STAGE_NAME "device-hixl.tar.gz")
set(HIXL_DEVICE_PACKAGE_BASE_URL
    "https://ascend-cann.obs.cn-north-4.myhuaweicloud.com/CANN/20260729_newest"
    CACHE STRING "Base URL for signed HIXL device package")
set(HIXL_DEVICE_PACKAGE_URL
    "${HIXL_DEVICE_PACKAGE_BASE_URL}/${HIXL_DEVICE_PACKAGE_NAME}"
    CACHE STRING "URL for signed HIXL device package")
set(HIXL_DEVICE_PACKAGE_URL_HASH
    ""
    CACHE STRING "Hash for signed HIXL device package. Keep empty when using the rolling latest URL")
set(HIXL_DEVICE_PACKAGE_PKG_PATH "${CANN_3RD_LIB_PATH}/pkg/device")
set(HIXL_DEVICE_PACKAGE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/device_build")
set(HIXL_DEVICE_PACKAGE_OUTPUT "${HIXL_DEVICE_PACKAGE_OUTPUT_DIR}/${HIXL_DEVICE_PACKAGE_STAGE_NAME}")

if(EXISTS "${CANN_3RD_LIB_PATH}/${HIXL_DEVICE_PACKAGE_NAME}")
    message(STATUS "[Third party] Found local HIXL device package: ${CANN_3RD_LIB_PATH}/${HIXL_DEVICE_PACKAGE_NAME}")
    set(HIXL_DEVICE_PACKAGE_PROJECT_URL "${CANN_3RD_LIB_PATH}/${HIXL_DEVICE_PACKAGE_NAME}")
    set(HIXL_DEVICE_PACKAGE_FULLPATH "${CANN_3RD_LIB_PATH}/${HIXL_DEVICE_PACKAGE_NAME}")
elseif(EXISTS "${HIXL_DEVICE_PACKAGE_PKG_PATH}/${HIXL_DEVICE_PACKAGE_NAME}")
    message(STATUS "[Third party] Found cached HIXL device package: ${HIXL_DEVICE_PACKAGE_PKG_PATH}/${HIXL_DEVICE_PACKAGE_NAME}")
    set(HIXL_DEVICE_PACKAGE_PROJECT_URL "${HIXL_DEVICE_PACKAGE_PKG_PATH}/${HIXL_DEVICE_PACKAGE_NAME}")
    set(HIXL_DEVICE_PACKAGE_FULLPATH "${HIXL_DEVICE_PACKAGE_PKG_PATH}/${HIXL_DEVICE_PACKAGE_NAME}")
else()
    message(STATUS "[Third party] Downloading HIXL device package from ${HIXL_DEVICE_PACKAGE_URL}")
    set(HIXL_DEVICE_PACKAGE_PROJECT_URL "${HIXL_DEVICE_PACKAGE_URL}")
    set(HIXL_DEVICE_PACKAGE_FULLPATH "${HIXL_DEVICE_PACKAGE_PKG_PATH}/${HIXL_DEVICE_PACKAGE_NAME}")
endif()

include(ExternalProject)
ExternalProject_Add(hixl_device_package
    URL ${HIXL_DEVICE_PACKAGE_PROJECT_URL}
    URL_HASH ${HIXL_DEVICE_PACKAGE_URL_HASH}
    DOWNLOAD_DIR ${HIXL_DEVICE_PACKAGE_PKG_PATH}
    SOURCE_DIR ${CMAKE_BINARY_DIR}/hixl_device_package_src
    CONFIGURE_COMMAND ${CMAKE_COMMAND}
        -D HIXL_DEVICE_PACKAGE_SOURCE=${HIXL_DEVICE_PACKAGE_FULLPATH}
        -D HIXL_DEVICE_PACKAGE_OUTPUT=${HIXL_DEVICE_PACKAGE_OUTPUT}
        -P ${CMAKE_CURRENT_LIST_DIR}/stage_hixl_device_package.cmake
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
    DOWNLOAD_NO_EXTRACT TRUE
    DOWNLOAD_NO_PROGRESS TRUE
    TIMEOUT 300
)

add_custom_target(hixl_device_package_all ALL)
add_dependencies(hixl_device_package_all hixl_device_package)

install(FILES
    ${HIXL_DEVICE_PACKAGE_OUTPUT}
    DESTINATION .
    COMPONENT hixl
)
