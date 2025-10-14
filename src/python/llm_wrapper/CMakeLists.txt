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
    add_library(llm_datadist_wrapper MODULE
            llm_wrapper_v2.cc
            llm_datadist_v2_wrapper.cc)

    target_include_directories(llm_datadist_wrapper PRIVATE
            ${HI_PYTHON_INC}
            ${pybind11_INCLUDE_DIR}
            ${HIXL_CODE_DIR}/include
            ${HIXL_CODE_DIR}/src/llm_datadist
    )

    target_link_libraries(llm_datadist_wrapper PRIVATE
            $<BUILD_INTERFACE:intf_pub>
            $<BUILD_INTERFACE:slog_headers>
            $<BUILD_INTERFACE:mmpa_headers>
            $<BUILD_INTERFACE:hccl_headers>
            $<BUILD_INTERFACE:metadef_headers>
            $<BUILD_INTERFACE:msprof_headers>
            alog
            llm_datadist
    )

    set_target_properties(llm_datadist_wrapper
            PROPERTIES
            PREFIX ""
    )

    target_compile_definitions(llm_datadist_wrapper PRIVATE
            PYBIND11_NO_ASSERT_GIL_HELD_INCREF_DECREF
    )

    target_compile_options(llm_datadist_wrapper PRIVATE
            -O2
            -Xlinker -export-dynamic
    )

    target_link_options(llm_datadist_wrapper PRIVATE
            -s
    )
endif ()
