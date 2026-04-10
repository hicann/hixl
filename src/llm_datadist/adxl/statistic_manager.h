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
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
namespace adxl {
struct CostStatisticInfo {
  std::atomic<uint64_t> times = 0UL;
  std::atomic<uint64_t> max_cost = 0UL;
  std::atomic<uint64_t> total_cost = 0UL;

  void Reset() {
    times.store(0UL);
    max_cost.store(0UL);
    total_cost.store(0UL);
  }
};

struct ConnectStatisticInfo {
  CostStatisticInfo connect_total;
  CostStatisticInfo tcp_connect;
  CostStatisticInfo hccl_total;
  CostStatisticInfo hccl_comm_init;
  CostStatisticInfo hccl_comm_bind_mem;
  CostStatisticInfo hccl_comm_prepare;

  void Reset() {
    connect_total.Reset();
    tcp_connect.Reset();
    hccl_total.Reset();
    hccl_comm_init.Reset();
    hccl_comm_bind_mem.Reset();
    hccl_comm_prepare.Reset();
  }
};

struct BufferTransferStatisticInfo {
  CostStatisticInfo transfer;
  CostStatisticInfo client_copy;
  CostStatisticInfo server_d2d;
  CostStatisticInfo server_copy;
  std::atomic<uint64_t> total_bytes = 0UL;
  std::atomic<uint64_t> total_op_desc_count = 0UL;

  void Reset() {
    transfer.Reset();
    client_copy.Reset();
    server_d2d.Reset();
    server_copy.Reset();
    total_bytes.store(0UL);
    total_op_desc_count.store(0UL);
  }
};

struct FabricMemTransferStatisticInfo {
  CostStatisticInfo transfer;
  CostStatisticInfo real_copy;
  std::atomic<uint64_t> total_bytes = 0UL;
  std::atomic<uint64_t> total_op_desc_count = 0UL;

  void Reset() {
    transfer.Reset();
    real_copy.Reset();
    total_bytes.store(0UL);
    total_op_desc_count.store(0UL);
  }
};

struct DirectTransferStatisticInfo {
  CostStatisticInfo transfer;
  std::atomic<uint64_t> total_bytes = 0UL;
  std::atomic<uint64_t> total_op_desc_count = 0UL;

  void Reset() {
    transfer.Reset();
    total_bytes.store(0UL);
    total_op_desc_count.store(0UL);
  }
};

struct StatisticInfo {
  ConnectStatisticInfo connect_statistic_info;
  BufferTransferStatisticInfo buffer_transfer_statistic_info;
  FabricMemTransferStatisticInfo fabric_mem_transfer_statistic_info;
  DirectTransferStatisticInfo direct_transfer_statistic_info;
};

struct CostStatisticSnapshot {
  uint64_t times = 0UL;
  uint64_t max_cost = 0UL;
  uint64_t total_cost = 0UL;
};

struct ConnectStatisticSnapshot {
  CostStatisticSnapshot connect_total;
  CostStatisticSnapshot tcp_connect;
  CostStatisticSnapshot hccl_total;
  CostStatisticSnapshot hccl_comm_init;
  CostStatisticSnapshot hccl_comm_bind_mem;
  CostStatisticSnapshot hccl_comm_prepare;
};

struct TransferStatisticSnapshot {
  CostStatisticSnapshot transfer;
  uint64_t total_bytes = 0UL;
  uint64_t total_op_desc_count = 0UL;
};

struct StatisticInfoSnapshot {
  ConnectStatisticSnapshot connect_statistic_info;
  TransferStatisticSnapshot buffer_transfer_statistic_info;
  TransferStatisticSnapshot fabric_mem_transfer_statistic_info;
  TransferStatisticSnapshot direct_transfer_statistic_info;
};

class StatisticManager {
 public:
  static StatisticManager &GetInstance();
  static std::string GetStatisticChannelId(const std::string &channel_id, bool is_client);
  static std::string GetClientStatisticChannelId(const std::string &channel_id);
  static std::string GetServerStatisticChannelId(const std::string &channel_id);
  ~StatisticManager();
  StatisticManager(const StatisticManager &) = delete;
  StatisticManager(const StatisticManager &&) = delete;
  StatisticManager &operator=(const StatisticManager &) = delete;
  StatisticManager &operator=(const StatisticManager &&) = delete;

  void Dump();
  void RegisterChannel(const std::string &channel_id);
  void UpdateBufferTransferCost(const std::string &channel_id, uint64_t cost, uint64_t total_bytes,
                                uint64_t op_desc_count);
  void UpdateClientCopyCost(const std::string &channel_id, uint64_t cost);
  void UpdateServerD2DCost(const std::string &channel_id, uint64_t cost);
  void UpdateServerCopyCost(const std::string &channel_id, uint64_t cost);
  void UpdateConnectTotalCost(const std::string &channel_id, uint64_t cost);
  void UpdateTcpConnectCost(const std::string &channel_id, uint64_t cost);
  void UpdateHcclTotalCost(const std::string &channel_id, uint64_t cost);
  void UpdateHcclCommInitCost(const std::string &channel_id, uint64_t cost);
  void UpdateHcclCommBindMemCost(const std::string &channel_id, uint64_t cost);
  void UpdateHcclCommPrepareCost(const std::string &channel_id, uint64_t cost);
  void UpdateFabricMemCosts(const std::string &channel_id, uint64_t transfer_cost, uint64_t real_copy_cost,
                            uint64_t total_bytes, uint64_t op_desc_count);
  void UpdateDirectTransferCost(const std::string &channel_id, uint64_t cost, uint64_t total_bytes,
                                uint64_t op_desc_count);

  void SetEnableUseFabricMem(bool enable_use_fabric_mem);
  void RemoveStatisticChannel(const std::string &channel_id, bool is_client);
  void StartPeriodicDumpIfNeeded();
  StatisticInfoSnapshot GetStatisticInfoSnapshot(const std::string &channel_id) const;

 private:
  StatisticManager() = default;
  static void UpdateCost(uint64_t cost, std::atomic<uint64_t> &total_times, std::atomic<uint64_t> &max_cost,
                         std::atomic<uint64_t> &total_cost);
  void RemoveStatisticInfo(const std::string &channel_id);
  static uint64_t GetAvgCost(const CostStatisticInfo &cost_info);
  static uint64_t GetOtherTotalCost(const ConnectStatisticInfo &cost_info);
  static CostStatisticSnapshot ToSnapshot(const CostStatisticInfo &cost_info);
  void DumpBufferTransferStatisticInfo();
  void DumpConnectStatisticInfo();
  void DumpFabricMemTransferStatisticInfo();
  void DumpDirectTransferStatisticInfo();
  std::shared_ptr<StatisticInfo> GetOrCreateStatisticInfo(const std::string &channel_id);
  std::shared_ptr<StatisticInfo> GetStatisticInfo(const std::string &channel_id) const;

  bool enable_use_fabric_mem_ = false;
  std::mutex dump_mutex_;
  void *dump_timer_handle_{nullptr};
  mutable std::shared_mutex map_mutex_;
  std::unordered_map<std::string, std::shared_ptr<StatisticInfo>> transfer_statistic_info_;
};
}  // namespace adxl
#endif
