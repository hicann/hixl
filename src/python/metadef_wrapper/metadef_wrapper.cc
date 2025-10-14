/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "Python.h"
#ifdef ASCEND_CI_LIMITED_PY37
#undef PyCFunction_NewEx
#endif

#include <vector>
#include "pybind11/pybind11.h"
#include "graph/types.h"

#undef PYBIND11_CHECK_PYTHON_VERSION
#define PYBIND11_CHECK_PYTHON_VERSION

namespace llm {
namespace {
namespace py = pybind11;
}  // namespace


PYBIND11_MODULE(metadef_wrapper, m) {
  m.attr("DT_FLOAT") = py::int_(static_cast<int32_t>(ge::DataType::DT_FLOAT));
  m.attr("DT_FLOAT16") = py::int_(static_cast<int32_t>(ge::DataType::DT_FLOAT16));
  m.attr("DT_BF16") = py::int_(static_cast<int32_t>(ge::DataType::DT_BF16));
  m.attr("DT_INT8") = py::int_(static_cast<int32_t>(ge::DataType::DT_INT8));
  m.attr("DT_INT16") = py::int_(static_cast<int32_t>(ge::DataType::DT_INT16));
  m.attr("DT_UINT16") = py::int_(static_cast<int32_t>(ge::DataType::DT_UINT16));
  m.attr("DT_UINT8") = py::int_(static_cast<int32_t>(ge::DataType::DT_UINT8));
  m.attr("DT_INT32") = py::int_(static_cast<int32_t>(ge::DataType::DT_INT32));
  m.attr("DT_INT64") = py::int_(static_cast<int32_t>(ge::DataType::DT_INT64));
  m.attr("DT_UINT32") = py::int_(static_cast<int32_t>(ge::DataType::DT_UINT32));
  m.attr("DT_UINT64") = py::int_(static_cast<int32_t>(ge::DataType::DT_UINT64));
  m.attr("DT_BOOL") = py::int_(static_cast<int32_t>(ge::DataType::DT_BOOL));
  m.attr("DT_DOUBLE") = py::int_(static_cast<int32_t>(ge::DataType::DT_DOUBLE));
  m.attr("DT_STRING") = py::int_(static_cast<int32_t>(ge::DataType::DT_STRING));
}
}  // namespace llm
