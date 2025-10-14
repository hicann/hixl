/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.

 * The code snippet comes from Huawei's open-source Ascend project.
 * Copyright 2020 Huawei Technologies Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LLM_THREAD_POOL_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LLM_THREAD_POOL_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "llm_datadist/llm_error_codes.h"
#include "common/llm_log.h"
#include "mem_utils.h"

namespace llm {
using ThreadTask = std::function<void()>;

class LLMThreadPool {
 public:
  explicit LLMThreadPool(std::string thread_name_prefix, const uint32_t size = 4U);
  ~LLMThreadPool();
  void Destroy();

  template <class Func, class... Args>
  auto commit(Func &&func, Args &&... args) -> std::future<decltype(func(args...))> {
    LLMLOGD("commit run task enter.");
    using retType = decltype(func(args...));
    std::future<retType> fail_future;
    if (is_stoped_.load()) {
      LLMLOGE(ge::FAILED, "thread pool has been stopped.");
      return fail_future;
    }

    const auto bindFunc = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);
    const auto task = MakeShared<std::packaged_task<retType()>>(bindFunc);
    if (task == nullptr) {
      LLMLOGE(ge::FAILED, "Make shared failed.");
      return fail_future;
    }
    std::future<retType> future = task->get_future();
    {
      const std::lock_guard<std::mutex> lock{m_lock_};
      tasks_.emplace([task]() { (*task)(); });
    }
    cond_var_.notify_one();
    LLMLOGD("commit run task end");
    return future;
  }

  static void ThreadFunc(LLMThreadPool *const thread_pool, uint32_t thread_idx);

 private:
  std::string thread_name_prefix_;
  std::vector<std::thread> pool_;
  std::queue<ThreadTask> tasks_;
  std::mutex m_lock_;
  std::condition_variable cond_var_;
  std::atomic<bool> is_stoped_;
  std::atomic<uint32_t> idle_thrd_num_;
};
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LLM_THREAD_POOL_H_
