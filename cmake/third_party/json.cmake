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

unset(json_FOUND CACHE)
unset(JSON_INCLUDE CACHE)

set(JSON_DOWNLOAD_PATH ${ASCEND_3RD_LIB_PATH}/pkg)
set(JSON_INSTALL_PATH ${ASCEND_3RD_LIB_PATH}/json)

find_path(JSON_INCLUDE
        NAMES nlohmann/json.hpp
        NO_CMAKE_SYSTEM_PATH
        NO_CMAKE_FIND_ROOT_PATH
        PATHS ${JSON_INSTALL_PATH}/include)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(json
        FOUND_VAR
        json_FOUND
        REQUIRED_VARS
        JSON_INCLUDE
        )

if(json_FOUND AND NOT FORCE_REBUILD_CANN_3RD)
    message("json found in ${JSON_INSTALL_PATH}, and not force rebuild cann third_party")
    set(JSON_INCLUDE_DIR ${JSON_INCLUDE})
    add_library(json INTERFACE IMPORTED)
    set_target_properties(json PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${JSON_INCLUDE}")
else()
    set(REQ_URL "https://gitcode.com/cann-src-third-party/json/releases/download/v3.11.3/include.zip")
    message("json not found in ${JSON_INSTALL_PATH}, begin load from ${REQ_URL}")

    file(MAKE_DIRECTORY ${JSON_DOWNLOAD_PATH})
    file(DOWNLOAD 
        ${REQ_URL} 
        ${JSON_DOWNLOAD_PATH}/include.zip 
        SHOW_PROGRESS
        TLS_VERIFY OFF
    )
    file(MAKE_DIRECTORY ${JSON_INSTALL_PATH})
    execute_process(
        COMMAND unzip -o "${JSON_DOWNLOAD_PATH}/include.zip" -d ${JSON_INSTALL_PATH}
        RESULT_VARIABLE UNZIP_RESULT
        ERROR_VARIABLE UNZIP_ERROR
    )

    if(NOT UNZIP_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to unzip ${JSON_DOWNLOAD_PATH}/include.zip")
    endif()

    set(JSON_INCLUDE_DIR ${JSON_INSTALL_PATH}/include)
    add_library(json INTERFACE IMPORTED)
    set_target_properties(json PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${JSON_INCLUDE_DIR}")
endif()
