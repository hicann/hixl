# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

include(${PROJECT_SOURCE_DIR}/cmake/third_party/json.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/third_party/pybind11.cmake)

include(CMakePrintHelpers)
find_package(Python3 COMPONENTS Interpreter Development)
if (Python3_FOUND)
    set(HI_PYTHON_INC ${Python3_INCLUDE_DIRS})
    cmake_print_variables(HI_PYTHON_INC)
endif ()
cmake_print_variables(HI_PYTHON_INC)
cmake_print_variables(pybind11_INCLUDE_DIR)

if (ENABLE_TEST)
    include(${PROJECT_SOURCE_DIR}/cmake/third_party/gtest.cmake)
endif()