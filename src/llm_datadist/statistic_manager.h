/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_STATISTIC_MANAGER_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_STATISTIC_MANAGER_H_

#include <atomic>

namespace llm {
struct LinkStatisticInfo {
  uint64_t comm_init_times = 0UL;
  uint64_t comm_destroy_times = 0UL;

  uint64_t comm_init_max_cost = 0UL;
  uint64_t comm_init_min_cost = UINT64_MAX;
  uint64_t comm_init_total_cost = 0UL;

  uint64_t comm_destroy_max_cost = 0UL;
  uint64_t comm_destroy_min_cost = UINT64_MAX;
  uint64_t comm_destroy_total_cost = 0UL;

  uint64_t exchange_mem_times = 0UL;
  uint64_t exchange_mem_max_cost = 0UL;
  uint64_t exchange_mem_min_cost = UINT64_MAX;
  uint64_t exchange_mem_total_cost = 0UL;

  uint64_t register_global_mem_times = 0UL;
  uint64_t deregister_global_mem_times = 0UL;

  uint64_t comm_bind_mem_times = 0UL;
  uint64_t comm_unbind_mem_times = 0UL;

  uint64_t comm_prepare_times = 0UL;
  uint64_t comm_prepare_max_cost = 0UL;
  uint64_t comm_prepare_min_cost = UINT64_MAX;
  uint64_t comm_prepare_total_cost = 0UL;

  void Reset() {
    comm_init_times = 0UL;
    comm_destroy_times = 0UL;

    comm_init_max_cost = 0UL;
    comm_init_min_cost = UINT64_MAX;
    comm_init_total_cost = 0UL;

    comm_destroy_max_cost = 0UL;
    comm_destroy_min_cost = UINT64_MAX;
    comm_destroy_total_cost = 0UL;

    exchange_mem_max_cost = 0UL;
    exchange_mem_min_cost = UINT64_MAX;
    exchange_mem_total_cost = 0UL;

    register_global_mem_times = 0UL;
    deregister_global_mem_times = 0UL;

    comm_bind_mem_times = 0UL;
    comm_unbind_mem_times = 0UL;

    comm_prepare_times = 0UL;
    comm_prepare_max_cost = 0UL;
    comm_prepare_min_cost = UINT64_MAX;
    comm_prepare_total_cost = 0UL;
  }
};

struct SendStatisticInfo {
  uint64_t batch_put_times = 0UL;
  uint64_t batch_put_max_cost = 0UL;
  uint64_t batch_put_min_cost = UINT64_MAX;
  uint64_t batch_put_total_cost = 0UL;

  uint64_t send_total_num = 0UL;
  uint64_t send_times = 0UL;
  uint64_t send_total_cost = 0UL;
  uint64_t send_max_cost = 0UL;
  uint64_t send_min_cost = UINT64_MAX;

  uint64_t event_record_times = 0UL;
  uint64_t event_record_total_cost = 0UL;

  uint64_t sync_flag_put_times = 0UL;
  uint64_t req_info_put_times = 0UL;

  void Reset() {
    batch_put_times = 0UL;
    batch_put_max_cost = 0UL;
    batch_put_min_cost = UINT64_MAX;
    batch_put_total_cost = 0UL;
    send_total_num = 0UL;
    send_times = 0UL;
    send_total_cost = 0UL;
    send_max_cost = 0UL;
    send_min_cost = UINT64_MAX;
    event_record_times = 0UL;
    event_record_times = 0UL;
    sync_flag_put_times = 0UL;
    req_info_put_times = 0UL;
  }
};

struct RecvStatisticInfo {
  uint64_t req_info_get_times = 0UL;
  uint64_t sync_flag_get_times = 0UL;

  uint64_t batch_get_times = 0UL;
  uint64_t batch_get_max_cost = 0UL;
  uint64_t batch_get_min_cost = UINT64_MAX;
  uint64_t batch_get_total_cost = 0UL;
  uint64_t get_total_num = 0UL;
  uint64_t pull_times = 0UL;
  uint64_t pull_total_cost = 0UL;
  uint64_t pull_max_cost = 0UL;
  uint64_t pull_min_cost = UINT64_MAX;

  void Reset() {
    req_info_get_times = 0UL;
    sync_flag_get_times = 0UL;
    batch_get_times = 0UL;
    batch_get_max_cost = 0UL;
    batch_get_min_cost = UINT64_MAX;
    batch_get_total_cost = 0UL;
  }
};

struct MemoryStatisticInfo {
  uint64_t alloc_mem = 0UL;
  uint64_t free_mem = 0UL;

  uint64_t alloc_times = 0UL;
  uint64_t free_times = 0UL;

  void Reset() {
    alloc_mem = 0UL;
    free_mem = 0UL;

    alloc_times = 0UL;
    free_times = 0UL;
  }
};

struct FuncStatisticInfo {
  std::atomic<uint64_t> link_func_times{0UL};
  std::atomic<uint64_t> link_func_max_cost{0UL};
  std::atomic<uint64_t> link_func_min_cost{UINT64_MAX};
  std::atomic<uint64_t> link_func_total_cost{0UL};

  std::atomic<uint64_t> unlink_func_times{0UL};
  std::atomic<uint64_t> unlink_func_max_cost{0UL};
  std::atomic<uint64_t> unlink_func_min_cost{UINT64_MAX};
  std::atomic<uint64_t> unlink_func_total_cost{0UL};

  std::atomic<uint64_t> register_func_times{0UL};
  std::atomic<uint64_t> deregister_func_times{0UL};

  std::atomic<uint64_t> pull_func_times{0UL};
  std::atomic<uint64_t> pull_func_max_cost{0UL};
  std::atomic<uint64_t> pull_func_min_cost{UINT64_MAX};
  std::atomic<uint64_t> pull_func_total_cost{0UL};

  std::atomic<uint64_t> copy_func_times{0UL};
  std::atomic<uint64_t> copy_func_max_cost{0UL};
  std::atomic<uint64_t> copy_func_min_cost{UINT64_MAX};
  std::atomic<uint64_t> copy_func_total_cost{0UL};

  std::atomic<uint64_t> swap_func_times{0UL};
  std::atomic<uint64_t> swap_func_max_cost{0UL};
  std::atomic<uint64_t> swap_func_min_cost{UINT64_MAX};
  std::atomic<uint64_t> swap_func_total_cost{0UL};

  std::atomic<uint64_t> transfer_func_times{0UL};
  std::atomic<uint64_t> transfer_func_max_cost{0UL};
  std::atomic<uint64_t> transfer_func_min_cost{UINT64_MAX};
  std::atomic<uint64_t> transfer_func_total_cost{0UL};

  void Reset() {
    link_func_times.store(0UL);
    link_func_max_cost.store(0UL);
    link_func_min_cost.store(UINT64_MAX);
    link_func_total_cost.store(0UL);

    unlink_func_times.store(0UL);
    unlink_func_max_cost.store(0UL);
    unlink_func_min_cost.store(UINT64_MAX);
    unlink_func_total_cost.store(0UL);

    register_func_times.store(0UL);
    deregister_func_times.store(0UL);

    pull_func_times.store(0UL);
    pull_func_max_cost.store(0UL);
    pull_func_min_cost.store(UINT64_MAX);
    pull_func_total_cost.store(0UL);

    copy_func_times.store(0UL);
    copy_func_max_cost.store(0UL);
    copy_func_min_cost.store(UINT64_MAX);
    copy_func_total_cost.store(0UL);

    swap_func_times.store(0UL);
    swap_func_max_cost.store(0UL);
    swap_func_min_cost.store(UINT64_MAX);
    swap_func_total_cost.store(0UL);

    transfer_func_times.store(0UL);
    transfer_func_max_cost.store(0UL);
    transfer_func_min_cost.store(UINT64_MAX);
    transfer_func_total_cost.store(0UL);
  }
};

class StatisticManager {
 public:
  static StatisticManager &GetInstance();
  static void UpdateCost(const uint64_t cost, uint64_t &total_times, uint64_t &min_cost, uint64_t &max_cost,
                         uint64_t &total_cost);
  static void UpdateCost(const uint64_t cost, std::atomic<uint64_t> &total_times, std::atomic<uint64_t> &min_cost,
                         std::atomic<uint64_t> &max_cost, std::atomic<uint64_t> &total_cost);
  ~StatisticManager() = default;
  StatisticManager(const StatisticManager &) = delete;
  StatisticManager(const StatisticManager &&) = delete;
  StatisticManager &operator=(const StatisticManager &) = delete;
  StatisticManager &operator=(const StatisticManager &&) = delete;
  void Dump() const;
  void Reset();

  void AddExchangeMemCost(const uint64_t cost);
  void AddCommInitCost(const uint64_t cost);
  void AddCommDestroyCost(const uint64_t cost);
  void AddBatchPutCost(const uint64_t cost);
  void AddRegisterGlobalMemTimes();
  void AddDeregisterGlobalMemTimes();
  void AddCommBindMemTimes();
  void AddCommUnbindMemTimes();
  void AddCommPrepareCost(const uint64_t cost);
  MemoryStatisticInfo &GetMemoryStatisticInfo();
  FuncStatisticInfo &GetFuncStatisticInfo();

 private:
  StatisticManager() = default;
  void DumpMemoryProfilingTrack() const;
  void DumpFuncProfilingTrack() const;
  void DumpLinkProfilingTrack() const;
  void DumpUnLinkProfilingTrack() const;
  LinkStatisticInfo link_statistic_info_;
  SendStatisticInfo send_statistic_info_;
  RecvStatisticInfo recv_statistic_info_;
  MemoryStatisticInfo memory_statistic_info_;
  FuncStatisticInfo func_statistic_info_;
};
}  // namespace llm
#endif