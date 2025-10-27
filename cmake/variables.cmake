# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

if(PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  message(STATUS "compile project with library")
  option(BUILD_WITH_INSTALLED_DEPENDENCY_CANN_PKG "Build hixl with cann pkg" ON)
else()
  message(STATUS "compile project with src")
  option(BUILD_WITH_INSTALLED_DEPENDENCY_CANN_PKG "Build hixl with cann source" OFF)
endif()

if(BUILD_WITH_INSTALLED_DEPENDENCY_CANN_PKG)
    if ("$ENV{USER}" STREQUAL "root")
        set(ASCEND_DEFAULT_PATH /usr/local/Ascend)
    else()
        set(ASCEND_DEFAULT_PATH $ENV{HOME}/Ascend)
    endif()

    if (DEFINED ENV{ASCEND_HOME_PATH})
        set(CANN_INSTALL_PATH $ENV{ASCEND_HOME_PATH})
    else()
        if (EXISTS ${ASCEND_DEFAULT_PATH}/ascend-toolkit/latest)
            set(CANN_INSTALL_PATH ${ASCEND_DEFAULT_PATH}/ascend-toolkit/latest)
        elseif (EXISTS ${ASCEND_DEFAULT_PATH}/latest)
            set(CANN_INSTALL_PATH ${ASCEND_DEFAULT_PATH}/latest)
        else()
            message(FATAL_ERROR "Please set CANN_INSTALL_PATH or env:ASCEND_HOME_PATH")
        endif()
    endif()
    message("CANN_INSTALL_PATH:${CANN_INSTALL_PATH}")

    if (NOT DEFINED CANN_3RD_LIB_PATH)
        set(CANN_3RD_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
        file(MAKE_DIRECTORY "${CANN_3RD_LIB_PATH}")
    endif()
    message("CANN_3RD_LIB_PATH:${CANN_3RD_LIB_PATH}")
    set(INSTALL_LIBRARY_DIR hixl/lib)
else()
    set(INSTALL_LIBRARY_DIR lib)
endif()

set(CMAKE_MODULE_PATH
    ${PROJECT_SOURCE_DIR}/cmake/modules
    ${CMAKE_MODULE_PATH}
)
message("CMAKE_MODULE_PATH:${CMAKE_MODULE_PATH}")

set(CMAKE_PREFIX_PATH ${CANN_INSTALL_PATH})
message("CMAKE_PREFIX_PATH:${CMAKE_PREFIX_PATH}")
message("CMAKE_INSTALL_PREFIX:${CMAKE_INSTALL_PREFIX}")

if (NOT DEFINED CMAKE_BUILD_TYPE)
    if (ENABLE_TEST)
        set(CMAKE_BUILD_TYPE "DT")
    else ()
        set(CMAKE_BUILD_TYPE "Release")
    endif()
endif()
message("CMAKE_BUILD_TYPE:${CMAKE_BUILD_TYPE}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE True)
set(HIXL_CODE_DIR ${PROJECT_SOURCE_DIR})
set(HI_PYTHON python3)
set(TARGET_SYSTEM_NAME "Linux")

if (CMAKE_BUILD_TYPE MATCHES GCOV)
    set(DT_COMMON_COMPILE_OPTION
            -O0
            -g
            --coverage -fprofile-arcs -ftest-coverage
            -fsanitize=address -fsanitize=leak -fsanitize-recover=address
            )
    set(COV_COMPILE_OPTION
            --coverage -fprofile-arcs -ftest-coverage
            )
    if (TARGET_SYSTEM_NAME STREQUAL "Android")
        set(COMMON_LINK_OPTION
                -fsanitize=address -fsanitize=leak -fsanitize-recover=address
                -ldl -lgcov
                )
    else ()
        set(COMMON_LINK_OPTION
                -fsanitize=address -fsanitize=leak -fsanitize-recover=address
                -lrt -ldl -lgcov
                )
    endif ()
elseif(CMAKE_BUILD_TYPE MATCHES DT)
    set(DT_COMMON_COMPILE_OPTION -O0 -g)
    set(COV_COMPILE_OPTION ${COMMON_COMPILE_OPTION})
else ()
    if (TARGET_SYSTEM_NAME STREQUAL "Windows")
        if (CMAKE_CONFIGURATION_TYPES STREQUAL "Debug")
            set(COMMON_COMPILE_OPTION /MTd)
        else ()
            set(COMMON_COMPILE_OPTION /MT)
        endif ()

    else ()
        set(COMMON_COMPILE_OPTION -fvisibility=hidden -O2 -Werror -fno-common -Wextra -Wfloat-equal)
    endif ()
endif ()
message("common compile options ${COMMON_COMPILE_OPTION}")
message("common link options ${COMMON_LINK_OPTION}")

