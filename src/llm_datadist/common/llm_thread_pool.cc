/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.

 * The code snippet comes from Huawei's open-source Mindspore project.
 * Copyright 2020 Huawei Technologies Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#include "llm_thread_pool.h"

namespace llm {
LLMThreadPool::LLMThreadPool(std::string thread_name_prefix, const uint32_t size)
    : thread_name_prefix_(std::move(thread_name_prefix)), is_stoped_(false) {
  idle_thrd_num_ = size < 1U ? 1U : size;

  for (uint32_t i = 0U; i < idle_thrd_num_; ++i) {
    pool_.emplace_back(&ThreadFunc, this, i);
  }
}

LLMThreadPool::~LLMThreadPool() {
  Destroy();
}

void LLMThreadPool::Destroy() {
  if (is_stoped_.load() == true) {
    return;
  }
  is_stoped_.store(true);
  {
    const std::unique_lock<std::mutex> lock{m_lock_};
    cond_var_.notify_all();
  }

  for (std::thread &thd : pool_) {
    if (thd.joinable()) {
      try {
        thd.join();
      } catch (...) {
        LLMLOGW("thread join exception");
      }
    }
  }
}

void LLMThreadPool::ThreadFunc(LLMThreadPool *const thread_pool, uint32_t thread_idx) {
  if (thread_pool == nullptr) {
    return;
  }
  if (!thread_pool->thread_name_prefix_.empty()) {
    auto thread_name = thread_pool->thread_name_prefix_ + std::to_string(thread_idx);
    int32_t set_ret = pthread_setname_np(pthread_self(), thread_name.c_str());
    LLMLOGD("set thread name to [%s], ret=%d", thread_name.c_str(), set_ret);
  }

  while (!thread_pool->is_stoped_) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock{thread_pool->m_lock_};
      thread_pool->cond_var_.wait(
          lock, [thread_pool]() -> bool { return thread_pool->is_stoped_.load() || (!thread_pool->tasks_.empty()); });
      if (thread_pool->is_stoped_ && thread_pool->tasks_.empty()) {
        return;
      }
      task = std::move(thread_pool->tasks_.front());
      thread_pool->tasks_.pop();
    }
    --thread_pool->idle_thrd_num_;
    task();
    ++thread_pool->idle_thrd_num_;
  }
}
}  // namespace llm
