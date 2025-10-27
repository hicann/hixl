/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_LLM_DATADIST_TIMER_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_LLM_DATADIST_TIMER_H_

#include <map>
#include <thread>
#include <mutex>
#include <functional>
#include "ge_common/ge_api_types.h"

namespace llm {
using TimerCallback = std::function<void(void)>;
struct TimerInfo {
  uint64_t next_time_stamp;
  uint32_t period;
  uint32_t timer_id;
  bool one_shot_flag;
  bool is_start;
  TimerCallback timer_callback;
};

class LlmDatadistTimer {
 public:
  static LlmDatadistTimer &Instance();
  ~LlmDatadistTimer() = default;
  void Init();
  void Finalize();
  void *CreateTimer(const TimerCallback &callback);
  ge::Status DeleteTimer(const void *handle);
  ge::Status StartTimer(void *handle, uint32_t period, bool one_shot);
  ge::Status StopTimer(void *handle);

 private:
  LlmDatadistTimer() = default;
  void TimerThreadLoop();
  void ProcessTimerInContext();

  std::mutex mutex_;
  std::thread time_thread_;
  bool running_{false};
  bool is_init_{false};
  std::map<uint32_t, std::shared_ptr<TimerInfo>> timer_infos_;
};
}  // namespace llm
#endif