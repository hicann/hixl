/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#include "gtest/gtest.h"
#include "llm_datadist_timer.h"

namespace {
constexpr int32_t kCreateTimerThreads = 8;
constexpr int32_t kTimersPerThread = 32;
constexpr int32_t kTimerLoopWaitMs = 10;

void CreateTimersOnThread(llm::LlmDatadistTimer &timer, std::mutex &mu, std::set<uint32_t> &ids,
                          std::vector<void *> &handles, std::atomic<int32_t> &null_handles) {
  for (int32_t i = 0; i < kTimersPerThread; ++i) {
    void *handle = timer.CreateTimer([]() {});
    if (handle == nullptr) {
      null_handles.fetch_add(1);
      continue;
    }
    auto *info = static_cast<llm::TimerInfo *>(handle);
    std::lock_guard<std::mutex> lock(mu);
    ids.insert(info->timer_id);
    handles.push_back(handle);
  }
}
}  // namespace

TEST(LlmDatadistTimerUt, ConcurrentCreateTimerAssignsUniqueIds) {
  auto &timer = llm::LlmDatadistTimer::Instance();
  std::mutex mu;
  std::set<uint32_t> ids;
  std::vector<void *> handles;
  std::atomic<int32_t> null_handles{0};
  std::vector<std::thread> threads;
  threads.reserve(static_cast<size_t>(kCreateTimerThreads));
  for (int32_t t = 0; t < kCreateTimerThreads; ++t) {
    threads.emplace_back([&]() { CreateTimersOnThread(timer, mu, ids, handles, null_handles); });
  }
  for (auto &th : threads) {
    th.join();
  }
  EXPECT_EQ(null_handles.load(), 0);
  EXPECT_EQ(ids.size(), static_cast<size_t>(kCreateTimerThreads * kTimersPerThread));
  for (void *handle : handles) {
    EXPECT_EQ(timer.DeleteTimer(handle), ge::SUCCESS);
  }
}

TEST(LlmDatadistTimerUt, InitFinalizeStopsTimerThread) {
  auto &timer = llm::LlmDatadistTimer::Instance();
  timer.Init();
  std::this_thread::sleep_for(std::chrono::milliseconds(kTimerLoopWaitMs));
  timer.Finalize();
  timer.Finalize();
}
