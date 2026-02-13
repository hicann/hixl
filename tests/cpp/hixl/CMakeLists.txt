# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

set(HIXL_TEST_FILES
        cs/test_hixl_cs_server.cc
        cs/hixl_cs_client_ut.cc
        cs/hixl_mem_store_basic_ut.cc
        cs/hixl_cs_client_basic_ut.cc
        engine/hixl_server_unittest.cpp
        engine/hixl_client_unittest.cc
        engine/hixl_utils_unittest.cc
        engine/hixl_engine_unittest.cc
)

file(GLOB HIXL_SRC_LIST
        "${HIXL_CODE_DIR}/src/hixl/common/*.cc"
        "${HIXL_CODE_DIR}/src/hixl/engine/*.cc"
        "${HIXL_CODE_DIR}/src/hixl/cs/*.cc"
)

file(GLOB ADXL_SRC_LIST
        "${HIXL_CODE_DIR}/src/llm_datadist/api/adxl_engine_impl.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/statistic_manager.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/llm_datadist_timer.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/adxl/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/common/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/hccl/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/allocator/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/config/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/span/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/type/*.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/memory/util/*.cc"
)

set(HIXL_STUB_SRC_FILES
        "${HIXL_CODE_DIR}/tests/depends/llm_datadist/src/ascend_string_stub.cc"
)

add_executable(hixl_test
        ${HIXL_TEST_FILES}
        ${HIXL_STUB_SRC_FILES}
        ${ADXL_SRC_LIST}
        ${HIXL_SRC_LIST}
        )

target_compile_options(hixl_test PRIVATE
        ${DT_COMMON_COMPILE_OPTION}
        -Wno-deprecated-declarations
        -Wall -Wfloat-equal
        )

target_include_directories(hixl_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HIXL_CODE_DIR}/include
        ${HIXL_CODE_DIR}/src/hixl
        ${HIXL_CODE_DIR}/src/llm_datadist
        ${HIXL_CODE_DIR}/src/hixl/cs
        ${HIXL_CODE_DIR}/src/hixl/engine
        ${HIXL_CODE_DIR}/src/ops
        ${HIXL_CODE_DIR}/tests
        )

set_target_properties(hixl_test PROPERTIES CXX_STANDARD 17)

target_link_libraries(hixl_test
        ${COMMON_LINK_OPTION}
        c_sec
        mmpa_headers
        hccl_headers
        metadef_headers
        intf_pub
        json
        -ldl
        slog_stub
        mmpa_stub
        ascendcl_stub
        msprof_headers
        GTest::gtest GTest::gtest_main
        GTest::gmock
        -Wl,--no-as-needed
        hccl_stub
        error_manager_stub
        -Wl,--as-needed)

include(CTest)
enable_testing()