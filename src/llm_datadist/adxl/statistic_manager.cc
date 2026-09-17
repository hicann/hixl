/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "common/llm_log.h"
#include "common/statistic_utils.h"
#include "llm_datadist_timer.h"
#include "statistic_manager.h"

namespace adxl {
StatisticManager &StatisticManager::GetInstance() {
  (void)llm::LlmDatadistTimer::Instance();
  static StatisticManager instance;
  return instance;
}

std::string StatisticManager::GetStatisticChannelId(const std::string &channel_id, bool is_client) {
  return hixl::statistic::GetStatisticChannelId(channel_id, is_client);
}

std::string StatisticManager::GetClientStatisticChannelId(const std::string &channel_id) {
  return GetStatisticChannelId(channel_id, true);
}

std::string StatisticManager::GetServerStatisticChannelId(const std::string &channel_id) {
  return GetStatisticChannelId(channel_id, false);
}

void StatisticManager::StartPeriodicDumpIfNeeded() {
  std::lock_guard<std::mutex> lock(dump_mutex_);
  if (dump_timer_handle_ != nullptr) {
    return;
  }
  llm::LlmDatadistTimer::Instance().Init();
  dump_timer_handle_ = llm::LlmDatadistTimer::Instance().CreateTimer([this]() { Dump(); });
  (void)llm::LlmDatadistTimer::Instance().StartTimer(dump_timer_handle_, hixl::statistic::kStatisticTimerPeriodMs,
                                                     false);
}

StatisticManager::~StatisticManager() {
  std::lock_guard<std::mutex> lock(dump_mutex_);
  if (dump_timer_handle_ != nullptr) {
    (void)llm::LlmDatadistTimer::Instance().StopTimer(dump_timer_handle_);
    (void)llm::LlmDatadistTimer::Instance().DeleteTimer(dump_timer_handle_);
    dump_timer_handle_ = nullptr;
  }
}

void StatisticManager::RegisterChannel(const std::string &channel_id) {
  (void)GetOrCreateStatisticInfo(channel_id);
}

void StatisticManager::RemoveStatisticChannel(const std::string &channel_id, bool is_client) {
  RemoveStatisticInfo(GetStatisticChannelId(channel_id, is_client));
}

void StatisticManager::RemoveStatisticInfo(const std::string &channel_id) {
  std::unique_lock<std::shared_mutex> lock(map_mutex_);
  transfer_statistic_info_.erase(channel_id);
}

void StatisticManager::UpdateCost(const uint64_t cost, std::atomic<uint64_t> &total_times,
                                  std::atomic<uint64_t> &max_cost, std::atomic<uint64_t> &total_cost) {
  (void)total_times.fetch_add(1U, std::memory_order_relaxed);
  (void)total_cost.fetch_add(cost, std::memory_order_relaxed);
  auto current_max = max_cost.load(std::memory_order_relaxed);
  while (current_max < cost &&
         !max_cost.compare_exchange_weak(current_max, cost, std::memory_order_relaxed, std::memory_order_relaxed)) {
  }
}

CostStatisticSnapshot StatisticManager::ToSnapshot(const CostStatisticInfo &cost_info) {
  return {cost_info.times.load(std::memory_order_relaxed), cost_info.max_cost.load(std::memory_order_relaxed),
          cost_info.total_cost.load(std::memory_order_relaxed)};
}

std::shared_ptr<StatisticInfo> StatisticManager::GetOrCreateStatisticInfo(const std::string &channel_id) {
  {
    std::shared_lock<std::shared_mutex> lock(map_mutex_);
    auto it = transfer_statistic_info_.find(channel_id);
    if (it != transfer_statistic_info_.end()) {
      return it->second;
    }
  }

  auto statistic_info = std::make_shared<StatisticInfo>();
  std::unique_lock<std::shared_mutex> lock(map_mutex_);
  auto [it, inserted] = transfer_statistic_info_.emplace(channel_id, statistic_info);
  return inserted ? statistic_info : it->second;
}

std::shared_ptr<StatisticInfo> StatisticManager::GetStatisticInfo(const std::string &channel_id) const {
  std::shared_lock<std::shared_mutex> lock(map_mutex_);
  auto it = transfer_statistic_info_.find(channel_id);
  if (it == transfer_statistic_info_.end()) {
    return nullptr;
  }
  return it->second;
}

void StatisticManager::UpdateBufferTransferCost(const std::string &channel_id, uint64_t cost, uint64_t total_bytes,
                                                uint64_t op_desc_count) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  auto &statistic = info->CurrentBufferTransferStatistic();
  UpdateCost(cost, statistic.transfer.times, statistic.transfer.max_cost, statistic.transfer.total_cost);
  (void)statistic.total_bytes.fetch_add(total_bytes, std::memory_order_relaxed);
  (void)statistic.total_op_desc_count.fetch_add(op_desc_count, std::memory_order_relaxed);
}

void StatisticManager::UpdateClientCopyCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  auto &statistic = info->CurrentBufferTransferStatistic();
  UpdateCost(cost, statistic.client_copy.times, statistic.client_copy.max_cost, statistic.client_copy.total_cost);
}

void StatisticManager::UpdateServerD2DCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  auto &statistic = info->CurrentBufferTransferStatistic();
  UpdateCost(cost, statistic.server_d2d.times, statistic.server_d2d.max_cost, statistic.server_d2d.total_cost);
}

void StatisticManager::UpdateServerCopyCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  auto &statistic = info->CurrentBufferTransferStatistic();
  UpdateCost(cost, statistic.server_copy.times, statistic.server_copy.max_cost, statistic.server_copy.total_cost);
}

void StatisticManager::UpdateConnectTotalCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->connect_statistic_info.connect_total.times,
             info->connect_statistic_info.connect_total.max_cost,
             info->connect_statistic_info.connect_total.total_cost);
}

void StatisticManager::UpdateTcpConnectCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->connect_statistic_info.tcp_connect.times, info->connect_statistic_info.tcp_connect.max_cost,
             info->connect_statistic_info.tcp_connect.total_cost);
}

void StatisticManager::UpdateHcclTotalCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->connect_statistic_info.hccl_total.times, info->connect_statistic_info.hccl_total.max_cost,
             info->connect_statistic_info.hccl_total.total_cost);
}

void StatisticManager::UpdateHcclCommInitCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->connect_statistic_info.hccl_comm_init.times,
             info->connect_statistic_info.hccl_comm_init.max_cost,
             info->connect_statistic_info.hccl_comm_init.total_cost);
}

void StatisticManager::UpdateHcclCommBindMemCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->connect_statistic_info.hccl_comm_bind_mem.times,
             info->connect_statistic_info.hccl_comm_bind_mem.max_cost,
             info->connect_statistic_info.hccl_comm_bind_mem.total_cost);
}

void StatisticManager::UpdateHcclCommPrepareCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->connect_statistic_info.hccl_comm_prepare.times,
             info->connect_statistic_info.hccl_comm_prepare.max_cost,
             info->connect_statistic_info.hccl_comm_prepare.total_cost);
}

void StatisticManager::UpdateDirectTransferCost(const std::string &channel_id, uint64_t cost, uint64_t total_bytes,
                                                uint64_t op_desc_count) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  auto &statistic = info->CurrentDirectTransferStatistic();
  UpdateCost(cost, statistic.transfer.times, statistic.transfer.max_cost, statistic.transfer.total_cost);
  (void)statistic.total_bytes.fetch_add(total_bytes, std::memory_order_relaxed);
  (void)statistic.total_op_desc_count.fetch_add(op_desc_count, std::memory_order_relaxed);
}

StatisticInfoSnapshot StatisticManager::GetStatisticInfoSnapshot(const std::string &channel_id) const {
  auto info = GetStatisticInfo(channel_id);
  if (info == nullptr) {
    return {};
  }
  StatisticInfoSnapshot snapshot;
  snapshot.connect_statistic_info.connect_total = ToSnapshot(info->connect_statistic_info.connect_total);
  snapshot.connect_statistic_info.tcp_connect = ToSnapshot(info->connect_statistic_info.tcp_connect);
  snapshot.connect_statistic_info.hccl_total = ToSnapshot(info->connect_statistic_info.hccl_total);
  snapshot.connect_statistic_info.hccl_comm_init = ToSnapshot(info->connect_statistic_info.hccl_comm_init);
  snapshot.connect_statistic_info.hccl_comm_bind_mem = ToSnapshot(info->connect_statistic_info.hccl_comm_bind_mem);
  snapshot.connect_statistic_info.hccl_comm_prepare = ToSnapshot(info->connect_statistic_info.hccl_comm_prepare);
  const auto &buffer_statistic = info->CurrentBufferTransferStatistic();
  snapshot.buffer_transfer_statistic_info.transfer = ToSnapshot(buffer_statistic.transfer);
  snapshot.buffer_transfer_statistic_info.total_bytes = buffer_statistic.total_bytes.load(std::memory_order_relaxed);
  snapshot.buffer_transfer_statistic_info.total_op_desc_count =
      buffer_statistic.total_op_desc_count.load(std::memory_order_relaxed);
  const auto &direct_statistic = info->CurrentDirectTransferStatistic();
  snapshot.direct_transfer_statistic_info.transfer = ToSnapshot(direct_statistic.transfer);
  snapshot.direct_transfer_statistic_info.total_bytes = direct_statistic.total_bytes.load(std::memory_order_relaxed);
  snapshot.direct_transfer_statistic_info.total_op_desc_count =
      direct_statistic.total_op_desc_count.load(std::memory_order_relaxed);
  return snapshot;
}

void StatisticManager::Dump() const {
  DumpBufferTransferStatisticInfo();
  DumpDirectTransferStatisticInfo();
}

void StatisticManager::DumpBufferTransferStatisticInfo() const {
  DumpTransferStatisticSummary(false);
}

void StatisticManager::DumpDirectTransferStatisticInfo() const {
  DumpTransferStatisticSummary(true);
}

void StatisticManager::DumpTransferStatisticSummary(bool is_direct) const {
  hixl::statistic::TransferSummary summary;
  {
    std::shared_lock<std::shared_mutex> lock(map_mutex_);
    for (const auto &item : transfer_statistic_info_) {
      auto &mutable_info = *item.second;
      if (is_direct) {
        const auto current = mutable_info.direct_transfer_statistic_index.load(std::memory_order_acquire);
        const auto next = current ^ 1U;
        mutable_info.direct_transfer_statistic_slots[next].Reset();
        mutable_info.direct_transfer_statistic_index.store(next, std::memory_order_release);
        const auto &statistic = mutable_info.direct_transfer_statistic_slots[current];
        const auto &transfer = statistic.transfer;
        summary.Accumulate(item.first, transfer.times.load(std::memory_order_relaxed),
                           statistic.total_bytes.load(std::memory_order_relaxed),
                           statistic.total_op_desc_count.load(std::memory_order_relaxed),
                           transfer.total_cost.load(std::memory_order_relaxed));
      } else {
        const auto current = mutable_info.buffer_transfer_statistic_index.load(std::memory_order_acquire);
        const auto next = current ^ 1U;
        mutable_info.buffer_transfer_statistic_slots[next].Reset();
        mutable_info.buffer_transfer_statistic_index.store(next, std::memory_order_release);
        const auto &statistic = mutable_info.buffer_transfer_statistic_slots[current];
        const auto &transfer = statistic.transfer;
        summary.Accumulate(item.first, transfer.times.load(std::memory_order_relaxed),
                           statistic.total_bytes.load(std::memory_order_relaxed),
                           statistic.total_op_desc_count.load(std::memory_order_relaxed),
                           transfer.total_cost.load(std::memory_order_relaxed));
      }
    }
  }
  if (summary.active_channels == 0UL) {
    return;
  }
  LLMEVENT(
      "%s transfer statistic summary[transfer times:%lu, avg size:%lu kBytes, max bandwidth:%.4f GB/s, "
      "min bandwidth:%.4f GB/s, avg bandwidth:%.4f GB/s, min bandwidth channel:%s].",
      is_direct ? "Direct" : "Buffer", summary.transfer_times,
      hixl::statistic::ToKBytes(
          hixl::statistic::GetAvgBytesPerOpDesc(summary.total_bytes, summary.total_op_desc_count)),
      summary.max_bandwidth, summary.min_bandwidth, summary.AvgBandwidth(), summary.min_bandwidth_channel.c_str());
}

}  // namespace adxl
