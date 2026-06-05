/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_TESTS_CPP_COMMON_ASYNC_TRANSFER_TEST_HELPERS_H
#define HIXL_TESTS_CPP_COMMON_ASYNC_TRANSFER_TEST_HELPERS_H

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace hixl {
namespace test_helpers {

template <typename Engine, typename Status>
inline bool PollTransferUntilDone(Engine &engine, TransferReq req, int poll_interval_ms,
                                  const std::atomic<bool> &stop) {
  Status status = Status::WAITING;
  while (!stop.load() && status == Status::WAITING) {
    engine.GetTransferStatus(req, status);
    if (status == Status::COMPLETED) {
      return true;
    }
    if (status == Status::FAILED) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
  }
  return false;
}

template <typename Engine, typename Status>
inline void WaitForAllAsyncTransfers(Engine &engine, TransferReq *req_list, int req_count, int poll_interval_ms = 10,
                                     int max_wait_sec = 5) {
  std::vector<std::thread> poll_threads;
  std::atomic<int> completed{0};
  std::atomic<bool> stop{false};
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(max_wait_sec);
  for (int i = 0; i < req_count; ++i) {
    poll_threads.emplace_back([&, i]() {
      if (PollTransferUntilDone<Engine, Status>(engine, req_list[i], poll_interval_ms, stop)) {
        completed.fetch_add(1);
      }
    });
  }
  while (std::chrono::steady_clock::now() < deadline && completed.load() < req_count) {
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
  }
  stop = true;
  for (auto &t : poll_threads) {
    if (t.joinable()) {
      t.join();
    }
  }
  EXPECT_EQ(completed.load(), req_count);
}

}  // namespace test_helpers
}  // namespace hixl

#endif  // HIXL_TESTS_CPP_COMMON_ASYNC_TRANSFER_TEST_HELPERS_H
