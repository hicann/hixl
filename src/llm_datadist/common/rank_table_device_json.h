/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_RANK_TABLE_DEVICE_JSON_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_RANK_TABLE_DEVICE_JSON_H_

#include <string>
#include <vector>

#include "common/llm_checker.h"
#include "common/llm_log.h"
#include "nlohmann/json.hpp"

namespace llm {
namespace rank_table_json {
constexpr size_t kMaxCommResSizeInBytes = 64U * 1024U;

inline bool IsCommResJsonSizeValid(const std::string &comm_res) {
  return comm_res.size() <= kMaxCommResSizeInBytes;
}

inline nlohmann::json ParseCommResJson(const std::string &comm_res) {
  return nlohmann::json::parse(comm_res);
}

inline void LoadOptionalDeviceNetworkFields(const nlohmann::json &j, std::string &device_ip, std::string &device_port) {
  if (j.contains("device_ip")) {
    j.at("device_ip").get_to(device_ip);
  }
  if (j.contains("device_port")) {
    j.at("device_port").get_to(device_port);
  }
}

inline void StoreOptionalDeviceNetworkFields(nlohmann::json &j, const std::string &device_ip,
                                             const std::string &device_port) {
  if (!device_ip.empty()) {
    j["device_ip"] = device_ip;
  }
  if (!device_port.empty()) {
    j["device_port"] = device_port;
  }
}

inline bool LessDeviceNetworkEndpoint(const std::string &device_ip, const std::string &device_port,
                                      const std::string &other_ip, const std::string &other_port) {
  if (device_ip != other_ip) {
    return device_ip < other_ip;
  }
  return device_port < other_port;
}

template <typename DeviceInfo>
inline void FromJsonServerInfo(const nlohmann::json &j, std::string &server_id, std::vector<DeviceInfo> &device_list) {
  j.at("server_id").get_to(server_id);
  j.at("device").get_to(device_list);
}

template <typename DeviceInfo>
inline void ToJsonServerInfo(nlohmann::json &j, const std::string &server_id,
                             const std::vector<DeviceInfo> &device_list) {
  j = nlohmann::json{};
  j["server_id"] = server_id;
  j["device"] = device_list;
}

template <typename RankTableInfo, typename LoadFn>
inline ge::Status LoadRankTablesFromCommRes(const std::string &local_comm_res, const std::string &peer_comm_res,
                                            LoadFn load_fn, RankTableInfo &local_rank_table,
                                            RankTableInfo &peer_rank_table) {
  if (!IsCommResJsonSizeValid(local_comm_res) || !IsCommResJsonSizeValid(peer_comm_res)) {
    LLMLOGE(ge::LLM_PARAM_INVALID, "comm_res size exceeds limit");
    return ge::LLM_PARAM_INVALID;
  }
  try {
    local_rank_table = load_fn(local_comm_res);
    peer_rank_table = load_fn(peer_comm_res);
  } catch (const nlohmann::json::exception &e) {
    LLMLOGE(ge::LLM_PARAM_INVALID, "Failed to load rank table, exception:%s", e.what());
    return ge::LLM_PARAM_INVALID;
  }
  return ge::SUCCESS;
}

template <typename RankTableInfo>
inline ge::Status DumpRankTableJson(const RankTableInfo &rank_table_info, std::string &rank_table) {
  try {
    nlohmann::json j = rank_table_info;
    rank_table = j.dump();
  } catch (const nlohmann::json::exception &e) {
    LLMLOGE(ge::LLM_PARAM_INVALID, "Failed to dump rank table, exception:%s", e.what());
    return ge::LLM_PARAM_INVALID;
  }
  return ge::SUCCESS;
}
}  // namespace rank_table_json
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_RANK_TABLE_DEVICE_JSON_H_
