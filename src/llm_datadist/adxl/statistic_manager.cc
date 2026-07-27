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
  UpdateCost(cost, info->buffer_transfer_statistic_info.transfer.times,
             info->buffer_transfer_statistic_info.transfer.max_cost,
             info->buffer_transfer_statistic_info.transfer.total_cost);
  (void)info->buffer_transfer_statistic_info.total_bytes.fetch_add(total_bytes, std::memory_order_relaxed);
  (void)info->buffer_transfer_statistic_info.total_op_desc_count.fetch_add(op_desc_count, std::memory_order_relaxed);
  if (info->buffer_transfer_statistic_info.transfer.times.load(std::memory_order_relaxed) >
      hixl::statistic::kResetTimes) {
    info->buffer_transfer_statistic_info.Reset();
  }
}

void StatisticManager::UpdateClientCopyCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->buffer_transfer_statistic_info.client_copy.times,
             info->buffer_transfer_statistic_info.client_copy.max_cost,
             info->buffer_transfer_statistic_info.client_copy.total_cost);
}

void StatisticManager::UpdateServerD2DCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->buffer_transfer_statistic_info.server_d2d.times,
             info->buffer_transfer_statistic_info.server_d2d.max_cost,
             info->buffer_transfer_statistic_info.server_d2d.total_cost);
}

void StatisticManager::UpdateServerCopyCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->buffer_transfer_statistic_info.server_copy.times,
             info->buffer_transfer_statistic_info.server_copy.max_cost,
             info->buffer_transfer_statistic_info.server_copy.total_cost);
  if (info->buffer_transfer_statistic_info.server_copy.times.load(std::memory_order_relaxed) >
      hixl::statistic::kResetTimes) {
    info->buffer_transfer_statistic_info.Reset();
  }
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
  UpdateCost(cost, info->direct_transfer_statistic_info.transfer.times,
             info->direct_transfer_statistic_info.transfer.max_cost,
             info->direct_transfer_statistic_info.transfer.total_cost);
  (void)info->direct_transfer_statistic_info.total_bytes.fetch_add(total_bytes, std::memory_order_relaxed);
  (void)info->direct_transfer_statistic_info.total_op_desc_count.fetch_add(op_desc_count, std::memory_order_relaxed);
  if (info->direct_transfer_statistic_info.transfer.times.load(std::memory_order_relaxed) >
      hixl::statistic::kResetTimes) {
    info->direct_transfer_statistic_info.Reset();
  }
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
  snapshot.buffer_transfer_statistic_info.transfer = ToSnapshot(info->buffer_transfer_statistic_info.transfer);
  snapshot.buffer_transfer_statistic_info.total_bytes =
      info->buffer_transfer_statistic_info.total_bytes.load(std::memory_order_relaxed);
  snapshot.buffer_transfer_statistic_info.total_op_desc_count =
      info->buffer_transfer_statistic_info.total_op_desc_count.load(std::memory_order_relaxed);
  snapshot.direct_transfer_statistic_info.transfer = ToSnapshot(info->direct_transfer_statistic_info.transfer);
  snapshot.direct_transfer_statistic_info.total_bytes =
      info->direct_transfer_statistic_info.total_bytes.load(std::memory_order_relaxed);
  snapshot.direct_transfer_statistic_info.total_op_desc_count =
      info->direct_transfer_statistic_info.total_op_desc_count.load(std::memory_order_relaxed);
  return snapshot;
}

void StatisticManager::Dump() {
  DumpBufferTransferStatisticInfo();
  DumpDirectTransferStatisticInfo();
}

void StatisticManager::DumpBufferTransferStatisticInfo() {
  DumpTransferStatisticSummary(false);
}

void StatisticManager::DumpDirectTransferStatisticInfo() {
  DumpTransferStatisticSummary(true);
}

void StatisticManager::DumpTransferStatisticSummary(bool is_direct) {
  hixl::statistic::TransferSummary summary;
  {
    std::shared_lock<std::shared_mutex> lock(map_mutex_);
    for (const auto &item : transfer_statistic_info_) {
      const auto &transfer = is_direct ? item.second->direct_transfer_statistic_info.transfer
                                       : item.second->buffer_transfer_statistic_info.transfer;
      const auto &total_bytes_ref = is_direct ? item.second->direct_transfer_statistic_info.total_bytes
                                              : item.second->buffer_transfer_statistic_info.total_bytes;
      const auto &total_op_desc_count_ref = is_direct ? item.second->direct_transfer_statistic_info.total_op_desc_count
                                                      : item.second->buffer_transfer_statistic_info.total_op_desc_count;
      summary.Accumulate(
          item.first, transfer.times.load(std::memory_order_relaxed), total_bytes_ref.load(std::memory_order_relaxed),
          total_op_desc_count_ref.load(std::memory_order_relaxed), transfer.total_cost.load(std::memory_order_relaxed));
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
