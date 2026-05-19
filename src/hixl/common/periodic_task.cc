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

  auto state = state_;
  std::lock_guard<std::mutex> lock(state->mutex);
  if (state->running.load(std::memory_order_acquire)) {
    return SUCCESS;
  }
  state->interval = interval;
  state->task = std::move(task);
  state->running.store(true, std::memory_order_release);
  worker_ = std::thread(&PeriodicTask::Run, state);
  return SUCCESS;
}

void PeriodicTask::Stop() {
  auto state = state_;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->running.load(std::memory_order_acquire)) {
      return;
    }
    state->running.store(false, std::memory_order_release);
    state->task = nullptr;
  }
  state->cv.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

bool PeriodicTask::IsRunning() const {
  return state_->running.load(std::memory_order_acquire);
}

void PeriodicTask::Run(std::shared_ptr<State> state) {
  while (state->running.load(std::memory_order_acquire)) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(state->mutex);
      if (state->cv.wait_for(lock, state->interval,
                             [state]() { return !state->running.load(std::memory_order_acquire); })) {
        break;
      }
      task = state->task;
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
