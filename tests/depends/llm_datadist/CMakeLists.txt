# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

file(GLOB LLM_V2_SRC_LIST
        "${HIXL_CODE_DIR}/src/llm_datadist/api/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/adxl/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/common/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/link_mgr/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/cache_mgr/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/hccl/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/common/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/fsm/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/utils/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/data_transfer/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/allocator/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/config/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/span/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/type/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/util/*.cc"
        "${HIXL_CODE_DIR}/tests/depends/llm_datadist/src/mmpa_stub.cc"
        "${HIXL_CODE_DIR}/tests/depends/llm_datadist/src/hccl_stub.cc"
        "${HIXL_CODE_DIR}/tests/depends/llm_datadist/src/ascend_string_stub.cc"
)
message("LLM_V2_SRC_LIST = ${LLM_V2_SRC_LIST}")

add_library(llm_datadist_base SHARED
        ${LLM_V2_SRC_LIST}
)

target_include_directories(llm_datadist_base PRIVATE
        ${HIXL_CODE_DIR}/include
        ${HIXL_CODE_DIR}/tests/framework/ge_running_env/include
        ${HIXL_CODE_DIR}/src/llm_datadist
        ${HIXL_CODE_DIR}/src/llm_datadist/common
        ${HIXL_CODE_DIR}/tests/
        ${HIXL_CODE_DIR}/tests/depends/llm_datadist/src
)

target_link_libraries(llm_datadist_base PRIVATE
        $<BUILD_INTERFACE:mmpa_headers>
        $<BUILD_INTERFACE:hccl_headers>
        $<BUILD_INTERFACE:runtime_headers>
        metadef_headers
        intf_pub
        PUBLIC c_sec
        slog_stub
        runtime_stub
        mmpa_stub
        json
        msprof_headers
        ${COMMON_LINK_OPTION}
)

target_compile_options(llm_datadist_base PRIVATE
        ${DT_COMMON_COMPILE_OPTION}
)