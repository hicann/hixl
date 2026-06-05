# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

set(ADXL_CORE_TEST_FILES
        adxl_engine_unittest.cc
        buffer_transfer_service_unittest.cc
        channel_manager_unittest.cc
        channel_msg_handler_unittest.cc
        control_msg_handler_unittest.cc
        control_msg_handler_write_unittest.cc
        segment_table_unittest.cc
        statistic_manager_unittest.cc
        test_adxl_engine_api.cc
)

set(CHANNEL_POOL_TEST_FILES
        channel_pool_system_test.cc
        channel_pool_unit_test.cc
)

set(ADXL_STUB_SRC_FILES
        "${HIXL_CODE_DIR}/tests/depends/llm_datadist/src/data_cache_engine_test_helper.cc"
        "${HIXL_CODE_DIR}/tests/depends/llm_datadist/src/ascend_string_stub.cc"
        "${HIXL_CODE_DIR}/tests/depends/llm_datadist/src/hccl_stub.cc"
)

file(GLOB ADXL_SRC_LIST
        "${HIXL_CODE_DIR}/src/llm_datadist/api/adxl_engine_impl.cc"
        "${HIXL_CODE_DIR}/src/llm_datadist/comm_statistic_manager.cc"
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

file(GLOB HIXL_SRC_LIST
        "${HIXL_CODE_DIR}/src/hixl/common/*.cc"
        "${HIXL_CODE_DIR}/src/hixl/fabric_mem/*.cc"
        "${HIXL_CODE_DIR}/src/hixl/engine/*.cc"
        "${HIXL_CODE_DIR}/src/hixl/cs/*.cc"
        "${HIXL_CODE_DIR}/src/hixl/profiling/*.cc"
        "${HIXL_CODE_DIR}/src/hixl/proxy/*.cc"
)

function(add_adxl_family_test target_name test_files)
  add_executable(${target_name}
          ${test_files}
          ${ADXL_STUB_SRC_FILES}
          ${ADXL_SRC_LIST}
          ${HIXL_SRC_LIST}
  )

  target_compile_options(${target_name} PRIVATE
          ${DT_COMMON_COMPILE_OPTION}
          -Wno-deprecated-declarations
          -Wall -Wfloat-equal
  )

  target_compile_definitions(${target_name} PRIVATE
          HIXL_SKIP_DATA_CACHE_ENGINE_TEST_CONTEXT
  )

  target_include_directories(${target_name} PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_CURRENT_SOURCE_DIR}/../common
          ${CMAKE_CURRENT_SOURCE_DIR}/../hixl/engine
          ${HIXL_CODE_DIR}/include
          ${HIXL_CODE_DIR}/src/llm_datadist
          ${HIXL_CODE_DIR}/src/hixl
          ${HIXL_CODE_DIR}/src/hixl/common
          ${HIXL_CODE_DIR}/src/hixl/cs
          ${HIXL_CODE_DIR}/src/hixl/engine
          ${HIXL_CODE_DIR}/src/hixl/profiling
          ${HIXL_CODE_DIR}/src/hixl/proxy
          ${HIXL_CODE_DIR}/tests
          ${HIXL_CODE_DIR}/tests/depends/ascendcl/src
          ${HIXL_CODE_DIR}/tests/depends/hccl/src
          ${HIXL_CODE_DIR}/tests/depends/llm_datadist/src
          ${HIXL_CODE_DIR}/tests/depends/slog/src
  )

  set_target_properties(${target_name} PROPERTIES CXX_STANDARD 17)

  target_link_libraries(${target_name}
          ${COMMON_LINK_OPTION}
          c_sec
          mmpa_headers
          hccl_headers
          acl_rt_headers
          runtime_headers
          metadef_headers
          intf_pub
          json
          -ldl
          slog_stub
          mmpa_stub
          ascendcl_stub
          runtime_stub
          msprof_stub
          msprof_headers
          dcmi_stub
          GTest::gtest GTest::gtest_main
          GTest::gmock
          stdc++fs
          -Wl,--no-as-needed
          hccl_stub
          error_manager_stub
          -Wl,--as-needed)

  set_target_properties(${target_name} PROPERTIES
          BUILD_RPATH "${CMAKE_BINARY_DIR}/tests/depends/dcmi"
  )
endfunction()

add_adxl_family_test(adxl_test "${ADXL_CORE_TEST_FILES}")
add_adxl_family_test(channel_pool_test "${CHANNEL_POOL_TEST_FILES}")

include(CTest)
enable_testing()
