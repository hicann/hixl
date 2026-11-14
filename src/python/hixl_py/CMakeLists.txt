# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------
# 单文件双态: 仓内构建 (默认) 与 wheel 在线编译共用本文件, 消除双轨 CMakeLists 漂移.
# 出包时 gen_manifest.py 按 [hixl:online-begin/end] 标记提取在线段 (含 license 头)
# 生成随包 _src/CMakeLists.txt —— 随包文件不含仓内分支与开关脚手架, 独立配置即在线编译.
# 在线段变量由 _build_online.py 通过 -D 传入:
#   Python3_EXECUTABLE / Python3_EXECUTABLE_VERSION / PY_EXT_SUFFIX
#   PYBIND11_CMAKE_DIR / CANN_INCLUDE_DIR / CANN_LIB_DIR
# 绑定层仅依赖 hixl 公共头 (hixl/hixl.h, hixl/hixl_types.h), 内部头与 pkg_inc 均不依赖.
option(HIXL_ONLINE_STANDALONE "Build hixl binding standalone (wheel online-compile path)" OFF)
if (HIXL_ONLINE_STANDALONE)
# ---- [hixl:online-begin] ----
    cmake_minimum_required(VERSION 3.16)
    project(hixl_python_binding_online LANGUAGES CXX)

    if ("${PY_EXT_SUFFIX}x" STREQUAL "x")
        message(FATAL_ERROR "PY_EXT_SUFFIX not defined, can't build hixl binding online")
    endif ()
    if ("${Python3_EXECUTABLE}x" STREQUAL "x")
        message(FATAL_ERROR "Python3_EXECUTABLE not defined, can't build hixl binding online")
    endif ()
    if ("${Python3_EXECUTABLE_VERSION}x" STREQUAL "x")
        message(FATAL_ERROR "Python3_EXECUTABLE_VERSION not defined, can't build hixl binding online")
    endif ()
    if (NOT EXISTS "${CANN_INCLUDE_DIR}")
        message(FATAL_ERROR "CANN_INCLUDE_DIR not found: ${CANN_INCLUDE_DIR}")
    endif ()
    if (NOT EXISTS "${CANN_LIB_DIR}")
        message(FATAL_ERROR "CANN_LIB_DIR not found: ${CANN_LIB_DIR}")
    endif ()

    # 锚定用户解释器前缀并强制版本精确匹配, 防止 CMake 在系统路径
    # (如 /usr/local 下残留的其他版本 Python 开发文件) 误发现错误头文件
    get_filename_component(_HIXL_PY3_EXECUTABLE "${Python3_EXECUTABLE}" REALPATH)
    get_filename_component(_HIXL_PY3_ROOT_DIR "${_HIXL_PY3_EXECUTABLE}" DIRECTORY)
    get_filename_component(_HIXL_PY3_ROOT_DIR "${_HIXL_PY3_ROOT_DIR}/../" REALPATH)
    set(Python3_ROOT_DIR "${_HIXL_PY3_ROOT_DIR}" CACHE INTERNAL "Python3 root path" FORCE)
    message(STATUS "Python3_ROOT_DIR=${Python3_ROOT_DIR}")

    set(CMAKE_CXX_STANDARD 17)  # std::shared_mutex 依赖 C++17
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    find_package(Python3 ${Python3_EXECUTABLE_VERSION} EXACT REQUIRED COMPONENTS Development)
    if (NOT EXISTS "${Python3_INCLUDE_DIRS}/Python.h")
        message(FATAL_ERROR "Python.h not found under ${Python3_INCLUDE_DIRS},"
                " refuse to build with wrong python headers")
    endif ()

    find_package(pybind11 CONFIG REQUIRED PATHS ${PYBIND11_CMAKE_DIR} NO_DEFAULT_PATH)

    add_library(hixl MODULE
            hixl_py.cc)

    target_include_directories(hixl PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}      # hixl_py.h
            ${CANN_INCLUDE_DIR}            # hixl/ external/ge_common/ 公共头
            ${Python3_INCLUDE_DIRS}
    )

    # 安全与 ABI 选项, 对齐仓内构建 intf_pub 携带的加固基线
    target_compile_definitions(hixl PRIVATE
            PYBIND11_NO_ASSERT_GIL_HELD_INCREF_DECREF
            _GLIBCXX_USE_CXX11_ABI=0
    )
    target_compile_options(hixl PRIVATE
            -O2
            -fno-strict-aliasing
            -fstack-protector-strong
            -Xlinker -export-dynamic
    )
    target_link_options(hixl PRIVATE
            -Wl,-z,relro
            -Wl,-z,now
            -Wl,-z,noexecstack
    )

    target_link_libraries(hixl PRIVATE
            pybind11::module
            cann_hixl
            metadef
    )

    target_link_directories(hixl PRIVATE
            ${CANN_LIB_DIR}
    )

    target_link_options(hixl PRIVATE
            -s
    )

    set_target_properties(hixl PROPERTIES
            PREFIX ""
            OUTPUT_NAME "hixl"
            SUFFIX "${PY_EXT_SUFFIX}"
    )

    install(TARGETS hixl
            LIBRARY DESTINATION "."
    )
# ---- [hixl:online-end] ----
    return()
endif ()

if (NOT ENABLE_TEST)
    set(CMAKE_SKIP_RPATH TRUE)

    # 计算当前解释器的扩展模块后缀 (如 .cpython-311-x86_64-linux-gnu.so),
    # 使预编译产物仅在匹配的 Python 版本下被 import 命中, 其余版本由在线编译兜底
    if (Python3_EXECUTABLE)
        set(HIXL_PY_INTERPRETER ${Python3_EXECUTABLE})
    else ()
        set(HIXL_PY_INTERPRETER ${HI_PYTHON})
    endif ()
    execute_process(
            COMMAND ${HIXL_PY_INTERPRETER} -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX'))"
            OUTPUT_VARIABLE PY_EXT_SUFFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE PY_EXT_SUFFIX_RESULT
    )
    if (NOT PY_EXT_SUFFIX_RESULT EQUAL 0 OR "${PY_EXT_SUFFIX}x" STREQUAL "x")
        message(FATAL_ERROR "Can not get python extension suffix, check your python environment.")
    endif ()
    message(STATUS "hixl python extension suffix: ${PY_EXT_SUFFIX}")

    add_library(hixl MODULE
            hixl_py.cc)

    target_include_directories(hixl PRIVATE
            ${HI_PYTHON_INC}
            ${HIXL_CODE_DIR}/include
    )

    target_link_libraries(hixl PRIVATE
            $<BUILD_INTERFACE:intf_pub>
            $<BUILD_INTERFACE:metadef_headers>
            pybind11
            cann_hixl
    )
    # CMake 3.16 does not propagate INTERFACE add_dependencies to consumers.
    if(TARGET pybind11_build)
        add_dependencies(hixl pybind11_build)
    endif()

    set_target_properties(hixl PROPERTIES
            PREFIX ""
            SUFFIX "${PY_EXT_SUFFIX}"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )

    target_compile_definitions(hixl PRIVATE
            PYBIND11_NO_ASSERT_GIL_HELD_INCREF_DECREF
    )

    target_compile_options(hixl PRIVATE
            -Xlinker -export-dynamic
    )

    target_link_options(hixl PRIVATE
            -s
    )

add_custom_target(hixl_python ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/hixl-0.0.1-py3-none-any.whl)

if (ENABLE_EXPERIMENTAL AND EXISTS "${HIXL_CODE_DIR}/src/experimental/python")
    set(EXPERIMENTAL_COPY_CMD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${HIXL_CODE_DIR}/src/experimental/python
            ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl
    )
    set(EXPERIMENTAL_PYTHON_DEPS ${HIXL_CODE_DIR}/src/experimental/python)
endif()

# 在线编译构建信息 (版本 + 构建时间戳), 供运行时缓存 key 隔离
# HIXL_PY_VERSION 与 setup.py 的 setup(version=...) 保持一致 (wheel 文件名依赖两者), 升版本需同步修改
set(HIXL_PY_VERSION 0.0.1)
string(TIMESTAMP HIXL_BUILD_TIMESTAMP "%Y%m%d_%H%M%S" UTC)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/_build_info.py.in
        ${CMAKE_CURRENT_BINARY_DIR}/_build_info.py @ONLY)

# 随包发布的在线编译源码目录 (wheel 内 hixl/_src)
set(HIXL_ONLINE_SRC_DIR ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl/hixl/_src)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hixl-0.0.1-py3-none-any.whl
    COMMAND ${CMAKE_COMMAND} -E echo "package hixl whl start"
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/setup.py ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl/setup.py
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/MANIFEST.in ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl/
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/hixl ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl/hixl
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/hixl${PY_EXT_SUFFIX} ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl/hixl/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/_build_info.py ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl/hixl/_build_info.py
    COMMAND ${CMAKE_COMMAND} -E make_directory ${HIXL_ONLINE_SRC_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/hixl_py.cc ${HIXL_ONLINE_SRC_DIR}/hixl_py.cc
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/hixl_py.h ${HIXL_ONLINE_SRC_DIR}/hixl_py.h
    COMMAND ${HIXL_PY_INTERPRETER} ${CMAKE_CURRENT_SOURCE_DIR}/gen_manifest.py ${HIXL_ONLINE_SRC_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
            --prebuilt-so ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl/hixl/hixl${PY_EXT_SUFFIX}
            --py-ext-suffix ${PY_EXT_SUFFIX}
            --build-timestamp ${HIXL_BUILD_TIMESTAMP}
    ${EXPERIMENTAL_COPY_CMD}
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl ${HI_PYTHON} setup.py bdist_wheel
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl/dist/hixl-0.0.1-py3-none-any.whl ${CMAKE_CURRENT_BINARY_DIR}/
    COMMAND ${CMAKE_COMMAND} -E echo "package hixl whl end"
DEPENDS version_hixl_info hixl ${EXPERIMENTAL_PYTHON_DEPS}
            ${CMAKE_CURRENT_SOURCE_DIR}/hixl/_build_online.py
            ${CMAKE_CURRENT_SOURCE_DIR}/hixl/__init__.py
            ${CMAKE_CURRENT_SOURCE_DIR}/gen_manifest.py
            ${CMAKE_CURRENT_SOURCE_DIR}/hixl_py.cc
            ${CMAKE_CURRENT_SOURCE_DIR}/hixl_py.h
            ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    )

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/hixl-0.0.1-py3-none-any.whl
            DESTINATION ${INSTALL_LIBRARY_DIR}
            COMPONENT hixl
    )
endif ()
