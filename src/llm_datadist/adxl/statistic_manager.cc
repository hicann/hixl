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
#include "statistic_manager.h"
namespace adxl {
namespace {
constexpr uint64_t kResetTimes = 100000UL;
constexpr uint64_t kFabricMemResetTimes = 10000UL;
}  // namespace
StatisticManager &StatisticManager::GetInstance() {
  static StatisticManager instance;
  return instance;
}

void StatisticManager::SetEnableUseFabricMem(bool enable_use_frabric_mem) {
  enable_use_frabric_mem_ = enable_use_frabric_mem;
}

void StatisticManager::RemoveChannel(const std::string &channel_id) {
  std::lock_guard<std::mutex> lock(map_mutex_);
  auto it = transfer_statistic_info_.find(channel_id);
  if (it != transfer_statistic_info_.end()) {
    transfer_statistic_info_.erase(it);
  }
}

void StatisticManager::UpdateCost(const uint64_t cost, std::atomic<uint64_t> &total_times,
                                  std::atomic<uint64_t> &max_cost, std::atomic<uint64_t> &total_cost) {
  (void)total_times.fetch_add(1U, std::memory_order_relaxed);
  (void)total_cost.fetch_add(cost, std::memory_order_relaxed);
  if (max_cost.load() < cost) {
    max_cost.store(cost, std::memory_order_relaxed);
  }
}

void StatisticManager::UpdateBufferTransferCost(const std::string &channel_id, const uint64_t cost) {
  std::lock_guard<std::mutex> lock(map_mutex_);
  auto &info = transfer_statistic_info_[channel_id];
  UpdateCost(cost, info.buffer_transfer_statistic_info.transfer_times,
             info.buffer_transfer_statistic_info.transfer_max_cost,
             info.buffer_transfer_statistic_info.transfer_total_cost);
  if (info.buffer_transfer_statistic_info.transfer_times.load(std::memory_order_relaxed) > kResetTimes) {
    info.buffer_transfer_statistic_info.Reset();
  }
}

void StatisticManager::UpdateClientCopyCost(const std::string &channel_id, const uint64_t cost) {
  std::lock_guard<std::mutex> lock(map_mutex_);
  auto &info = transfer_statistic_info_[channel_id];
  UpdateCost(cost, info.buffer_transfer_statistic_info.client_copy_times,
             info.buffer_transfer_statistic_info.client_copy_max_cost,
             info.buffer_transfer_statistic_info.client_copy_total_cost);
}

void StatisticManager::UpdateServerD2DCost(const std::string &channel_id, const uint64_t cost) {
  std::lock_guard<std::mutex> lock(map_mutex_);
  auto &info = transfer_statistic_info_[channel_id];
  UpdateCost(cost, info.buffer_transfer_statistic_info.server_d2d_times,
             info.buffer_transfer_statistic_info.server_d2d_max_cost,
             info.buffer_transfer_statistic_info.server_d2d_total_cost);
}

void StatisticManager::UpdateServerCopyCost(const std::string &channel_id, const uint64_t cost) {
  std::lock_guard<std::mutex> lock(map_mutex_);
  auto &info = transfer_statistic_info_[channel_id];
  UpdateCost(cost, info.buffer_transfer_statistic_info.server_copy_times,
             info.buffer_transfer_statistic_info.server_copy_max_cost,
             info.buffer_transfer_statistic_info.server_copy_total_cost);
  if (info.buffer_transfer_statistic_info.server_copy_times.load(std::memory_order_relaxed) > kResetTimes) {
    info.buffer_transfer_statistic_info.Reset();
  }
}

void StatisticManager::UpdateFabricMemTransferCost(const std::string &channel_id, const uint64_t cost) {
  std::lock_guard<std::mutex> lock(map_mutex_);
  auto &info = transfer_statistic_info_[channel_id];
  UpdateCost(cost, info.fabric_mem_transfer_statistic_info.transfer_times,
             info.fabric_mem_transfer_statistic_info.transfer_max_cost,
             info.fabric_mem_transfer_statistic_info.transfer_total_cost);
  if (info.fabric_mem_transfer_statistic_info.transfer_times.load(std::memory_order_relaxed) > kFabricMemResetTimes) {
    info.fabric_mem_transfer_statistic_info.Reset();
  }
}

void StatisticManager::UpdateFabricMemRealCopyCost(const std::string &channel_id, const uint64_t cost) {
  std::lock_guard<std::mutex> lock(map_mutex_);
  auto &info = transfer_statistic_info_[channel_id];
  UpdateCost(cost, info.fabric_mem_transfer_statistic_info.real_copy_times,
             info.fabric_mem_transfer_statistic_info.real_copy_max_cost,
             info.fabric_mem_transfer_statistic_info.real_copy_total_cost);
}

void StatisticManager::Dump() {
  if (enable_use_frabric_mem_) {
    DumpFabricMemTransferStatisticInfo();
  } else {
    DumpBufferTransferStatisticInfo();
  }
}

void StatisticManager::DumpBufferTransferStatisticInfo() {
  std::lock_guard<std::mutex> lock(map_mutex_);
  for (auto &item : transfer_statistic_info_) {
    auto &stat_info = item.second;
    // cal avg time
    auto transfer_times = stat_info.buffer_transfer_statistic_info.transfer_times.load(std::memory_order_relaxed);
    const uint64_t buffer_transfer_avg_cost =
        transfer_times == 0U
            ? 0U
            : stat_info.buffer_transfer_statistic_info.transfer_total_cost.load(std::memory_order_relaxed) /
                  transfer_times;
    // cal client copy avg time
    auto client_copy_times = stat_info.buffer_transfer_statistic_info.client_copy_times.load(std::memory_order_relaxed);
    const uint64_t client_copy_avg_cost =
        client_copy_times == 0U
            ? 0U
            : stat_info.buffer_transfer_statistic_info.client_copy_total_cost.load(std::memory_order_relaxed) /
                  client_copy_times;
    // cal server d2d avg time
    auto server_d2d_times = stat_info.buffer_transfer_statistic_info.server_d2d_times.load(std::memory_order_relaxed);
    const uint64_t server_d2d_avg_cost =
        server_d2d_times == 0U
            ? 0U
            : stat_info.buffer_transfer_statistic_info.server_d2d_total_cost.load(std::memory_order_relaxed) /
                  server_d2d_times;
    // cal server copy avg time
    auto server_copy_times = stat_info.buffer_transfer_statistic_info.server_copy_times.load(std::memory_order_relaxed);
    const uint64_t server_copy_avg_cost =
        server_copy_times == 0U
            ? 0U
            : stat_info.buffer_transfer_statistic_info.server_copy_total_cost.load() / server_copy_times;
    LLMEVENT(
        "Buffer transfer statistic info[channel:%s, transfer times:%lu, max cost:%lu us, avg cost:%lu us, client copy "
        "times:%lu, "
        "max cost:%lu us, avg cost:%lu us, server comm times:%lu, max cost:%lu us, avg cost:%lu us, server "
        "copy times:%lu, max cost:%lu us, avg cost:%lu us].",
        item.first.c_str(), transfer_times,
        stat_info.buffer_transfer_statistic_info.transfer_max_cost.load(std::memory_order_relaxed),
        buffer_transfer_avg_cost, client_copy_times,
        stat_info.buffer_transfer_statistic_info.client_copy_max_cost.load(std::memory_order_relaxed),
        client_copy_avg_cost, server_d2d_times,
        stat_info.buffer_transfer_statistic_info.server_d2d_max_cost.load(std::memory_order_relaxed),
        server_d2d_avg_cost, server_copy_times,
        stat_info.buffer_transfer_statistic_info.server_copy_max_cost.load(std::memory_order_relaxed),
        server_copy_avg_cost);
  }
}

void StatisticManager::DumpFabricMemTransferStatisticInfo() {
  std::lock_guard<std::mutex> lock(map_mutex_);
  for (auto &item : transfer_statistic_info_) {
    auto &stat_info = item.second;
    // cal avg time
    auto transfer_times = stat_info.fabric_mem_transfer_statistic_info.transfer_times.load(std::memory_order_relaxed);
    const uint64_t fabric_mem_transfer_avg_cost =
        transfer_times == 0U
            ? 0U
            : stat_info.fabric_mem_transfer_statistic_info.transfer_total_cost.load(std::memory_order_relaxed) /
                  transfer_times;
    // cal real copy avg time
    auto real_copy_times = stat_info.fabric_mem_transfer_statistic_info.real_copy_times.load(std::memory_order_relaxed);
    const uint64_t real_copy_avg_cost =
        real_copy_times == 0U
            ? 0U
            : stat_info.fabric_mem_transfer_statistic_info.real_copy_total_cost.load(std::memory_order_relaxed) /
                  real_copy_times;
    LLMEVENT(
        "Fabric mem transfer statistic info[channel:%s, transfer times:%lu, max cost:%lu us, avg cost:%lu us, real "
        "copy times:%lu, max cost:%lu us, avg cost:%lu us].",
        item.first.c_str(), transfer_times,
        stat_info.fabric_mem_transfer_statistic_info.transfer_max_cost.load(std::memory_order_relaxed),
        fabric_mem_transfer_avg_cost, real_copy_times,
        stat_info.fabric_mem_transfer_statistic_info.real_copy_max_cost.load(std::memory_order_relaxed),
        real_copy_avg_cost);
  }
}

void StatisticManager::Reset() {
  std::lock_guard<std::mutex> lock(map_mutex_);
  for (auto &item : transfer_statistic_info_) {
    auto &stat_info = item.second;
    stat_info.buffer_transfer_statistic_info.Reset();
    stat_info.fabric_mem_transfer_statistic_info.Reset();
  }
}
}  // namespace adxl
