# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

set(LLM_DATADIST_WRAPPER_SRC_FILES
        "${HIXL_CODE_DIR}/src/python/llm_wrapper/llm_wrapper_v2.cc"
        "${HIXL_CODE_DIR}/src/python/llm_wrapper/llm_datadist_v2_wrapper.cc")

add_library(llm_datadist_wrapper_stub MODULE
        ${LLM_DATADIST_WRAPPER_SRC_FILES}
)

target_include_directories(llm_datadist_wrapper_stub PRIVATE
        ${HI_PYTHON_INC}
        ${pybind11_INCLUDE_DIR}
        ${HIXL_CODE_DIR}/include
        ${HIXL_CODE_DIR}/src/llm_datadist
        ${HIXL_CODE_DIR}/tests/depends/llm_datadist/src
)

target_link_libraries(llm_datadist_wrapper_stub PRIVATE
        intf_pub
        metadef_headers
        runtime_headers
        llm_datadist_base
        -Wl,-as-needed
        ${DT_COMMON_LINK_OPTION}
)

set_target_properties(llm_datadist_wrapper_stub
        PROPERTIES
        PREFIX ""
)

set_target_properties(llm_datadist_wrapper_stub PROPERTIES OUTPUT_NAME "llm_datadist_wrapper")

target_compile_options(llm_datadist_wrapper_stub PRIVATE
        ${DT_COMMON_COMPILE_OPTION}
        -Xlinker -export-dynamic
)

add_library(metadef_wrapper_stub MODULE
        "${HIXL_CODE_DIR}/src/python/metadef_wrapper/metadef_wrapper.cc"
)

target_include_directories(metadef_wrapper_stub PRIVATE
        ${HI_PYTHON_INC}
        ${pybind11_INCLUDE_DIR}
        ${HIXL_CODE_DIR}/include
)

target_link_libraries(metadef_wrapper_stub PRIVATE
        intf_pub
        metadef_headers
        ${DT_COMMON_LINK_OPTION}
)

set_target_properties(metadef_wrapper_stub
        PROPERTIES
        PREFIX ""
)

set_target_properties(metadef_wrapper_stub PROPERTIES OUTPUT_NAME "metadef_wrapper")

target_compile_options(metadef_wrapper_stub PRIVATE
        ${DT_COMMON_COMPILE_OPTION}
        -Xlinker -export-dynamic
)
