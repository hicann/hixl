/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef HIXL_ADXL_STATISTIC_MANAGER_H_
#define HIXL_ADXL_STATISTIC_MANAGER_H_

#include <atomic>

namespace adxl {
struct BufferTransferStatisticInfo {
  std::atomic<uint64_t> transfer_times = 0UL;
  std::atomic<uint64_t> transfer_suc_times = 0UL;
  std::atomic<uint64_t> transfer_max_cost = 0UL;
  std::atomic<uint64_t> transfer_total_cost = 0UL;
  std::atomic<uint64_t> client_copy_max_cost = 0UL;
  std::atomic<uint64_t> client_copy_total_cost = 0UL;
  std::atomic<uint64_t> client_copy_times = 0UL;
  std::atomic<uint64_t> server_d2d_max_cost = 0UL;
  std::atomic<uint64_t> server_d2d_total_cost = 0UL;
  std::atomic<uint64_t> server_d2d_times = 0UL;
  std::atomic<uint64_t> server_copy_max_cost = 0UL;
  std::atomic<uint64_t> server_copy_total_cost = 0UL;
  std::atomic<uint64_t> server_copy_times = 0UL;

  void Reset() {
    transfer_times.store(0UL);
    transfer_suc_times.store(0UL);
    transfer_max_cost.store(0UL);
    transfer_total_cost.store(0UL);
    client_copy_max_cost.store(0UL);
    client_copy_total_cost.store(0UL);
    client_copy_times.store(0UL);
    server_d2d_max_cost.store(0UL);
    server_d2d_total_cost.store(0UL);
    server_d2d_times.store(0UL);
    server_copy_max_cost.store(0UL);
    server_copy_total_cost.store(0UL);
    server_copy_times.store(0UL);
  }
};

class StatisticManager {
 public:
  static StatisticManager &GetInstance();
  ~StatisticManager() = default;
  StatisticManager(const StatisticManager &) = delete;
  StatisticManager(const StatisticManager &&) = delete;
  StatisticManager &operator=(const StatisticManager &) = delete;
  StatisticManager &operator=(const StatisticManager &&) = delete;

  void Dump();
  void Reset();
  void UpdateBufferTransferCost(const std::string &channel_name, uint64_t cost);
  void UpdateClientCopyCost(const std::string &channel_name, uint64_t cost);
  void UpdateServerD2DCost(const std::string &channel_name, uint64_t cost);
  void UpdateServerCopyCost(const std::string &channel_name, uint64_t cost);

 private:
  StatisticManager() = default;
  static void UpdateCost(uint64_t cost, std::atomic<uint64_t> &total_times, std::atomic<uint64_t> &max_cost,
                         std::atomic<uint64_t> &total_cost);
  void DumpBufferTransferStatisticInfo();

  std::mutex map_mutex_;
  std::unordered_map<std::string, BufferTransferStatisticInfo> buffer_transfer_statistic_info_;
};
}  // namespace adxl
#endif