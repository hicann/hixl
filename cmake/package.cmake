# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

#### CPACK to package run #####
message(STATUS "System processor: ${CMAKE_SYSTEM_PROCESSOR}")
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    message(STATUS "Detected architecture: x86_64")
    set(ARCH x86_64)
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
    message(STATUS "Detected architecture: ARM64")
    set(ARCH aarch64)
else ()
    message(WARNING "Unknown architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    set(ARCH ${CMAKE_SYSTEM_PROCESSOR})
endif ()
# 打印路径
message(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")
message(STATUS "CMAKE_SOURCE_DIR = ${CMAKE_SOURCE_DIR}")
message(STATUS "CMAKE_BINARY_DIR = ${CMAKE_BINARY_DIR}")
set(ARCH_LINUX_PATH "${ARCH}-linux")

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/third_party/makeself-fetch.cmake)

set(script_prefix ${CMAKE_CURRENT_SOURCE_DIR}/scripts/package/hixl/scripts)
install(DIRECTORY ${script_prefix}/
    DESTINATION share/info/hixl/script
    FILE_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE  # 文件权限
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE
    DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE  # 目录权限
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE
)
set(SCRIPTS_FILES
    ${CMAKE_SOURCE_DIR}/scripts/package/common/sh/check_version_required.awk
    ${CMAKE_SOURCE_DIR}/scripts/package/common/sh/common_func.inc
    ${CMAKE_SOURCE_DIR}/scripts/package/common/sh/common_interface.bash
    ${CMAKE_SOURCE_DIR}/scripts/package/common/sh/common_interface.csh
    ${CMAKE_SOURCE_DIR}/scripts/package/common/sh/common_interface.fish
    ${CMAKE_SOURCE_DIR}/scripts/package/common/sh/version_compatiable.inc
)

install(FILES ${SCRIPTS_FILES}
    DESTINATION share/info/hixl/script
)
set(COMMON_FILES
    ${CMAKE_SOURCE_DIR}/scripts/package/common/sh/install_common_parser.sh
    ${CMAKE_SOURCE_DIR}/scripts/package/common/sh/common_func_v2.inc
    ${CMAKE_SOURCE_DIR}/scripts/package/common/sh/common_installer.inc
    ${CMAKE_SOURCE_DIR}/scripts/package/common/sh/script_operator.inc
    ${CMAKE_SOURCE_DIR}/scripts/package/common/sh/version_cfg.inc
)

set(PACKAGE_FILES
    ${COMMON_FILES}
    ${CMAKE_SOURCE_DIR}/scripts/package/common/sh/multi_version.inc
)
set(LATEST_MANGER_FILES
    ${COMMON_FILES}
    ${CMAKE_SOURCE_DIR}/scripts/package/common/sh/common_func.inc
    ${CMAKE_SOURCE_DIR}/scripts/package/common/sh/version_compatiable.inc
    ${CMAKE_SOURCE_DIR}/scripts/package/common/sh/check_version_required.awk
)
set(CONF_FILES
    ${CMAKE_SOURCE_DIR}/scripts/package/common/cfg/path.cfg
)

install(FILES ${HIXL_VERSION_FILE}
    DESTINATION share/info/hixl
)
install(FILES ${CONF_FILES}
    DESTINATION hixl/conf
)
install(FILES ${PACKAGE_FILES}
    DESTINATION share/info/hixl/script
)
install(FILES ${LATEST_MANGER_FILES}
    DESTINATION latest_manager
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/scripts/package/latest_manager/scripts/
    DESTINATION latest_manager
)
set(BIN_FILES
    ${CMAKE_SOURCE_DIR}/scripts/package/hixl/scripts/prereq_check.bash
    ${CMAKE_SOURCE_DIR}/scripts/package/hixl/scripts/prereq_check.csh
    ${CMAKE_SOURCE_DIR}/scripts/package/hixl/scripts/prereq_check.fish
    ${CMAKE_SOURCE_DIR}/scripts/package/hixl/scripts/setenv.bash
    ${CMAKE_SOURCE_DIR}/scripts/package/hixl/scripts/setenv.csh
    ${CMAKE_SOURCE_DIR}/scripts/package/hixl/scripts/setenv.fish
)
install(FILES ${BIN_FILES}
    DESTINATION share/info/hixl/bin
)

set(hixl_include ${CMAKE_SOURCE_DIR}/include)
install(DIRECTORY ${hixl_include}/
    DESTINATION hixl/include
    FILE_PERMISSIONS
    OWNER_READ OWNER_WRITE
    GROUP_READ GROUP_EXECUTE
)
install(TARGETS llm_datadist
        LIBRARY DESTINATION hixl/lib)

# ============= CPack =============
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}")

set(CPACK_INSTALL_PREFIX "/")

set(CPACK_CMAKE_SOURCE_DIR "${CMAKE_SOURCE_DIR}")
set(CPACK_CMAKE_BINARY_DIR "${CMAKE_BINARY_DIR}")
set(CPACK_CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
set(CPACK_CMAKE_CURRENT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CPACK_MAKESELF_PATH "${MAKESELF_PATH}")
set(CPACK_ARCH "${ARCH}")
set(CPACK_SET_DESTDIR ON)
set(CPACK_GENERATOR External)
set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/makeself.cmake")
set(CPACK_EXTERNAL_ENABLE_STAGING true)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")

message(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")
include(CPack)