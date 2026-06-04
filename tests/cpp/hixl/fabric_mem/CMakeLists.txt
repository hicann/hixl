# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

set(FABRIC_MEM_TEST_FILES
        fabric_mem_unittest.cc
        virtual_memory_manager_unittest.cc
        fabric_mem_st.cc
)

file(GLOB FABRIC_MEM_SRC_LIST
        "${HIXL_CODE_DIR}/src/hixl/common/*.cc"
        "${HIXL_CODE_DIR}/src/hixl/fabric_mem/*.cc"
        "${HIXL_CODE_DIR}/src/hixl/profiling/*.cc"
)

set(FABRIC_MEM_ENGINE_SRC
        "${HIXL_CODE_DIR}/src/hixl/engine/fabric_mem_engine.cc"
        "${HIXL_CODE_DIR}/src/hixl/engine/hixl_options.cc"
)

set(FABRIC_MEM_STUB_SRC_FILES
        "${HIXL_CODE_DIR}/tests/depends/llm_datadist/src/ascend_string_stub.cc"
)

add_executable(fabric_mem_test
        ${FABRIC_MEM_TEST_FILES}
        ${FABRIC_MEM_STUB_SRC_FILES}
        ${FABRIC_MEM_SRC_LIST}
        ${FABRIC_MEM_ENGINE_SRC}
)

target_compile_options(fabric_mem_test PRIVATE
        ${DT_COMMON_COMPILE_OPTION}
        -Wno-deprecated-declarations
        -Wall -Wfloat-equal
)

target_compile_definitions(fabric_mem_test PRIVATE
        HIXL_TEST_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_include_directories(fabric_mem_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${HIXL_CODE_DIR}/include
        ${HIXL_CODE_DIR}/src/hixl
        ${HIXL_CODE_DIR}/src/hixl/common
        ${HIXL_CODE_DIR}/src/hixl/engine
        ${HIXL_CODE_DIR}/src/hixl/profiling
        ${HIXL_CODE_DIR}/src/llm_datadist
        ${HIXL_CODE_DIR}/tests
        ${HIXL_CODE_DIR}/tests/depends/ascendcl/src
)

set_target_properties(fabric_mem_test PROPERTIES CXX_STANDARD 17)

target_link_libraries(fabric_mem_test
        ${COMMON_LINK_OPTION}
        c_sec
        mmpa_headers
        runtime_headers
        metadef_headers
        intf_pub
        json
        -ldl
        slog_stub
        mmpa_stub
        msprof_stub
        ascendcl_stub
        runtime_stub
        msprof_headers
        GTest::gtest GTest::gtest_main
        GTest::gmock
        stdc++fs
        -Wl,--no-as-needed
        error_manager_stub
        -Wl,--as-needed)

include(CTest)
enable_testing()
