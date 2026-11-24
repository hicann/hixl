# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

# UB_MEM suite: everything that is specific to the UB_MEM transport and the fabric memory layer under it,
# from the memory internals (allocator, VMM, share handles, AICPU kernel) up to the CS-level integration
# (endpoint, channel, client, server). The CS-level cases need the same sources and stubs as the hixl
# suite, so this target reuses the source lists the parent directory already globbed.
set(UBMEM_TEST_FILES
        ubmem_runtime_stub.cc
        ubmem_unittest.cc
        ubmem_config_parser_unittest.cc
        ubmem_aicpu_kernel_unittest.cc
        ubmem_virtual_memory_manager_unittest.cc
        ubmem_aicpu_param_ut.cc
        ubmem_endpoint_ut.cc
        ubmem_cs_client_ut.cc
        ubmem_cs_server_ut.cc
)

set(UBMEM_STUB_SRC_FILES
        "${HIXL_CODE_DIR}/tests/depends/llm_datadist/src/ascend_string_stub.cc"
        "${HIXL_CODE_DIR}/tests/depends/llm_datadist/src/data_cache_engine_test_helper.cc"
)

add_executable(ubmem_test
        ${UBMEM_TEST_FILES}
        ${UBMEM_STUB_SRC_FILES}
        ${ADXL_SRC_LIST}
        ${HIXL_SRC_LIST}
)

target_compile_options(ubmem_test PRIVATE
        ${DT_COMMON_COMPILE_OPTION}
        -Wno-deprecated-declarations
        -Wall -Wfloat-equal
)

target_compile_definitions(ubmem_test PRIVATE
        HIXL_TEST_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
        HIXL_ENABLE_TEST_HOOKS
        HIXL_SKIP_DATA_CACHE_ENGINE_TEST_CONTEXT
        HCCN_TEST_CONF_PATH="${HIXL_CODE_DIR}/tests/depends/llm_datadist/src/hccn_test.conf"
)

target_include_directories(ubmem_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${HIXL_CODE_DIR}/include
        ${HIXL_CODE_DIR}/src/hixl
        ${HIXL_CODE_DIR}/src/hixl/common
        ${HIXL_CODE_DIR}/src/hixl/cs
        ${HIXL_CODE_DIR}/src/hixl/engine
        ${HIXL_CODE_DIR}/src/hixl/engine/endpoint_generator
        ${HIXL_CODE_DIR}/src/hixl/profiling
        ${HIXL_CODE_DIR}/src/hixl/proxy
        ${HIXL_CODE_DIR}/src/ops
        ${HIXL_CODE_DIR}/src/ops/hixl_kernel
        ${HIXL_CODE_DIR}/src/ops/hixl_kernel/extern_include
        ${HIXL_CODE_DIR}/src/llm_datadist
        ${HIXL_CODE_DIR}/tests
        ${HIXL_CODE_DIR}/tests/depends/ascendcl/src
        ${HIXL_CODE_DIR}/tests/depends/hccl/src
        ${HIXL_CODE_DIR}/tests/depends/aicpu/src
        ${HIXL_CODE_DIR}/tests/depends/slog/src
)

set_target_properties(ubmem_test PROPERTIES CXX_STANDARD 17)

target_link_libraries(ubmem_test
        ${COMMON_LINK_OPTION}
        c_sec
        mmpa_headers
        ascend_hal_headers
        hccl_headers
        runtime_headers
        metadef_headers
        $<$<PLATFORM_ID:Linux>:$<BUILD_INTERFACE:rdma_core_headers>>
        intf_pub
        json
        ssl_static
        crypto_static
        slog_stub
        mmpa_stub
        msprof_stub
        ascendcl_stub
        runtime_stub
        msprof_headers
        dcmi_stub
        drvdsmi_host_stub
        hixl_ascend_hal_stub
        sys_api_wrap
        -ldl
        GTest::gtest GTest::gtest_main
        GTest::gmock
        stdc++fs
        -ldl
        -Wl,--no-as-needed
        hccl_stub
        error_manager_stub
        aicpu_stub
        -Wl,--as-needed)

# Let dlopen("libdcmi.so") / dlopen("libdrvdsmi_host.so") / dlopen("libascend_hal.so") find the stubs, and
# intercept realpath/access so the device-kernel json path resolves in unit tests (the file is not
# installed in the test environment). Same setup the hixl suite uses.
set_target_properties(ubmem_test PROPERTIES
        BUILD_RPATH "${CMAKE_BINARY_DIR}/tests/depends/dcmi;${CMAKE_BINARY_DIR}/tests/depends/dsmi;${CMAKE_BINARY_DIR}/tests/depends/ascend_hal;${CMAKE_BINARY_DIR}/tests/depends/ibverbs"
        LINK_FLAGS "-Wl,--disable-new-dtags -Wl,--wrap=dlopen -Wl,--wrap=dlsym -Wl,--wrap=dlclose -Wl,--wrap=dlerror -Wl,--wrap=access -Wl,--wrap=realpath"
)

include(CTest)
enable_testing()
