/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "common/llm_log.h"
#include "statistic_manager.h"
namespace llm {
StatisticManager &StatisticManager::GetInstance() {
  static StatisticManager instance;
  return instance;
}
void StatisticManager::UpdateCost(const uint64_t cost, uint64_t &total_times, uint64_t &min_cost, uint64_t &max_cost,
                                  uint64_t &total_cost) {
  total_times++;
  total_cost += cost;
  max_cost = (max_cost < cost) ? cost : max_cost;
  min_cost = (min_cost > cost) ? cost : min_cost;
}

void StatisticManager::UpdateCost(const uint64_t cost, std::atomic<uint64_t> &total_times,
                                  std::atomic<uint64_t> &min_cost, std::atomic<uint64_t> &max_cost,
                                  std::atomic<uint64_t> &total_cost) {
  (void)total_times.fetch_add(1U);
  (void)total_cost.fetch_add(cost);
  if (max_cost.load() < cost) {
    max_cost.store(cost);
  }
  if (min_cost.load() > cost) {
    min_cost.store(cost);
  }
}

void StatisticManager::AddExchangeMemCost(const uint64_t cost) {
  UpdateCost(cost, link_statistic_info_.exchange_mem_times, link_statistic_info_.exchange_mem_min_cost,
             link_statistic_info_.exchange_mem_max_cost, link_statistic_info_.exchange_mem_total_cost);
}

void StatisticManager::AddCommInitCost(const uint64_t cost) {
  UpdateCost(cost, link_statistic_info_.comm_init_times, link_statistic_info_.comm_init_min_cost,
             link_statistic_info_.comm_init_max_cost, link_statistic_info_.comm_init_total_cost);
}

void StatisticManager::AddCommDestroyCost(const uint64_t cost) {
  UpdateCost(cost, link_statistic_info_.comm_destroy_times, link_statistic_info_.comm_destroy_min_cost,
             link_statistic_info_.comm_destroy_max_cost, link_statistic_info_.comm_destroy_total_cost);
}

void StatisticManager::AddRegisterGlobalMemTimes() {
  link_statistic_info_.register_global_mem_times++;
}

void StatisticManager::AddDeregisterGlobalMemTimes() {
  link_statistic_info_.deregister_global_mem_times++;
}

void StatisticManager::AddCommBindMemTimes() {
  link_statistic_info_.comm_bind_mem_times++;
}

void StatisticManager::AddCommUnbindMemTimes() {
  link_statistic_info_.comm_unbind_mem_times++;
}

void StatisticManager::AddCommPrepareCost(const uint64_t cost) {
  UpdateCost(cost, link_statistic_info_.comm_prepare_times, link_statistic_info_.comm_prepare_min_cost,
             link_statistic_info_.comm_prepare_max_cost, link_statistic_info_.comm_prepare_total_cost);
}

void StatisticManager::AddBatchPutCost(const uint64_t cost) {
  send_statistic_info_.batch_put_times++;
  send_statistic_info_.batch_put_total_cost += cost;
}

MemoryStatisticInfo &StatisticManager::GetMemoryStatisticInfo() {
  return memory_statistic_info_;
}
FuncStatisticInfo &StatisticManager::GetFuncStatisticInfo() {
  return func_statistic_info_;
}

void StatisticManager::Dump() const{
  DumpMemoryProfilingTrack();
  DumpFuncProfilingTrack();
  DumpLinkProfilingTrack();
  DumpUnLinkProfilingTrack();
}

void StatisticManager::DumpMemoryProfilingTrack() const {
  LLMEVENT("Memory statistic info:alloc mem:%lu, free mem:%lu, alloc times:%lu, free times%:lu",
          memory_statistic_info_.alloc_mem, memory_statistic_info_.free_mem, memory_statistic_info_.alloc_times,
          memory_statistic_info_.free_times);
}

void StatisticManager::DumpFuncProfilingTrack() const {
  const uint64_t link_func_avg_cost =
      func_statistic_info_.link_func_times == 0U
          ? 0U
          : func_statistic_info_.link_func_total_cost / func_statistic_info_.link_func_times;
  const uint64_t unlink_func_avg_cost =
      func_statistic_info_.unlink_func_times == 0U
          ? 0U
          : func_statistic_info_.unlink_func_total_cost / func_statistic_info_.unlink_func_times;
  const uint64_t pull_func_avg_cost =
      func_statistic_info_.pull_func_times == 0U
          ? 0U
          : func_statistic_info_.pull_func_total_cost / func_statistic_info_.pull_func_times;
  const uint64_t copy_func_avg_cost =
      func_statistic_info_.copy_func_times == 0U
          ? 0U
          : func_statistic_info_.copy_func_total_cost / func_statistic_info_.copy_func_times;
  const uint64_t swap_func_avg_cost =
      func_statistic_info_.swap_func_times == 0U
          ? 0U
          : func_statistic_info_.swap_func_total_cost / func_statistic_info_.swap_func_times;

  const uint64_t transfer_func_avg_cost =
      func_statistic_info_.transfer_func_times == 0U
      ? 0U
      : func_statistic_info_.transfer_func_total_cost / func_statistic_info_.transfer_func_times;
  LLMEVENT(
      "Func statistic info:"
      "link info[times:%lu, max_cost:%lu us, min_cost:%lu us, avg_cost:%lu us], "
      "unlink info[times:%lu, max_cost:%lu us, min_cost:%lu us, avg_cost:%lu us], "
      "pull info[times:%lu, max_cost:%lu us, min_cost:%lu us, avg_cost:%lu us], "
      "copy info[times:%lu, max_cost:%lu us, min_cost:%lu us, avg_cost:%lu us], "
      "swap info[times:%lu, max_cost:%lu us, min_cost:%lu us, avg_cost:%lu us], "
      "transfer info[times:%lu, max_cost:%lu us, min_cost:%lu us, avg_cost:%lu us], ",
      func_statistic_info_.link_func_times.load(), func_statistic_info_.link_func_max_cost.load(),
      func_statistic_info_.link_func_min_cost.load(), link_func_avg_cost, func_statistic_info_.unlink_func_times.load(),
      func_statistic_info_.unlink_func_max_cost.load(), func_statistic_info_.unlink_func_min_cost.load(),
      unlink_func_avg_cost, func_statistic_info_.pull_func_times.load(), func_statistic_info_.pull_func_max_cost.load(),
      func_statistic_info_.pull_func_min_cost.load(), pull_func_avg_cost, func_statistic_info_.copy_func_times.load(),
      func_statistic_info_.copy_func_max_cost.load(), func_statistic_info_.copy_func_min_cost.load(),
      copy_func_avg_cost, func_statistic_info_.swap_func_times.load(), func_statistic_info_.swap_func_max_cost.load(),
      func_statistic_info_.swap_func_min_cost.load(), swap_func_avg_cost,
      func_statistic_info_.transfer_func_times.load(), func_statistic_info_.transfer_func_max_cost.load(),
      func_statistic_info_.transfer_func_min_cost.load(), transfer_func_avg_cost);
}

void StatisticManager::DumpLinkProfilingTrack() const {
  const uint64_t comm_init_avg_cost =
      link_statistic_info_.comm_init_times == 0U
          ? 0U
          : link_statistic_info_.comm_init_total_cost / link_statistic_info_.comm_init_times;
  const uint64_t comm_prepare_avg_cost =
      link_statistic_info_.comm_prepare_times == 0U
          ? 0U
          : link_statistic_info_.comm_prepare_total_cost / link_statistic_info_.comm_prepare_times;
  const uint64_t exchange_mem_avg_cost =
      link_statistic_info_.exchange_mem_times == 0U
          ? 0U
          : link_statistic_info_.exchange_mem_total_cost / link_statistic_info_.exchange_mem_times;
  LLMEVENT(
      "Link statistic info:"
      "init comm info[times:%lu, max_cost:%lu us, min_cost:%lu us, avg_cost:%lu us], "
      "prepare comm info[times:%lu, max_cost:%lu us, min_cost:%lu us, avg_cost:%lu us], "
      "register global mem info[times:%lu], comm bind mem info[times:%lu], ",
      "exchange mem info[times:%lu, max_cost:%lu us, min_cost:%lu us, avg_cost:%lu us]",
      link_statistic_info_.comm_init_times, link_statistic_info_.comm_init_max_cost,
      link_statistic_info_.comm_init_min_cost, comm_init_avg_cost,
      link_statistic_info_.comm_prepare_times, link_statistic_info_.comm_prepare_max_cost,
      link_statistic_info_.comm_prepare_min_cost, comm_prepare_avg_cost,
      link_statistic_info_.register_global_mem_times, link_statistic_info_.comm_bind_mem_times,
      link_statistic_info_.exchange_mem_times, link_statistic_info_.exchange_mem_max_cost,
      link_statistic_info_.exchange_mem_min_cost, exchange_mem_avg_cost
      );
}

void StatisticManager::DumpUnLinkProfilingTrack() const {
  const uint64_t comm_destroy_avg_cost =
      link_statistic_info_.comm_destroy_times == 0U
          ? 0U
          : link_statistic_info_.comm_destroy_total_cost / link_statistic_info_.comm_destroy_times;
  LLMEVENT(
      "Unlink statistic info:"
      "destroy comm info[times:%lu, max_cost:%lu us, min_cost:%lu us, avg_cost:%lu us], "
      "deregister global mem info[times:%lu], comm unbind mem info[times:%lu]",
      link_statistic_info_.comm_destroy_times,link_statistic_info_.comm_destroy_max_cost,
      link_statistic_info_.comm_destroy_min_cost, comm_destroy_avg_cost,
      link_statistic_info_.deregister_global_mem_times, link_statistic_info_.comm_unbind_mem_times
      );
}

void StatisticManager::Reset() {
  link_statistic_info_.Reset();
  send_statistic_info_.Reset();
  recv_statistic_info_.Reset();
  memory_statistic_info_.Reset();
  func_statistic_info_.Reset();
}
}  // namespace llm
