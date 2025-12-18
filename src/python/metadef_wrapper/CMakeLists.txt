# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

if (NOT ENABLE_TEST)
    set(CMAKE_SKIP_RPATH TRUE)
    add_library(metadef_wrapper MODULE
            metadef_wrapper.cc
    )

    target_include_directories(metadef_wrapper PRIVATE
            ${HI_PYTHON_INC}
            ${pybind11_INCLUDE_DIR}
            ${HIXL_CODE_DIR}/inc
            ${HIXL_CODE_DIR}/include
    )

    target_link_libraries(metadef_wrapper PRIVATE
            $<BUILD_INTERFACE:intf_pub>
            $<BUILD_INTERFACE:slog_headers>
            $<BUILD_INTERFACE:mmpa_headers>
            $<BUILD_INTERFACE:metadef_headers>
            alog
    )

    set_target_properties(metadef_wrapper
            PROPERTIES
            PREFIX ""
    )

    target_compile_definitions(metadef_wrapper PRIVATE
            PYBIND11_NO_ASSERT_GIL_HELD_INCREF_DECREF
    )

    target_compile_options(metadef_wrapper PRIVATE
            -Xlinker -export-dynamic
    )

    target_link_options(metadef_wrapper PRIVATE
            -s
    )
endif ()