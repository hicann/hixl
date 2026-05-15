/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "common/periodic_task.h"

#include <exception>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"

namespace hixl {
PeriodicTask::~PeriodicTask() {
  Stop();
}

Status PeriodicTask::Start(std::chrono::milliseconds interval, std::function<void()> task) {
  HIXL_CHK_BOOL_RET_STATUS(interval.count() > 0, PARAM_INVALID, "Periodic task interval must be positive.");
  HIXL_CHK_BOOL_RET_STATUS(static_cast<bool>(task), PARAM_INVALID, "Periodic task callback is empty.");

  std::lock_guard<std::mutex> lock(mutex_);
  if (running_.load(std::memory_order_acquire)) {
    return SUCCESS;
  }
  interval_ = interval;
  task_ = std::move(task);
  running_.store(true, std::memory_order_release);
  worker_ = std::thread(&PeriodicTask::Run, this);
  return SUCCESS;
}

void PeriodicTask::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_.load(std::memory_order_acquire)) {
      return;
    }
    running_.store(false, std::memory_order_release);
    task_ = nullptr;
  }
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

bool PeriodicTask::IsRunning() const {
  return running_.load(std::memory_order_acquire);
}

void PeriodicTask::Run() {
  while (running_.load(std::memory_order_acquire)) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (cv_.wait_for(lock, interval_, [this]() { return !running_.load(std::memory_order_acquire); })) {
        break;
      }
      task = task_;
    }
    if (!task) {
      continue;
    }
    try {
      task();
    } catch (const std::exception &e) {
      HIXL_LOGE(FAILED, "Periodic task callback threw exception:%s", e.what());
    } catch (...) {
      HIXL_LOGE(FAILED, "Periodic task callback threw unknown exception.");
    }
  }
}
}  // namespace hixl
