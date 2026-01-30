# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_library(mmpa_stub SHARED src/mmpa_stub.cc)
target_compile_options(mmpa_stub PRIVATE -g)
target_include_directories(mmpa_stub PRIVATE
    ${HIXL_CODE_DIR}/inc
    ${HIXL_CODE_DIR}/include
    ${HIXL_CODE_DIR}/inc/framework
)
target_link_libraries(mmpa_stub
    PRIVATE
        metadef_headers
        acl_rt_headers
        msprof_headers
        slog_headers
        c_sec
        -ldl
    PUBLIC
        mmpa_headers
)

stub_module(static_mmpa mmpa_stub)
