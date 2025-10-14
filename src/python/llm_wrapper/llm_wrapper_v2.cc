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

#include <map>
#include <string>
#include <vector>
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"
#include "runtime/rt_error_codes.h"
#include "runtime/rt.h"
#include "graph/types.h"
#include "llm_datadist/llm_error_codes.h"
#include "llm_datadist_v2_wrapper.h"
#include "common/mem_utils.h"
#include "common/llm_utils.h"

#undef PYBIND11_CHECK_PYTHON_VERSION
#define PYBIND11_CHECK_PYTHON_VERSION

namespace llm {
namespace {
namespace py = pybind11;
void BindStatusCodes(py::module &m) {
  m.attr("kSuccess") = py::int_(ge::SUCCESS);
  m.attr("kFailed") = py::int_(ge::FAILED);
  m.attr("kParamInvalid") = py::int_(ge::LLM_PARAM_INVALID);
  m.attr("kLLMWaitProcessTimeOut") = py::int_(ge::LLM_WAIT_PROC_TIMEOUT);
  m.attr("kLLMKVCacheNotExist") = py::int_(ge::LLM_KV_CACHE_NOT_EXIST);
  m.attr("kLLMRepeatRequest") = py::int_(ge::LLM_REPEAT_REQUEST);
  m.attr("kLLMRequestAlreadyCompleted") = py::int_(ge::LLM_REQUEST_ALREADY_COMPLETED);
  m.attr("kLLMEngineFinalized") = py::int_(ge::LLM_ENGINE_FINALIZED);
  m.attr("kLLMNotYetLink") = py::int_(ge::LLM_NOT_YET_LINK);
  m.attr("kAlreadyLink") = py::int_(ge::LLM_ALREADY_LINK);
  m.attr("kLinKFailed") = py::int_(ge::LLM_LINK_FAILED);
  m.attr("kUnlinkFailed") = py::int_(ge::LLM_UNLINK_FAILED);
  m.attr("kNotifyPromptUnlinkFailed") = py::int_(ge::LLM_NOTIFY_PROMPT_UNLINK_FAILED);
  m.attr("kLLMClusterNumExceedLimit") = py::int_(ge::LLM_CLUSTER_NUM_EXCEED_LIMIT);
  m.attr("kProcessingLink") = py::int_(ge::LLM_PROCESSING_LINK);
  m.attr("kDeviceOutOfMemory") = py::int_(ge::LLM_DEVICE_OUT_OF_MEMORY);
  m.attr("kPrefixAlreadyExist") = py::int_(ge::LLM_PREFIX_ALREADY_EXIST);
  m.attr("kPrefixNotExist") = py::int_(ge::LLM_PREFIX_NOT_EXIST);
  m.attr("kLLMSeqLenOverLimit") = py::int_(ge::LLM_SEQ_LEN_OVER_LIMIT);
  m.attr("kLLMNoFreeBlock") = py::int_(ge::LLM_NO_FREE_BLOCK);
  m.attr("kLLMBlocksOutOfMemory") = py::int_(ge::LLM_BLOCKS_OUT_OF_MEMORY);
  m.attr("kLLMExistLink") = py::int_(ge::LLM_EXIST_LINK);
  m.attr("kLLMFeatureNotEnabled") = py::int_(ge::LLM_FEATURE_NOT_ENABLED);
  m.attr("kLLMTimeout") = py::int_(ge::LLM_TIMEOUT);
  m.attr("kLLMLinkBusy") = py::int_(ge::LLM_LINK_BUSY);
  m.attr("kLLMOutOfMemory") = py::int_(ge::LLM_OUT_OF_MEMORY);
  m.attr("kLLMDeviceMemError") = py::int_(ACL_ERROR_RT_DEVICE_MEM_ERROR);
  m.attr("kLLMSuspectRemoteError") = py::int_(ACL_ERROR_RT_SUSPECT_REMOTE_ERROR);
}

std::vector<std::pair<int64_t, int64_t>> PyDictToVector(const py::dict &py_dict) {
  std::vector<std::pair<int64_t, int64_t>> result;

  for (const auto &item : py_dict) {
    int64_t key = item.first.cast<int64_t>();
    int64_t value = item.second.cast<int64_t>();
    result.emplace_back(key, value);
  }
  return result;
}

int64_t CalcTensorSize(const std::vector<int64_t> &shape, int32_t data_type) {
  int64_t tensor_size = -1;
  (void) LLMUtils::CalcTensorMemSize(shape,
                                     static_cast<ge::DataType>(data_type),
                                     tensor_size);
  return tensor_size;
}

void BuildDataDistV2Funcs(py::module &m) {
  (void)m.def("calc_tensor_size", &CalcTensorSize);
  (void)m.def("dict_to_vector", &PyDictToVector, py::call_guard<py::gil_scoped_release>());
  (void)m.def("initialize_v2", &LLMDataDistV2Wrapper::Init, py::call_guard<py::gil_scoped_release>());
  (void)m.def("finalize_v2", &LLMDataDistV2Wrapper::Finalize, py::call_guard<py::gil_scoped_release>());
  (void)m.def("link", &LLMDataDistV2Wrapper::Link, py::call_guard<py::gil_scoped_release>());
  (void)m.def("unlink", &LLMDataDistV2Wrapper::Unlink, py::call_guard<py::gil_scoped_release>());
  (void)m.def("query_register_mem_status", &LLMDataDistV2Wrapper::QueryRegisterMemStatus,
              py::call_guard<py::gil_scoped_release>());
  (void)m.def("register_cache", &LLMDataDistV2Wrapper::RegisterCache, py::call_guard<py::gil_scoped_release>());
  (void)m.def("unregister_cache", &LLMDataDistV2Wrapper::UnregisterCache, py::call_guard<py::gil_scoped_release>());
  (void)m.def("allocate_cache_v2", &LLMDataDistV2Wrapper::AllocateCache, py::call_guard<py::gil_scoped_release>());
  (void)m.def("deallocate_cache_v2", &LLMDataDistV2Wrapper::DeallocateCache, py::call_guard<py::gil_scoped_release>());
  (void)m.def("remove_cache_key_v2", &LLMDataDistV2Wrapper::RemoveCacheKey, py::call_guard<py::gil_scoped_release>());
  (void)m.def("remap_registered_memory", &LLMDataDistV2Wrapper::RemapRegisteredMemory,
              py::call_guard<py::gil_scoped_release>());
  (void)m.def("pull_cache_v2", &LLMDataDistV2Wrapper::PullCache, py::call_guard<py::gil_scoped_release>());
  (void)m.def("copy_cache_v2", &LLMDataDistV2Wrapper::CopyCache, py::call_guard<py::gil_scoped_release>());
  (void)m.def("swap_blocks_v2", &LLMDataDistV2Wrapper::SwapBlocks, py::call_guard<py::gil_scoped_release>());
  (void)m.def("check_capacity_v2", &LLMDataDistV2Wrapper::CheckCapacity, py::call_guard<py::gil_scoped_release>());
  (void) m.def("transfer_cache_v2", &LLMDataDistV2Wrapper::TransferCache, py::call_guard<py::gil_scoped_release>());
  (void) m.def("link_clusters_v2", &LLMDataDistV2Wrapper::LinkClusters, py::call_guard<py::gil_scoped_release>());
  (void) m.def("unlink_clusters_v2", &LLMDataDistV2Wrapper::UnlinkClusters, py::call_guard<py::gil_scoped_release>());
  (void) m.def("switch_role_v2", &LLMDataDistV2Wrapper::SwitchRole, py::call_guard<py::gil_scoped_release>());
}
}  // namespace

PYBIND11_MODULE(llm_datadist_wrapper, m) {
  BindStatusCodes(m);
  BuildDataDistV2Funcs(m);
}
}  // namespace llm
