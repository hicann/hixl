/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "rank_table_generator_v1.h"
#include <map>
#include <set>
#include "nlohmann/json.hpp"
#include "acl/acl.h"
#include "llm_datadist/llm_datadist.h"

namespace llm {
namespace {
constexpr const char kConfigVersionV1[] = "1.0";
}

namespace rank_table_v1 {
static void from_json(const nlohmann::json &j, DeviceInfo &d) {
  j.at("device_id").get_to(d.device_id);
  if (j.contains("device_ip")) {
    j.at("device_ip").get_to(d.device_ip);
  }
}

static void to_json(nlohmann::json &j, const DeviceInfo &d) {
  j = nlohmann::json{};
  j["device_id"] = d.device_id;
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

static void from_json(const nlohmann::json &j, RankTableInfo &r) {
  j.at("version").get_to(r.version);
  j.at("server_list").get_to(r.server_list);
}

static void to_json(nlohmann::json &j, const RankTableInfo &r) {
  j = nlohmann::json{};
  j["version"] = r.version;
  j["server_count"] = std::to_string(r.server_list.size());
  j["server_list"] = r.server_list;
  j["status"] = r.status;
}
}

rank_table_v1::RankTableInfo RankTableGeneratorV1::LoadFromJsonStr(const std::string &rank_table) {
  auto j = nlohmann::json::parse(rank_table);
  return j.get<rank_table_v1::RankTableInfo>();
}

ge::Status RankTableGeneratorV1::MergeRankTable(int32_t local_device_id,
                                                const rank_table_v1::RankTableInfo &local_rank_table,
                                                const rank_table_v1::RankTableInfo &peer_rank_table,
                                                rank_table_v1::RankTableInfo &merged_rank_table) {
  std::map<std::string, std::set<rank_table_v1::DeviceInfo>> merged_info;
  for (const auto &server : local_rank_table.server_list) {
    LLM_CHK_BOOL_RET_STATUS(server.device_list.size() == 1U, ge::LLM_PARAM_INVALID,
                           "Please check local option:%s, it only supports one device that is used.",
                           llm_datadist::OPTION_LOCAL_COMM_RES);
    int32_t phy_device_id = 0U;
    LLM_CHK_RT_RET(aclrtGetPhyDevIdByLogicDevId(local_device_id, &phy_device_id));
    LLM_CHK_BOOL_RET_STATUS(std::to_string(phy_device_id) == server.device_list[0].device_id,
                           ge::LLM_PARAM_INVALID,
                           "Please check local option:%s, device_id:%s should be %u, logic device id:%d.",
                           llm_datadist::OPTION_LOCAL_COMM_RES, server.device_list[0].device_id.c_str(),
                           phy_device_id, local_device_id);
    merged_info[server.server_id].emplace(server.device_list[0]);
  }
  for (const auto &server : peer_rank_table.server_list) {
    LLM_CHK_BOOL_RET_STATUS(server.device_list.size() == 1U, ge::LLM_PARAM_INVALID,
                           "Please check peer option:%s, it only supports one device that is used.",
                           llm_datadist::OPTION_LOCAL_COMM_RES);
    auto peer_device = server.device_list[0];
    peer_device.is_local = false;
    merged_info[server.server_id].emplace(peer_device);
  }

  merged_rank_table.version = kConfigVersionV1;
  merged_rank_table.status = local_rank_table.status;
  int32_t rank_id = 0;
  for (const auto &it : merged_info) {
    const auto &server_id = it.first;
    const auto &device_set = it.second;
    rank_table_v1::ServerInfo server_info{};
    server_info.server_id = server_id;
    for (const auto& device : device_set) {
      auto device_info = device;
      device_info.rank_id = rank_id;
      rank_id++;
      server_info.device_list.emplace_back(device_info);
    }
    merged_rank_table.server_list.emplace_back(server_info);
  }
  return ge::SUCCESS;
}

ge::Status RankTableGeneratorV1::Generate(int32_t local_device_id, std::string &rank_table) {
  rank_table_v1::RankTableInfo local_rank_table{};
  rank_table_v1::RankTableInfo peer_rank_table{};
  LLMLOGI("Rank table generate begin, local comm res:%s, peer comm res:%s",
         local_comm_res_.c_str(), peer_comm_res_.c_str());
  try {
    local_rank_table = RankTableGeneratorV1::LoadFromJsonStr(local_comm_res_);
    peer_rank_table = RankTableGeneratorV1::LoadFromJsonStr(peer_comm_res_);
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

int32_t RankTableGeneratorV1::GetLocalRankId() {
  for (const auto &server : merged_rank_table_.server_list) {
    for (const auto &device : server.device_list) {
      if (device.is_local) {
        return device.rank_id;
      }
    }
  }
  return -1;
}

int32_t RankTableGeneratorV1::GetPeerRankId() {
  for (const auto &server : merged_rank_table_.server_list) {
    for (const auto &device : server.device_list) {
      if (!device.is_local) {
        return device.rank_id;
      }
    }
  }
  return -1;
}

ge::Status RankTableGeneratorV1::GenerateLocalCommRes(const std::string &server_id,
                                                      int32_t device_id,
                                                      std::string &local_comm_res) {
  rank_table_v1::RankTableInfo local_rank_table{};
  local_rank_table.version = kConfigVersionV1;
  rank_table_v1::ServerInfo server_info{};
  server_info.server_id = server_id;
  rank_table_v1::DeviceInfo device_info{};
  int32_t phy_device_id = 0U;
  LLM_CHK_RT_RET(aclrtGetPhyDevIdByLogicDevId(device_id, &phy_device_id));
  device_info.device_id = std::to_string(phy_device_id);
  LLM_CHK_STATUS_RET(LocalCommResGenerator::GetDeviceIp(phy_device_id, device_info.device_ip),
                    "Failed to get device_ip, phy_device_id:%u", phy_device_id);
  server_info.device_list.emplace_back(device_info);
  local_rank_table.server_list.emplace_back(server_info);
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
