/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_PERIODIC_TASK_H_
#define CANN_HIXL_SRC_HIXL_COMMON_PERIODIC_TASK_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "hixl/hixl_types.h"

namespace hixl {
class PeriodicTask {
 public:
  PeriodicTask() = default;
  ~PeriodicTask();
  PeriodicTask(const PeriodicTask &) = delete;
  PeriodicTask &operator=(const PeriodicTask &) = delete;
  PeriodicTask(PeriodicTask &&) = delete;
  PeriodicTask &operator=(PeriodicTask &&) = delete;

  Status Start(std::chrono::milliseconds interval, std::function<void()> task);
  void Stop();
  bool IsRunning() const;

 private:
  struct State {
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::function<void()> task;
    std::chrono::milliseconds interval{0};
    std::atomic<bool> running{false};
  };

  static void Run(std::shared_ptr<State> state);

  std::shared_ptr<State> state_{std::make_shared<State>()};
  std::thread worker_;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_PERIODIC_TASK_H_
