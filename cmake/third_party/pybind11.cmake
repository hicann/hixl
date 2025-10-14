# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

include_guard(GLOBAL)

unset(pybind11_FOUND CACHE)
unset(PYBIND11_INCLUDE CACHE)

set(PYBIND11_DOWNLOAD_PATH ${ASCEND_3RD_LIB_PATH}/pkg)
set(PYBIND11_INSTALL_PATH ${ASCEND_3RD_LIB_PATH}/pybind11)

find_path(PYBIND11_INCLUDE
        NAMES pybind11/pybind11.h
        NO_CMAKE_SYSTEM_PATH
        NO_CMAKE_FIND_ROOT_PATH
        PATHS ${PYBIND11_INSTALL_PATH}/include)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(pybind11
        FOUND_VAR
        pybind11_FOUND
        REQUIRED_VARS
        PYBIND11_INCLUDE
        )
if(pybind11_FOUND AND NOT FORCE_REBUILD_CANN_3RD)
    message("pybind11 found in ${PYBIND11_INSTALL_PATH}, and not force rebuild cann third_party")
    set(pybind11_INCLUDE_DIR ${PYBIND11_INCLUDE})
    add_library(pybind11 INTERFACE IMPORTED)
    set_target_properties(pybind11 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${pybind11_INCLUDE_DIR}")
else()
    set(REQ_URL "https://gitcode.com/cann-src-third-party/pybind11/releases/download/v2.13.6/pybind11-2.13.6.tar.gz")
    message("pybind11 not found in ${PYBIND11_INSTALL_PATH}, begin load from ${REQ_URL}")

    file(MAKE_DIRECTORY ${PYBIND11_DOWNLOAD_PATH})
    file(DOWNLOAD 
        ${REQ_URL} 
        ${PYBIND11_DOWNLOAD_PATH}/pybind11-2.13.6.tar.gz
        SHOW_PROGRESS
        TLS_VERIFY OFF
    )
    file(MAKE_DIRECTORY ${PYBIND11_INSTALL_PATH})
    execute_process(
        COMMAND tar xf "${PYBIND11_DOWNLOAD_PATH}/pybind11-2.13.6.tar.gz" --strip-components=1 -C ${PYBIND11_INSTALL_PATH}
        RESULT_VARIABLE TAR_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )

    if(NOT TAR_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to tar xf ${PYBIND11_DOWNLOAD_PATH}/pybind11-2.13.6.tar.gz")
    endif()

    set(pybind11_INCLUDE_DIR ${PYBIND11_INSTALL_PATH}/include)
    add_library(pybind11 INTERFACE IMPORTED)
    set_target_properties(pybind11 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${pybind11_INCLUDE_DIR}")
endif()
