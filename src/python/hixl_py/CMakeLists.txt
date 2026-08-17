# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

if (NOT ENABLE_TEST)
    set(CMAKE_SKIP_RPATH TRUE)

    add_library(hixl MODULE
            hixl_py.cc)

    target_include_directories(hixl PRIVATE
            ${HI_PYTHON_INC}
            ${pybind11_INCLUDE_DIR}
            ${HIXL_CODE_DIR}/include
            ${HIXL_CODE_DIR}/src/hixl
    )

    target_link_libraries(hixl PRIVATE
            $<BUILD_INTERFACE:intf_pub>
            $<BUILD_INTERFACE:hccl_headers>
            $<BUILD_INTERFACE:slog_headers>
            $<BUILD_INTERFACE:metadef_headers>
            $<BUILD_INTERFACE:msprof_headers>
            alog
            cann_hixl
    )

    set_target_properties(hixl PROPERTIES
            PREFIX ""
            SUFFIX ".so"
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
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hixl-0.0.1-py3-none-any.whl
    COMMAND ${CMAKE_COMMAND} -E echo "package hixl whl start"
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/setup.py ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl/setup.py
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/MANIFEST.in ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl/
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/hixl ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl/hixl
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/hixl.so ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl/hixl/
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl ${HI_PYTHON} setup.py bdist_wheel
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/wheel_hixl/dist/hixl-0.0.1-py3-none-any.whl ${CMAKE_CURRENT_BINARY_DIR}/
    COMMAND ${CMAKE_COMMAND} -E echo "package hixl whl end"
    DEPENDS version_hixl_info hixl
)

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/hixl-0.0.1-py3-none-any.whl
            DESTINATION ${INSTALL_LIBRARY_DIR}
            COMPONENT hixl
    )
endif ()
