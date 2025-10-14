/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "rank_table_generator_v2.h"
#include <map>
#include <set>
#include "nlohmann/json.hpp"
#include "runtime/rt.h"
#include "llm_datadist/llm_datadist.h"

namespace llm {
namespace {
constexpr const char kConfigVersionV2[] = "1.2";
constexpr int32_t kInfoTypeSuperDeviceId = 26;
constexpr int32_t kInfoTypeSuperPodId = 29;
}

namespace rank_table_v2 {
static void from_json(const nlohmann::json &j, DeviceInfo &d) {
  j.at("device_id").get_to(d.device_id);
  j.at("super_device_id").get_to(d.super_device_id);
  if (j.contains("device_ip")) {
    j.at("device_ip").get_to(d.device_ip);
  }
}

static void to_json(nlohmann::json &j, const DeviceInfo &d) {
  j = nlohmann::json{};
  j["device_id"] = d.device_id;
  j["super_device_id"] = d.super_device_id;
  if (!d.device_ip.empty()) {
    j["device_ip"] = d.device_ip;
  }
  j["rank_id"] = std::to_string(d.rank_id);
}

static void from_json(const nlohmann::json &j, ServerInfo &s) {
  j.at("server_id").get_to(s.server_id);
  j.at("device").get_to(s.device_list);
}

static void to_json(nlohmann::json &j, const ServerInfo &s) {
  j = nlohmann::json{};
  j["server_id"] = s.server_id;
  j["device"] = s.device_list;
}

static void from_json(const nlohmann::json &j, ServerIdInfo &s) {
  j.at("server_id").get_to(s.server_id);
}

static void to_json(nlohmann::json &j, const ServerIdInfo &s) {
  j = nlohmann::json{};
  j["server_id"] = s.server_id;
}

static void from_json(const nlohmann::json &j, SuperPodInfo &s) {
  j.at("super_pod_id").get_to(s.super_pod_id);
  j.at("server_list").get_to(s.server_list);
}

static void to_json(nlohmann::json &j, const SuperPodInfo &s) {
  j = nlohmann::json{};
  j["super_pod_id"] = s.super_pod_id;
  j["server_list"] = s.server_list;
}

static void from_json(const nlohmann::json &j, RankTableInfo &r) {
  j.at("version").get_to(r.version);
  j.at("server_list").get_to(r.server_list);
  if (j.contains("super_pod_list")) {
    j.at("super_pod_list").get_to(r.super_pod_list);
  }
}

static void to_json(nlohmann::json &j, const RankTableInfo &r) {
  j = nlohmann::json{};
  j["version"] = r.version;
  j["server_count"] = std::to_string(r.server_list.size());
  j["server_list"] = r.server_list;
  if (!r.super_pod_list.empty()) {
    j["super_pod_list"] = r.super_pod_list;
  }
  j["status"] = r.status;
}
}

rank_table_v2::RankTableInfo RankTableGeneratorV2::LoadFromJsonStr(const std::string &rank_table) {
  auto j = nlohmann::json::parse(rank_table);
  return j.get<rank_table_v2::RankTableInfo>();
}

ge::Status RankTableGeneratorV2::MergeRankTable(int32_t local_device_id,
                                                const rank_table_v2::RankTableInfo &local_rank_table,
                                                const rank_table_v2::RankTableInfo &peer_rank_table,
                                                rank_table_v2::RankTableInfo &merged_rank_table) {
  std::map<std::string, std::set<rank_table_v2::DeviceInfo>> merged_server_info;
  for (const auto &server : local_rank_table.server_list) {
    LLM_CHK_BOOL_RET_STATUS(server.device_list.size() == 1U, ge::LLM_PARAM_INVALID,
                           "Please check local option:%s, it only supports one device that is used.",
                           llm_datadist::OPTION_LOCAL_COMM_RES);
    uint32_t phy_device_id = 0U;
    LLM_CHK_RT_RET(rtGetDevicePhyIdByIndex(static_cast<uint32_t>(local_device_id), &phy_device_id));
    LLM_CHK_BOOL_RET_STATUS(std::to_string(phy_device_id) == server.device_list[0].device_id,
                           ge::LLM_PARAM_INVALID,
                           "Please check local option:%s, device_id:%s should be %u, logic device id:%d.",
                           llm_datadist::OPTION_LOCAL_COMM_RES, server.device_list[0].device_id.c_str(),
                           phy_device_id, local_device_id);
    merged_server_info[server.server_id].emplace(server.device_list[0]);
  }
  for (const auto &server : peer_rank_table.server_list) {
    LLM_CHK_BOOL_RET_STATUS(server.device_list.size() == 1U, ge::LLM_PARAM_INVALID,
                           "Please check peer option:%s, it only supports one device that is used.",
                           llm_datadist::OPTION_LOCAL_COMM_RES);
    auto peer_device = server.device_list[0];
    peer_device.is_local = false;
    merged_server_info[server.server_id].emplace(peer_device);
  }

  merged_rank_table.version = kConfigVersionV2;
  merged_rank_table.status = local_rank_table.status;
  int32_t rank_id = 0;
  for (const auto &it : merged_server_info) {
    const auto &server_id = it.first;
    const auto &device_set = it.second;
    rank_table_v2::ServerInfo server_info{};
    server_info.server_id = server_id;
    for (const auto& device : device_set) {
      auto device_info = device;
      device_info.rank_id = rank_id;
      rank_id++;
      server_info.device_list.emplace_back(device_info);
    }
    merged_rank_table.server_list.emplace_back(server_info);
  }

  std::map<std::string, std::set<rank_table_v2::ServerIdInfo>> merged_pod_info;
  for (const auto &pod : local_rank_table.super_pod_list) {
    for (const auto &server_id : pod.server_list) {
      merged_pod_info[pod.super_pod_id].emplace(server_id);
    }
  }
  for (const auto &pod : peer_rank_table.super_pod_list) {
    for (const auto &server_id : pod.server_list) {
      merged_pod_info[pod.super_pod_id].emplace(server_id);
    }
  }
  for (const auto &it : merged_pod_info) {
    const auto &pod_id = it.first;
    const auto &server_set = it.second;
    rank_table_v2::SuperPodInfo pod_info{};
    pod_info.super_pod_id = pod_id;
    for (const auto &server_id : server_set) {
      pod_info.server_list.emplace_back(server_id);
    }
    merged_rank_table.super_pod_list.emplace_back(pod_info);
  }
  return ge::SUCCESS;
}

ge::Status RankTableGeneratorV2::Generate(int32_t local_device_id, std::string &rank_table) {
  rank_table_v2::RankTableInfo local_rank_table{};
  rank_table_v2::RankTableInfo peer_rank_table{};
  LLMLOGI("Rank table generate begin, local comm res:%s, peer comm res:%s",
         local_comm_res_.c_str(), peer_comm_res_.c_str());
  try {
    local_rank_table = RankTableGeneratorV2::LoadFromJsonStr(local_comm_res_);
    peer_rank_table = RankTableGeneratorV2::LoadFromJsonStr(peer_comm_res_);
  } catch (const nlohmann::json::exception &e) {
    LLMLOGE(ge::LLM_PARAM_INVALID, "Failed to load rank table, exception:%s", e.what());
    return ge::LLM_PARAM_INVALID;
  }
  LLM_CHK_STATUS_RET(MergeRankTable(local_device_id, local_rank_table, peer_rank_table, merged_rank_table_),
                    "Failed to merge rank table");
  try {
    nlohmann::json j = merged_rank_table_;
    rank_table = j.dump();
  } catch (const nlohmann::json::exception &e) {
    LLMLOGE(ge::LLM_PARAM_INVALID, "Failed to dump rank table, exception:%s", e.what());
    return ge::LLM_PARAM_INVALID;
  }
  LLMLOGI("Rank table generated successfully, rank table:%s", rank_table.c_str());
  return ge::SUCCESS;
}

int32_t RankTableGeneratorV2::GetLocalRankId() {
  for (const auto &server : merged_rank_table_.server_list) {
    for (const auto &device : server.device_list) {
      if (device.is_local) {
        return device.rank_id;
      }
    }
  }
  return -1;
}

int32_t RankTableGeneratorV2::GetPeerRankId() {
  for (const auto &server : merged_rank_table_.server_list) {
    for (const auto &device : server.device_list) {
      if (!device.is_local) {
        return device.rank_id;
      }
    }
  }
  return -1;
}

ge::Status RankTableGeneratorV2::GenerateLocalCommRes(const std::string &server_id,
                                                      int32_t device_id,
                                                      std::string &local_comm_res) {
  rank_table_v2::RankTableInfo local_rank_table{};
  local_rank_table.version = kConfigVersionV2;
  rank_table_v2::ServerInfo server_info{};
  server_info.server_id = server_id;
  rank_table_v2::DeviceInfo device_info{};
  uint32_t phy_device_id = 0U;
  LLM_CHK_RT_RET(rtGetDevicePhyIdByIndex(static_cast<uint32_t>(device_id), &phy_device_id));
  device_info.device_id = std::to_string(phy_device_id);
  LLM_CHK_STATUS_RET(LocalCommResGenerator::GetDeviceIp(phy_device_id, device_info.device_ip),
                    "Failed to get device_ip, phy_device_id:%u", phy_device_id);
  int64_t super_device_id = 0U;
  LLM_CHK_RT_RET(rtGetDeviceInfo(static_cast<uint32_t>(device_id),
                                RT_MODULE_TYPE_SYSTEM,
                                kInfoTypeSuperDeviceId,
                                &super_device_id));
  device_info.super_device_id = std::to_string(super_device_id);
  rank_table_v2::SuperPodInfo pod_info{};
  int64_t super_pod_id = 0U;
  LLM_CHK_RT_RET(rtGetDeviceInfo(static_cast<uint32_t>(device_id),
                                RT_MODULE_TYPE_SYSTEM,
                                kInfoTypeSuperPodId,
                                &super_pod_id));
  pod_info.super_pod_id = std::to_string(super_pod_id);
  rank_table_v2::ServerIdInfo server_id_info{};
  server_id_info.server_id = server_id;
  pod_info.server_list.emplace_back(server_id_info);

  server_info.device_list.emplace_back(device_info);
  local_rank_table.server_list.emplace_back(server_info);
  local_rank_table.super_pod_list.emplace_back(pod_info);
  try {
    nlohmann::json j = local_rank_table;
    local_comm_res = j.dump();
  } catch (...) {
    return ge::FAILED;
  }
  LLMLOGI("local comm res generated successfully, local comm res:%s", local_comm_res.c_str());
  return ge::SUCCESS;
}
}  // namespace llm
