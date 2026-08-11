# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

# System-interface wrap stub for unit tests. Intercepts dlopen/dlsym/dlclose/
# dlerror/access/realpath in test binaries via the linker --wrap mechanism so
# tests can simulate dynamic-load / file-access failures without touching
# production code. Linked to test targets only; never enters production builds.
add_library(sys_api_wrap STATIC src/sys_api_wrap.cc)
target_include_directories(sys_api_wrap PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# Linked into both test executables and the llm_datadist_base shared lib (Python
# UT), so position-independent code is required.
target_compile_options(sys_api_wrap PRIVATE -fPIC)
target_link_libraries(sys_api_wrap PRIVATE c_sec)
