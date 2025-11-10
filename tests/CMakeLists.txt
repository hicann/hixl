# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

set(DT_COMMON_COMPILE_OPTION ${DT_COMMON_COMPILE_OPTION} -fno-access-control)

add_subdirectory(depends/slog)
add_subdirectory(depends/mmpa)
add_subdirectory(depends/runtime)
add_subdirectory(depends/error_manager)
add_subdirectory(depends/llm_datadist)
add_subdirectory(depends/python)
add_subdirectory(depends/ascendcl)
add_subdirectory(cpp)
