# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

file(GLOB ADXL_SRC_LIST
        "${HIXL_CODE_DIR}/src/llm_datadist/api/adxl_engine_impl.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/comm_statistic_manager.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/llm_datadist_timer.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/adxl/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/common/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/adapter_hccl/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/comm_adapter/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/allocator/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/config/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/span/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/type/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/util/*.cc"
)

file(GLOB HIXL_SRC_LIST
        "${HIXL_CODE_DIR}/src/hixl/common/*.cc"
        "${HIXL_CODE_DIR}/src/hixl/fabric_mem/*.cc"
        "${HIXL_CODE_DIR}/src/hixl/engine/*.cc"
        "${HIXL_CODE_DIR}/src/hixl/engine/endpoint_generator/*.cc"
        "${HIXL_CODE_DIR}/src/hixl/cs/*.cc"
        "${HIXL_CODE_DIR}/src/hixl/profiling/*.cc"
        "${HIXL_CODE_DIR}/src/hixl/proxy/*.cc"
)

set(HIXL_STUB_SRC_FILES
        "${HIXL_CODE_DIR}/tests/depends/llm_datadist/src/mmpa_stub.cc"
        "${HIXL_CODE_DIR}/tests/depends/llm_datadist/src/hccl_stub.cc"
        "${HIXL_CODE_DIR}/tests/depends/llm_datadist/src/ascend_string_stub.cc"
)

add_library(cann_hixl_base SHARED
        ${HIXL_SRC_LIST}
        ${ADXL_SRC_LIST}
        ${HIXL_STUB_SRC_FILES}
)

target_include_directories(cann_hixl_base PRIVATE
        ${HIXL_CODE_DIR}/include
        ${HIXL_CODE_DIR}/src/hixl
        ${HIXL_CODE_DIR}/src/hixl/common
        ${HIXL_CODE_DIR}/src/hixl/proxy
        ${HIXL_CODE_DIR}/src/llm_datadist
        ${HIXL_CODE_DIR}/tests
        ${HIXL_CODE_DIR}/tests/depends/llm_datadist/src
        ${HIXL_CODE_DIR}/tests/depends/ascendcl/src
        ${HIXL_CODE_DIR}/tests/depends/hccl/src
        ${HIXL_CODE_DIR}/tests/depends/slog/src
)

target_link_libraries(cann_hixl_base PRIVATE
        $<BUILD_INTERFACE:intf_pub>
        $<BUILD_INTERFACE:hccl_headers>
        $<BUILD_INTERFACE:slog_headers>
        $<BUILD_INTERFACE:acl_rt_headers>
        $<BUILD_INTERFACE:runtime_headers>
        $<BUILD_INTERFACE:msprof_headers>
        $<BUILD_INTERFACE:metadef_headers>
        $<BUILD_INTERFACE:error_manager_headers>
        PUBLIC c_sec
        slog_stub
        sys_api_wrap
        msprof_stub
        ascendcl_stub
        runtime_stub
        hccl_stub
        dcmi_stub
        drvdsmi_host_stub
        json
        -ldl
        -Wl,--no-as-needed
        hccl_stub
        error_manager_stub
        -Wl,--as-needed
        ${DT_COMMON_LINK_OPTION}
)

set_target_properties(cann_hixl_base PROPERTIES
        LINK_FLAGS "-Wl,--wrap=dlopen -Wl,--wrap=dlsym -Wl,--wrap=dlclose -Wl,--wrap=dlerror -Wl,--wrap=access -Wl,--wrap=realpath"
)

target_compile_definitions(cann_hixl_base PRIVATE
    HCCN_TEST_CONF_PATH="${HIXL_CODE_DIR}/tests/depends/llm_datadist/src/hccn_test.conf"
)

target_compile_options(cann_hixl_base PRIVATE
        ${DT_COMMON_COMPILE_OPTION}
)
