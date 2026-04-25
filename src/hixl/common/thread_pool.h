/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_THREAD_POOL_H_
#define CANN_HIXL_SRC_HIXL_COMMON_THREAD_POOL_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

#include "hixl/hixl_types.h"
#include "cs/hixl_cs.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"

namespace hixl {
using ThreadTask = std::function<void()>;

class ThreadPool {
 public:
  explicit ThreadPool(std::string thread_name_prefix, const uint32_t min_size = 4U, const uint32_t max_size = 4U);
  ~ThreadPool();
  void Destroy();

  template <class Func, class... Args>
  auto commit(Func &&func, Args &&... args) -> std::future<decltype(func(args...))> {
    HIXL_LOGD("[ThreadPool:%s] commit task enter", thread_name_prefix_.c_str());
    using retType = decltype(func(args...));
    std::future<retType> fail_future;
    if (is_stopped_.load()) {
      HIXL_LOGE(ge::FAILED, "[ThreadPool:%s] has been stopped", thread_name_prefix_.c_str());
      return fail_future;
    }

    const auto bindFunc = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);
    const auto task = MakeShared<std::packaged_task<retType()>>(bindFunc);
    if (task == nullptr) {
      HIXL_LOGE(ge::FAILED, "[ThreadPool:%s] make shared failed", thread_name_prefix_.c_str());
      return fail_future;
    }
    std::future<retType> future = task->get_future();
    size_t task_queue_size = 0U;
    {
      const std::lock_guard<std::mutex> lock{m_lock_};
      tasks_.emplace([task]() { (*task)(); });
      task_queue_size = tasks_.size();
    }

    uint32_t idle_num = idle_thrd_num_.load();
    uint32_t busy_num = busy_thrd_num_.load();
    uint32_t total_num = total_thrd_num_.load();
    if (idle_num == 0U && task_queue_size > 0U && total_num < max_thrd_num_) {
      HIXL_LOGI("[ThreadPool:%s] scaling up, idle:%u, busy:%u, total:%u, max:%u, tasks:%zu",
                thread_name_prefix_.c_str(), idle_num, busy_num, total_num, max_thrd_num_, task_queue_size);
      AddTemporaryThread();
    }

    cond_var_.notify_one();
    HIXL_LOGD("[ThreadPool:%s] commit task end, idle:%u, busy:%u, total:%u, tasks:%zu", thread_name_prefix_.c_str(),
              idle_num, busy_num, total_num, task_queue_size);
    return future;
  }

  static void ThreadFunc(ThreadPool *const thread_pool, uint32_t thread_idx, bool is_temporary);

 private:
  void AddTemporaryThread();
  void SetThreadName(const std::string &thread_type, uint32_t thread_idx);
  void LogTempThreadExit(const char *reason, uint32_t thread_idx);
  bool PopTask(std::function<void()> &task);

  std::string thread_name_prefix_;
  std::vector<std::thread> pool_;
  std::queue<ThreadTask> tasks_;
  std::mutex m_lock_;
  std::condition_variable cond_var_;
  std::atomic<bool> is_stopped_;
  std::atomic<uint32_t> idle_thrd_num_;
  std::atomic<uint32_t> busy_thrd_num_;
  std::atomic<uint32_t> total_thrd_num_;
  uint32_t min_thrd_num_;
  uint32_t max_thrd_num_;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_THREAD_POOL_H_