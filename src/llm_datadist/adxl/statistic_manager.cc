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
}
StatisticManager &StatisticManager::GetInstance() {
  static StatisticManager instance;
  return instance;
}
void StatisticManager::UpdateCost(const uint64_t cost, std::atomic<uint64_t> &total_times,
                                  std::atomic<uint64_t> &max_cost, std::atomic<uint64_t> &total_cost) {
  (void)total_times.fetch_add(1U, std::memory_order_relaxed);
  (void)total_cost.fetch_add(cost, std::memory_order_relaxed);
  if (max_cost.load() < cost) {
    max_cost.store(cost, std::memory_order_relaxed);
  }
}

void StatisticManager::UpdateBufferTransferCost(const std::string &channel_name, const uint64_t cost) {
  auto &info = buffer_transfer_statistic_info_[channel_name];
  UpdateCost(cost, info.transfer_times, info.transfer_max_cost, info.transfer_total_cost);
  if (info.transfer_times.load(std::memory_order_relaxed) > kResetTimes) {
    info.Reset();
  }
}

void StatisticManager::UpdateClientCopyCost(const std::string &channel_name, const uint64_t cost) {
  auto &info = buffer_transfer_statistic_info_[channel_name];
  UpdateCost(cost, info.client_copy_times, info.client_copy_max_cost, info.client_copy_total_cost);
}

void StatisticManager::UpdateServerD2DCost(const std::string &channel_name, const uint64_t cost) {
  auto &info = buffer_transfer_statistic_info_[channel_name];
  UpdateCost(cost, info.server_d2d_times, info.server_d2d_max_cost, info.server_d2d_total_cost);
}

void StatisticManager::UpdateServerCopyCost(const std::string &channel_name, const uint64_t cost) {
  auto &info = buffer_transfer_statistic_info_[channel_name];
  UpdateCost(cost, info.server_copy_times, info.server_copy_max_cost, info.server_copy_total_cost);
  if (info.server_copy_times.load(std::memory_order_relaxed) > kResetTimes) {
    info.Reset();
  }
}

void StatisticManager::Dump() {
  DumpBufferTransferStatisticInfo();
}

void StatisticManager::DumpBufferTransferStatisticInfo() {
  for (auto &item : buffer_transfer_statistic_info_) {
    auto &stat_info = item.second;
    // cal avg time
    auto transfer_times = stat_info.transfer_times.load(std::memory_order_relaxed);
    const uint64_t buffer_transfer_avg_cost =
        transfer_times == 0U ? 0U : stat_info.transfer_total_cost.load(std::memory_order_relaxed) / transfer_times;
    // cal client copy avg time
    auto client_copy_times = stat_info.client_copy_times.load(std::memory_order_relaxed);
    const uint64_t client_copy_avg_cost =
        client_copy_times == 0U ? 0U
                                : stat_info.client_copy_total_cost.load(std::memory_order_relaxed) / client_copy_times;
    // cal server d2d avg time
    auto server_d2d_times = stat_info.server_d2d_times.load(std::memory_order_relaxed);
    const uint64_t server_d2d_avg_cost =
        server_d2d_times == 0U ? 0U
                               : stat_info.server_d2d_total_cost.load(std::memory_order_relaxed) / server_d2d_times;
    // cal server copy avg time
    auto server_copy_times = stat_info.server_copy_times.load(std::memory_order_relaxed);
    const uint64_t server_copy_avg_cost =
        server_copy_times == 0U ? 0U : stat_info.server_copy_total_cost.load() / server_copy_times;
    LLMEVENT(
        "Buffer transfer statistic info[channel:%s, transfer times:%lu, max cost:%lu us, avg cost:%lu us, client copy "
        "times:%lu, "
        "max cost:%lu us, avg cost:%lu us, server comm times:%lu, max cost:%lu us, avg cost:%lu us, server "
        "copy times:%lu, max cost:%lu us, avg cost:%lu us]. ",
        item.first.c_str(), transfer_times, stat_info.transfer_max_cost.load(std::memory_order_relaxed),
        buffer_transfer_avg_cost, client_copy_times, stat_info.client_copy_max_cost.load(std::memory_order_relaxed),
        client_copy_avg_cost, server_d2d_times, stat_info.server_d2d_max_cost.load(std::memory_order_relaxed),
        server_d2d_avg_cost, server_copy_times, stat_info.server_copy_max_cost.load(std::memory_order_relaxed),
        server_copy_avg_cost);
  }
}

void StatisticManager::Reset() {
  for (auto &item : buffer_transfer_statistic_info_) {
    auto &stat_info = item.second;
    stat_info.Reset();
  }
}
}  // namespace adxl
