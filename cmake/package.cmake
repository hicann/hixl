# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
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

add_cann_third_party(makeself-fetch)

set(script_prefix ${CMAKE_CURRENT_SOURCE_DIR}/scripts/package/hixl/scripts)
install(DIRECTORY ${script_prefix}/
    DESTINATION share/info/hixl/script
    COMPONENT hixl
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
    ${CANN_CMAKE_DIR}/scripts/install/check_version_required.awk
    ${CANN_CMAKE_DIR}/scripts/install/common_func.inc
    ${CANN_CMAKE_DIR}/scripts/install/common_interface.sh
    ${CANN_CMAKE_DIR}/scripts/install/common_interface.csh
    ${CANN_CMAKE_DIR}/scripts/install/common_interface.fish
    ${CANN_CMAKE_DIR}/scripts/install/version_compatiable.inc
)

install(FILES ${SCRIPTS_FILES}
    DESTINATION share/info/hixl/script
    COMPONENT hixl
)
set(COMMON_FILES
    ${CANN_CMAKE_DIR}/scripts/install/install_common_parser.sh
    ${CANN_CMAKE_DIR}/scripts/install/common_func_v2.inc
    ${CANN_CMAKE_DIR}/scripts/install/common_installer.inc
    ${CANN_CMAKE_DIR}/scripts/install/script_operator.inc
    ${CANN_CMAKE_DIR}/scripts/install/version_cfg.inc
)

set(PACKAGE_FILES
    ${COMMON_FILES}
    ${CANN_CMAKE_DIR}/scripts/install/multi_version.inc
)
set(CONF_FILES
    ${CANN_CMAKE_DIR}/scripts/package/cfg/path.cfg
)

install(FILES ${CMAKE_BINARY_DIR}/version.hixl.info
    DESTINATION share/info/hixl
    COMPONENT hixl
    RENAME version.info
)

install(FILES ${CONF_FILES}
    DESTINATION ${ARCH_LINUX_PATH}/conf
    COMPONENT hixl
)
install(FILES ${PACKAGE_FILES}
    DESTINATION share/info/hixl/script
    COMPONENT hixl
)

set(hixl_include ${CMAKE_SOURCE_DIR}/include)
install(DIRECTORY ${hixl_include}/cs
    DESTINATION ${ARCH_LINUX_PATH}/include
    COMPONENT hixl
    FILE_PERMISSIONS
    OWNER_READ OWNER_WRITE
    GROUP_READ GROUP_EXECUTE
)
install(DIRECTORY ${hixl_include}/adxl
    DESTINATION ${ARCH_LINUX_PATH}/include
    COMPONENT hixl
    FILE_PERMISSIONS
    OWNER_READ OWNER_WRITE
    GROUP_READ GROUP_EXECUTE
)
install(DIRECTORY ${hixl_include}/hixl
    DESTINATION ${ARCH_LINUX_PATH}/include
    COMPONENT hixl
    FILE_PERMISSIONS
    OWNER_READ OWNER_WRITE
    GROUP_READ GROUP_EXECUTE
)
install(FILES ${hixl_include}/llm_datadist/llm_datadist.h
    DESTINATION ${ARCH_LINUX_PATH}/include/llm_datadist
    COMPONENT hixl
)
install(FILES ${hixl_include}/llm_datadist/llm_error_codes.h
    DESTINATION ${ARCH_LINUX_PATH}/include/ge
    COMPONENT hixl
)
install(FILES ${hixl_include}/llm_datadist/llm_engine_types.h
    DESTINATION ${ARCH_LINUX_PATH}/include/ge
    COMPONENT hixl
)
install(TARGETS llm_datadist
        LIBRARY DESTINATION ${ARCH_LINUX_PATH}/lib64
        COMPONENT hixl
)

install(TARGETS cann_hixl
        LIBRARY DESTINATION ${ARCH_LINUX_PATH}/lib64
        COMPONENT hixl
)


install(FILES
    ${CMAKE_SOURCE_DIR}/build/device_install/hixl/aicpu_kernel/cann-hixl-compat.tar.gz
    DESTINATION opp/built-in/op_impl/aicpu/kernel
    COMPONENT hixl
)

install(FILES
    ${CMAKE_SOURCE_DIR}/build/device_install/hixl/aicpu_kernel/libcann_hixl_kernel.json
    DESTINATION opp/built-in/op_impl/aicpu/config
    COMPONENT hixl
)

# ============= CPack =============
if (NOT ENABLE_COV AND NOT ENABLE_UT)
    set_cann_cpack_config(hixl ENABLE_DEVICE ${ENABLE_DEVICE} SHARE_INFO_NAME hixl)
endif()