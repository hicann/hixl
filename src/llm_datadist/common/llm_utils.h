/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LLM_UTILS_H
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LLM_UTILS_H
#include <unordered_map>
#include <memory>
#include "llm_datadist/llm_error_codes.h"
#include "utils/extern_math_util.h"
#include "nlohmann/json.hpp"
#include "acl/acl.h"
#include "common/llm_inner_types.h"

namespace llm {
constexpr int32_t kDefaultSyncKvWaitTime = 1000;

struct DecoderWaitTimeInfo {
  int32_t sync_kv_wait_time{kDefaultSyncKvWaitTime};
};

enum class ServerType : uint32_t { Prompt = 0U, Decoder };

class LLMUtils {
 public:
  static ge::Status ParserWaitTimeInfo(const std::map<ge::AscendString, ge::AscendString> &options,
                                       DecoderWaitTimeInfo &wait_time_info);

  template<typename T>
  static ge::Status ToNumber(const std::string &num_str, T &value) {
    std::stringstream ss(num_str);
    ss >> value;
    LLM_CHK_BOOL_RET_STATUS(!ss.fail(), ge::LLM_PARAM_INVALID, "Failed to convert [%s] to number", num_str.c_str());
    LLM_CHK_BOOL_RET_STATUS(ss.eof(), ge::LLM_PARAM_INVALID, "Failed to convert [%s] to number", num_str.c_str());
    return ge::SUCCESS;
  }

  static ge::Status ParseDeviceId(const std::map<ge::AscendString, ge::AscendString> &options, int32_t &device_id);

  static ge::Status ParseDeviceId(const std::map<ge::AscendString, ge::AscendString> &options,
                                  std::vector<int32_t> &device_ids, const char *option);

  static ge::Status ParseListenIpInfo(const std::map<ge::AscendString, ge::AscendString> &options,
                                      std::string &ip,
                                      uint32_t &port);

  static ge::Status IpToInt(const std::string &ip, uint32_t &ip_int);

  static ge::Status IntToIp(uint32_t ip_int, std::string &ip_str);

  static ge::Status FindContiguousBlockIndexPair(const std::vector<std::pair<int64_t, int64_t>> &block_mapping,
                                                 std::vector<std::vector<std::pair<int64_t, int64_t>>> &result);

  static ge::Status FindContiguousBlockIndexPair(const std::vector<uint64_t> &src_blocks,
                                                 const std::vector<uint64_t> &dst_blocks,
                                                 std::vector<std::vector<std::pair<int64_t, int64_t>>> &result);
  static ge::Status ParseFlag(const std::string &option_name,
                              const std::map<ge::AscendString, ge::AscendString> &options,
                              bool &enabled);

  static bool CheckMultiplyOverflowInt64(int64_t a, int64_t b);

  static int32_t CeilDiv(int32_t a, int32_t b);

  static ge::Status CalcElementCntByDims(const std::vector<int64_t> &dims, int64_t &element_cnt);

  static bool GetDataTypeLength(const ge::DataType data_type, uint32_t &length);

  static ge::Status GetSizeInBytes(int64_t element_count, ge::DataType data_type, int64_t &mem_size);

  static ge::Status CalcTensorMemSize(const std::vector<int64_t> &dims,
                                      const ge::DataType data_type,
                                      int64_t &mem_size);


  static bool IsTimeout(const std::chrono::high_resolution_clock::time_point& start_time, int32_t timeout_ms);

  template <typename T>
  inline static void AssignRequired(T &variable, const std::string &key, const nlohmann::json &json) {
    try {
      json.at(key).get_to(variable);
    } catch (const nlohmann::json::exception &e) {
      LLMLOGE(ge::LLM_PARAM_INVALID, "Failed to read %s, err msg:%s.", key.c_str(), e.what());
      throw e;
    }
  }
 private:
  static ge::Status ParseListenIpInfo(const std::string &option,
                                      std::string &ip,
                                      uint32_t &port);
};

template <typename T>
std::string ToString(const std::vector<T> &v) {
  bool first = true;
  std::stringstream ss;
  ss << "[";
  for (const T &x : v) {
    if (first) {
      first = false;
      ss << x;
    } else {
      ss << ", " << x;
    }
  }
  ss << "]";
  return ss.str();
}

class TemporaryRtContext {
 public:
  explicit TemporaryRtContext(aclrtContext context);
  ~TemporaryRtContext();

 private:
  aclrtContext prev_context_ = nullptr;
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LLM_UTILS_H
