/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "llm_datadist_timer.h"
#include <chrono>
#include "common/def_types.h"
#include "common/llm_checker.h"
#include "common/mem_utils.h"
#include "common/llm_log.h"
#include "common/llm_log.h"

namespace llm {
namespace {
constexpr uint64_t kMilliToMicro = 1000U;
constexpr uint64_t kTimerWaitInterval = 2U;

uint64_t GetCurrentTimeStamp() {
  const auto current_time = std::chrono::steady_clock::now();
  auto duration = current_time.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

}  // namespace
LlmDatadistTimer &LlmDatadistTimer::Instance() {
  static LlmDatadistTimer instance;
  return instance;
}

void LlmDatadistTimer::Init() {
  if (is_init_) {
    return;
  }
  running_ = true;
  time_thread_ = std::thread(&LlmDatadistTimer::TimerThreadLoop, this);
  is_init_ = true;
}

void LlmDatadistTimer::Finalize() {
  running_ = false;
  if (time_thread_.joinable()) {
    time_thread_.join();
  }
  is_init_ = false;
}

void *LlmDatadistTimer::CreateTimer(const TimerCallback &callback) {
  static uint32_t timer_cnt = 0U;
  LLM_ASSERT_NOTNULL(callback, "timer callback is nullptr");
  if (timer_cnt == UINT32_MAX) {
    LLMLOGE(ge::LLM_PARAM_INVALID, "create timer reaches num limit[%u]", UINT32_MAX);
  }
  std::shared_ptr<TimerInfo> timer = MakeShared<TimerInfo>();
  LLM_ASSERT_NOTNULL(timer, "failed to create timer");
  timer->timer_id = timer_cnt;
  timer->timer_callback = callback;
  std::unique_lock<std::mutex> lk(mutex_);
  timer_infos_[timer_cnt] = timer;
  ++timer_cnt;

  LLMLOGI("CreateTimer success, timer_id:%u", timer->timer_id);
  return PtrToPtr<TimerInfo, void>(timer.get());
}

ge::Status LlmDatadistTimer::DeleteTimer(const void *handle) {
  auto timer = PtrToPtr<void, TimerInfo>(handle);
  LLM_ASSERT_NOTNULL(timer, "timer handle is nullptr");
  std::unique_lock<std::mutex> lk(mutex_);
  const auto &iter = timer_infos_.find(timer->timer_id);
  LLM_CHK_BOOL_RET_STATUS(iter != timer_infos_.cend(), ge::LLM_PARAM_INVALID,
                         "not find timer info, delete timer[%u] failed", timer->timer_id);
  LLMLOGI("DeleteTimer success, timer_id:%u", timer->timer_id);
  (void)timer_infos_.erase(iter);
  return ge::SUCCESS;
}

ge::Status LlmDatadistTimer::StartTimer(void *handle, uint32_t period, bool one_shot) {
  LLMLOGI("Start timer, period:%u, one shot:%u", period, one_shot);
  auto timer = PtrToPtr<void, TimerInfo>(handle);
  LLM_ASSERT_NOTNULL(timer, "timer handle is nullptr");
  std::unique_lock<std::mutex> lk(mutex_);
  const auto &iter = timer_infos_.find(timer->timer_id);
  LLM_CHK_BOOL_RET_STATUS(iter != timer_infos_.cend(), ge::LLM_PARAM_INVALID,
                         "not find timer info, start timer[%u] failed", timer->timer_id);
  timer->period = period;
  timer->one_shot_flag = one_shot;
  timer->is_start = true;
  timer->next_time_stamp = (GetCurrentTimeStamp() + static_cast<uint64_t>(period) * kMilliToMicro);
  return ge::SUCCESS;
}

ge::Status LlmDatadistTimer::StopTimer(void *handle) {
  auto timer = PtrToPtr<void, TimerInfo>(handle);
  LLM_ASSERT_NOTNULL(timer, "timer handle is nullptr");
  std::unique_lock<std::mutex> lk(mutex_);
  const auto &iter = timer_infos_.find(timer->timer_id);
  LLM_CHK_BOOL_RET_STATUS(iter != timer_infos_.cend(), ge::LLM_PARAM_INVALID,
                         "not find timer info, stop timer[%u] failed", timer->timer_id);
  timer->is_start = false;
  return ge::SUCCESS;
}

void LlmDatadistTimer::ProcessTimerInContext() {
  std::vector<TimerCallback> inner_callback_list;
  {
    std::unique_lock<std::mutex> lk(mutex_);
    const uint64_t current_time = GetCurrentTimeStamp();
    for (const auto &iter : timer_infos_) {
      auto timer_info = iter.second;
      if ((!timer_info->is_start) || (current_time < timer_info->next_time_stamp)) {
        continue;
      }
      inner_callback_list.emplace_back(iter.second->timer_callback);
      if (timer_info->one_shot_flag) {
        timer_info->is_start = false;
      } else {
        timer_info->next_time_stamp = (current_time + timer_info->period * kMilliToMicro);
      }
    }
  }
  for (const auto &callback : inner_callback_list) {
    callback();
  }
}

void LlmDatadistTimer::TimerThreadLoop() {
  (void) pthread_setname_np(pthread_self(), "ge_llm_stats");
  while (running_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kTimerWaitInterval));
    ProcessTimerInContext();
  }
  LLMLOGI("Timer loop thread exit");
}
}  // namespace llm