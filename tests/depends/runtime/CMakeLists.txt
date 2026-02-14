# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_library(runtime_stub SHARED src/runtime_stub.cc)
target_compile_options(runtime_stub PRIVATE -g)
target_compile_definitions(runtime_stub PRIVATE
    $<$<STREQUAL:${ENABLE_OPEN_SRC},True>:ONLY_COMPILE_OPEN_SRC>
)
target_include_directories(runtime_stub PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_link_libraries(runtime_stub
    PRIVATE
        intf_pub
        c_sec
        msprof_headers
        mmpa_headers
    PUBLIC
        runtime_headers
        mmpa_stub
)

stub_module(runtime runtime_stub)
