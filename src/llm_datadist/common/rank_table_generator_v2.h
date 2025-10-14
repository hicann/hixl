/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_RANK_TABLE_GENERATOR_V2_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_RANK_TABLE_GENERATOR_V2_H_

#include <vector>
#include "rank_table_generator.h"

namespace llm {
namespace rank_table_v2 {
struct DeviceInfo {
  std::string device_id;
  std::string super_device_id;
  std::string device_ip;
  int32_t rank_id = -1;
  bool is_local = true;

  bool operator < (const DeviceInfo &other) const {
    if (device_id != other.device_id) {
      return device_id < other.device_id;
    } else if (super_device_id != other.super_device_id) {
      return super_device_id < other.super_device_id;
    } else {
      return device_ip < other.device_ip;
    }
  }
};

struct ServerInfo {
  std::string server_id;
  std::vector<DeviceInfo> device_list;
};

struct ServerIdInfo {
  std::string server_id;

  bool operator < (const ServerIdInfo &other) const {
    return server_id < other.server_id;
  }
};

struct SuperPodInfo {
  std::string super_pod_id;
  std::vector<ServerIdInfo> server_list;
};

struct RankTableInfo {
  std::string version;
  std::vector<ServerInfo> server_list;
  std::vector<SuperPodInfo> super_pod_list;
  std::string status = "completed";
};
}

class RankTableGeneratorV2 : public RankTableGenerator {
 public:
  RankTableGeneratorV2(const std::string &local_comm_res, const std::string &peer_comm_res)
      : local_comm_res_(local_comm_res), peer_comm_res_(peer_comm_res), merged_rank_table_{} {};
  ~RankTableGeneratorV2() override = default;
  ge::Status Generate(int32_t local_device_id, std::string &rank_table) override;
  int32_t GetLocalRankId() override;
  int32_t GetPeerRankId() override;
  static ge::Status GenerateLocalCommRes(const std::string &server_id,
                                         int32_t device_id,
                                         std::string &local_comm_res);

 private:
  static rank_table_v2::RankTableInfo LoadFromJsonStr(const std::string &rank_table);
  static ge::Status MergeRankTable(int32_t local_device_id,
                                   const rank_table_v2::RankTableInfo &local_rank_table,
                                   const rank_table_v2::RankTableInfo &peer_rank_table,
                                   rank_table_v2::RankTableInfo &merged_rank_table);

  std::string local_comm_res_;
  std::string peer_comm_res_;
  rank_table_v2::RankTableInfo merged_rank_table_;
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_RANK_TABLE_GENERATOR_V2_H_
