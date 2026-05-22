# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_executable(hixl_kv_bench
    hixl_kv_bench.cpp
    kv_transfer_executor.cpp
    kvstore/kvstore.cpp
    kvstore/segment_manager.cpp
    kvstore/model_config.cpp
    kvstore/kv_slice_layout.cpp
)
target_compile_features(hixl_kv_bench PRIVATE cxx_std_17)
target_include_directories(hixl_kv_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/kvstore
    ${HIXL_INC_DIR}
    ${CANN_INSTALL_PATH}/include
    ${HIXL_CODE_DIR}/third_party/json/include
    ${HIXL_CODE_DIR}/src/llm_datadist
)
target_compile_options(hixl_kv_bench PRIVATE
    -ftrapv
    -O2
    -fno-common
    -Wfloat-equal
    -Wall
    -Werror
    -Wextra
)
target_link_libraries(hixl_kv_bench PRIVATE
    cann_hixl
    adxl_static
    acl_rt_headers
    acl_rt
    -lpthread
    -lrt
)
target_link_ascend_hal(hixl_kv_bench)

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/config/models.json"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/config")
