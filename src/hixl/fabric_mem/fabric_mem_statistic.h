/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_STATISTIC_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_STATISTIC_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "common/periodic_task.h"

namespace hixl {
struct FabricMemCostStatisticInfo {
  std::atomic<uint64_t> times = 0UL;
  std::atomic<uint64_t> max_cost = 0UL;
  std::atomic<uint64_t> total_cost = 0UL;

  void Reset();
};

struct FabricMemTransferStatisticInfo {
  FabricMemCostStatisticInfo transfer;
  FabricMemCostStatisticInfo real_copy;
  std::atomic<uint64_t> total_bytes = 0UL;
  std::atomic<uint64_t> total_op_desc_count = 0UL;

  void Reset();
};

struct FabricMemCostStatisticSnapshot {
  uint64_t times = 0UL;
  uint64_t max_cost = 0UL;
  uint64_t total_cost = 0UL;
};

struct FabricMemTransferStatisticSnapshot {
  FabricMemCostStatisticSnapshot transfer;
  FabricMemCostStatisticSnapshot real_copy;
  uint64_t total_bytes = 0UL;
  uint64_t total_op_desc_count = 0UL;
};

class FabricMemStatistic {
 public:
  FabricMemStatistic() = default;
  ~FabricMemStatistic();
  FabricMemStatistic(const FabricMemStatistic &) = delete;
  FabricMemStatistic &operator=(const FabricMemStatistic &) = delete;
  FabricMemStatistic(FabricMemStatistic &&) = delete;
  FabricMemStatistic &operator=(FabricMemStatistic &&) = delete;

  static std::string GetStatisticChannelId(const std::string &channel_id, bool is_client);
  static std::string GetClientStatisticChannelId(const std::string &channel_id);
  static std::string GetServerStatisticChannelId(const std::string &channel_id);

  void RegisterChannel(const std::string &channel_id);
  void RemoveStatisticChannel(const std::string &channel_id);
  void UpdateCosts(const std::string &channel_id, uint64_t transfer_cost, uint64_t real_copy_cost, uint64_t total_bytes,
                   uint64_t op_desc_count);
  void UpdateCostsDirect(FabricMemTransferStatisticInfo &info, uint64_t transfer_cost, uint64_t real_copy_cost,
                         uint64_t total_bytes, uint64_t op_desc_count);
  std::shared_ptr<FabricMemTransferStatisticInfo> GetOrCreateStatisticInfo(const std::string &channel_id);
  FabricMemTransferStatisticSnapshot GetSnapshot(const std::string &channel_id) const;
  void Dump() const;
  Status StartPeriodicDump();
  void StopPeriodicDump();

 private:
  static void UpdateCost(uint64_t cost, FabricMemCostStatisticInfo &cost_info);
  static uint64_t GetAvgCost(const FabricMemCostStatisticInfo &cost_info);
  static FabricMemCostStatisticSnapshot ToSnapshot(const FabricMemCostStatisticInfo &cost_info);
  std::shared_ptr<FabricMemTransferStatisticInfo> GetStatisticInfo(const std::string &channel_id) const;

  PeriodicTask dump_task_;
  mutable std::shared_mutex map_mutex_;
  std::unordered_map<std::string, std::shared_ptr<FabricMemTransferStatisticInfo>> transfer_statistic_info_;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_STATISTIC_H_
