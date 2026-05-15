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
  void Run();

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::function<void()> task_;
  std::chrono::milliseconds interval_{0};
  std::thread worker_;
  std::atomic<bool> running_{false};
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_PERIODIC_TASK_H_
