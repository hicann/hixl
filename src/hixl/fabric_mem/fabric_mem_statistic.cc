/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem/fabric_mem_statistic.h"

#include <chrono>

#include "common/hixl_log.h"
#include "common/statistic_utils.h"

namespace hixl {
void FabricMemCostStatisticInfo::Reset() {
  times.store(0UL, std::memory_order_relaxed);
  max_cost.store(0UL, std::memory_order_relaxed);
  total_cost.store(0UL, std::memory_order_relaxed);
}

void FabricMemTransferStatisticInfo::Reset() {
  transfer.Reset();
  real_copy.Reset();
  total_bytes.store(0UL, std::memory_order_relaxed);
  total_op_desc_count.store(0UL, std::memory_order_relaxed);
}

FabricMemStatistic::~FabricMemStatistic() {
  StopPeriodicDump();
}

std::string FabricMemStatistic::GetStatisticChannelId(const std::string &channel_id, bool is_client) {
  return statistic::GetStatisticChannelId(channel_id, is_client);
}

std::string FabricMemStatistic::GetClientStatisticChannelId(const std::string &channel_id) {
  return GetStatisticChannelId(channel_id, true);
}

std::string FabricMemStatistic::GetServerStatisticChannelId(const std::string &channel_id) {
  return GetStatisticChannelId(channel_id, false);
}

void FabricMemStatistic::RegisterChannel(const std::string &channel_id) {
  (void)GetOrCreateStatisticInfo(channel_id);
}

void FabricMemStatistic::RemoveStatisticChannel(const std::string &channel_id) {
  std::unique_lock<std::shared_mutex> lock(map_mutex_);
  transfer_statistic_info_.erase(channel_id);
}

void FabricMemStatistic::UpdateCost(uint64_t cost, FabricMemCostStatisticInfo &cost_info) {
  (void)cost_info.times.fetch_add(1U, std::memory_order_relaxed);
  (void)cost_info.total_cost.fetch_add(cost, std::memory_order_relaxed);
  auto current_max = cost_info.max_cost.load(std::memory_order_relaxed);
  while (current_max < cost && !cost_info.max_cost.compare_exchange_weak(current_max, cost, std::memory_order_relaxed,
                                                                         std::memory_order_relaxed)) {
  }
}

uint64_t FabricMemStatistic::GetAvgCost(const FabricMemCostStatisticInfo &cost_info) {
  const auto times = cost_info.times.load(std::memory_order_relaxed);
  if (times == 0U) {
    return 0U;
  }
  return cost_info.total_cost.load(std::memory_order_relaxed) / times;
}

FabricMemCostStatisticSnapshot FabricMemStatistic::ToSnapshot(const FabricMemCostStatisticInfo &cost_info) {
  return {cost_info.times.load(std::memory_order_relaxed), cost_info.max_cost.load(std::memory_order_relaxed),
          cost_info.total_cost.load(std::memory_order_relaxed)};
}

std::shared_ptr<FabricMemTransferStatisticInfo> FabricMemStatistic::GetOrCreateStatisticInfo(
    const std::string &channel_id) {
  {
    std::shared_lock<std::shared_mutex> lock(map_mutex_);
    const auto it = transfer_statistic_info_.find(channel_id);
    if (it != transfer_statistic_info_.end()) {
      return it->second;
    }
  }
  auto statistic_info = std::make_shared<FabricMemTransferStatisticInfo>();
  std::unique_lock<std::shared_mutex> lock(map_mutex_);
  auto [it, inserted] = transfer_statistic_info_.emplace(channel_id, statistic_info);
  return inserted ? statistic_info : it->second;
}

std::shared_ptr<FabricMemTransferStatisticInfo> FabricMemStatistic::GetStatisticInfo(
    const std::string &channel_id) const {
  std::shared_lock<std::shared_mutex> lock(map_mutex_);
  const auto it = transfer_statistic_info_.find(channel_id);
  if (it == transfer_statistic_info_.end()) {
    return nullptr;
  }
  return it->second;
}

void FabricMemStatistic::UpdateCosts(const std::string &channel_id, uint64_t transfer_cost, uint64_t real_copy_cost,
                                     uint64_t total_bytes, uint64_t op_desc_count) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCostsDirect(*info, transfer_cost, real_copy_cost, total_bytes, op_desc_count);
}

void FabricMemStatistic::UpdateCostsDirect(FabricMemTransferStatisticInfo &info, uint64_t transfer_cost,
                                            uint64_t real_copy_cost, uint64_t total_bytes, uint64_t op_desc_count) {
  UpdateCost(transfer_cost, info.transfer);
  UpdateCost(real_copy_cost, info.real_copy);
  (void)info.total_bytes.fetch_add(total_bytes, std::memory_order_relaxed);
  (void)info.total_op_desc_count.fetch_add(op_desc_count, std::memory_order_relaxed);
  if (info.transfer.times.load(std::memory_order_relaxed) > statistic::kResetTimes) {
    info.Reset();
  }
}

FabricMemTransferStatisticSnapshot FabricMemStatistic::GetSnapshot(const std::string &channel_id) const {
  auto info = GetStatisticInfo(channel_id);
  if (info == nullptr) {
    return {};
  }
  FabricMemTransferStatisticSnapshot snapshot;
  snapshot.transfer = ToSnapshot(info->transfer);
  snapshot.real_copy = ToSnapshot(info->real_copy);
  snapshot.total_bytes = info->total_bytes.load(std::memory_order_relaxed);
  snapshot.total_op_desc_count = info->total_op_desc_count.load(std::memory_order_relaxed);
  return snapshot;
}

void FabricMemStatistic::Dump() const {
  std::shared_lock<std::shared_mutex> lock(map_mutex_);
  for (const auto &item : transfer_statistic_info_) {
    const auto &stat_info = *item.second;
    const auto transfer_times = stat_info.transfer.times.load(std::memory_order_relaxed);
    if (transfer_times == 0U) {
      continue;
    }
    const auto total_bytes = stat_info.total_bytes.load(std::memory_order_relaxed);
    const auto total_op_desc_count = stat_info.total_op_desc_count.load(std::memory_order_relaxed);
    HIXL_EVENT(
        "Fabric mem transfer statistic info[channel:%s, transfer times:%lu, total size:%lu kBytes, "
        "avg size:%lu kBytes, avg bandwidth:%.4f GiB/s, max cost:%lu us, avg cost:%lu us, real copy times:%lu, "
        "max cost:%lu us, avg cost:%lu us].",
        item.first.c_str(), transfer_times, statistic::ToKBytes(total_bytes),
        statistic::ToKBytes(statistic::GetAvgBytesPerOpDesc(total_bytes, total_op_desc_count)),
        statistic::GetBandwidthGbps(total_bytes, stat_info.transfer.total_cost.load(std::memory_order_relaxed)),
        stat_info.transfer.max_cost.load(std::memory_order_relaxed), GetAvgCost(stat_info.transfer),
        stat_info.real_copy.times.load(std::memory_order_relaxed),
        stat_info.real_copy.max_cost.load(std::memory_order_relaxed), GetAvgCost(stat_info.real_copy));
  }
}

Status FabricMemStatistic::StartPeriodicDump() {
  return dump_task_.Start(std::chrono::milliseconds(statistic::kStatisticTimerPeriodMs), [this]() { Dump(); });
}

void FabricMemStatistic::StopPeriodicDump() {
  dump_task_.Stop();
}
}  // namespace hixl
