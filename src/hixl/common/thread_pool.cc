/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "thread_pool.h"

#include <chrono>
#include <system_error>

namespace hixl {
namespace {
constexpr int64_t kScaleRetryCooldownNs = 100LL * 1000LL * 1000LL;  // 100 ms

int64_t SteadyNowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace

ThreadPool::ThreadPool(std::string thread_name_prefix, const uint32_t min_size, const uint32_t max_size)
    : thread_name_prefix_(std::move(thread_name_prefix)),
      is_stopped_(false),
      min_thrd_num_(min_size < 1U ? 1U : min_size),
      max_thrd_num_(max_size < min_thrd_num_ ? min_thrd_num_ : max_size) {
  idle_thrd_num_ = min_thrd_num_;
  total_thrd_num_ = min_thrd_num_;
  busy_thrd_num_ = 0U;

  for (uint32_t i = 0U; i < min_thrd_num_; ++i) {
    pool_.emplace_back(&ThreadFunc, this, i, false, nullptr);
  }
  HIXL_LOGI("[ThreadPool] created, name:%s, min_size:%u, max_size:%u, core_threads:%u", thread_name_prefix_.c_str(),
            min_thrd_num_, max_thrd_num_, min_thrd_num_);
}

ThreadPool::~ThreadPool() {
  Destroy();
}

void ThreadPool::Destroy() {
  if (is_stopped_.load() == true) {
    return;
  }
  size_t pending_tasks = 0U;
  std::list<TempThreadEntry> temp_threads_to_join;
  {
    const std::unique_lock<std::mutex> lock{m_lock_};
    pending_tasks = tasks_.size();
    is_stopped_.store(true);
    cond_var_.notify_all();
    temp_threads_to_join.swap(temp_threads_);
  }
  HIXL_LOGI("[ThreadPool] destroying, name:%s, total_threads:%u, pending_tasks:%zu", thread_name_prefix_.c_str(),
            total_thrd_num_.load(), pending_tasks);

  for (std::thread &thd : pool_) {
    if (thd.joinable()) {
      try {
        thd.join();
      } catch (...) {
        HIXL_LOGW("[ThreadPool:%s] thread join exception", thread_name_prefix_.c_str());
      }
    }
  }
  for (auto &entry : temp_threads_to_join) {
    if (entry.thread.joinable()) {
      try {
        entry.thread.join();
      } catch (...) {
        HIXL_LOGW("[ThreadPool:%s] temp thread join exception", thread_name_prefix_.c_str());
      }
    }
  }
  HIXL_LOGI("[ThreadPool] destroyed, name:%s", thread_name_prefix_.c_str());
}

void ThreadPool::AddTemporaryThread() {
  const std::lock_guard<std::mutex> lock{m_lock_};
  if (is_stopped_.load()) {
    return;
  }
  CleanupFinishedTempThreads();
  if (tasks_.empty()) {
    return;
  }
  const uint32_t total = total_thrd_num_.load();
  const uint32_t busy = busy_thrd_num_.load();
  if (total >= max_thrd_num_ || (total > busy && static_cast<size_t>(total - busy) >= tasks_.size())) {
    HIXL_LOGD("[ThreadPool:%s] skip add temp thread, total:%u, busy:%u, max:%u, tasks:%zu", thread_name_prefix_.c_str(),
              total, busy, max_thrd_num_, tasks_.size());
    return;
  }
  const int64_t now_ns = SteadyNowNs();
  if (now_ns < scale_backoff_until_ns_) {
    return;
  }
  const uint32_t thread_idx = total;
  temp_threads_.emplace_back();
  auto &entry = temp_threads_.back();
  try {
    entry.thread = std::thread(&ThreadFunc, this, thread_idx, true, &entry.finished);
  } catch (const std::system_error &e) {
    temp_threads_.pop_back();
    scale_backoff_until_ns_ = now_ns + kScaleRetryCooldownNs;
    HIXL_LOGW(
        "[ThreadPool:%s] Call api:std::thread failed, fallback to existing threads, idx:%u, total:%u, max:%u, "
        "err_code:%d, err_msg:%s",
        thread_name_prefix_.c_str(), thread_idx, total_thrd_num_.load(), max_thrd_num_, e.code().value(), e.what());
    return;
  }
  ++total_thrd_num_;
  HIXL_LOGI("[ThreadPool:%s] add temp thread, idx:%u, idle:%u, busy:%u, total:%u, max:%u", thread_name_prefix_.c_str(),
            thread_idx, idle_thrd_num_.load(), busy_thrd_num_.load(), total_thrd_num_.load(), max_thrd_num_);
}

void ThreadPool::SetThreadName(const std::string &thread_type, uint32_t thread_idx) const {
  if (thread_name_prefix_.empty()) {
    return;
  }
  auto name = thread_name_prefix_ + "_" + thread_type + "_" + std::to_string(thread_idx);
  HIXL_LOGD("set thread name to [%s], ret=%d", name.c_str(), pthread_setname_np(pthread_self(), name.c_str()));
}

void ThreadPool::LogTempThreadExit(const char *reason, uint32_t thread_idx) const {
  HIXL_LOGI("[ThreadPool:%s] temp thread %s, idx:%u, idle:%u, busy:%u, total:%u", thread_name_prefix_.c_str(), reason,
            thread_idx, idle_thrd_num_.load(), busy_thrd_num_.load(), total_thrd_num_.load());
}

bool ThreadPool::PopTask(std::function<void()> &task, bool is_temporary, bool completed_task) {
  std::unique_lock<std::mutex> lock{m_lock_};
  if (completed_task) {
    --busy_thrd_num_;
    if (!is_temporary) {
      ++idle_thrd_num_;
    }
  }
  // Check stop before starting another iteration, as the original worker loop did.
  if (is_stopped_.load() || (is_temporary && tasks_.empty())) {
    if (is_temporary) {
      // Publish retirement atomically with respect to commit's enqueue and scaling decision.
      --total_thrd_num_;
    }
    return false;
  }
  if (!is_temporary) {
    cond_var_.wait(lock, [this]() { return is_stopped_.load() || !tasks_.empty(); });
  }
  // A core worker already waiting when stop arrives may execute one queued task, then exits next iteration.
  if (tasks_.empty()) {
    return false;
  }
  task = std::move(tasks_.front());
  tasks_.pop();
  if (!is_temporary) {
    --idle_thrd_num_;
  }
  ++busy_thrd_num_;
  return true;
}

void ThreadPool::CleanupFinishedTempThreads() {
  auto it = temp_threads_.begin();
  while (it != temp_threads_.end()) {
    if (it->finished.load()) {
      if (it->thread.joinable()) {
        it->thread.join();
      }
      it = temp_threads_.erase(it);
    } else {
      ++it;
    }
  }
}

void ThreadPool::ThreadFunc(ThreadPool *const thread_pool, uint32_t thread_idx, bool is_temporary,
                            std::atomic<bool> *finished_flag) {
  if (thread_pool == nullptr) {
    return;
  }
  const std::string thread_type = is_temporary ? "temp" : "core";
  thread_pool->SetThreadName(thread_type, thread_idx);
  HIXL_LOGD("[ThreadPool:%s] thread started, type:%s, idx:%u", thread_pool->thread_name_prefix_.c_str(),
            thread_type.c_str(), thread_idx);

  bool completed_task = false;
  while (true) {
    std::function<void()> task;
    if (!thread_pool->PopTask(task, is_temporary, completed_task)) {
      break;
    }
    task();
    completed_task = true;
  }
  if (is_temporary) {
    thread_pool->LogTempThreadExit(thread_pool->is_stopped_.load() ? "exit on stop" : "exit on empty queue",
                                   thread_idx);
  }
  if (finished_flag != nullptr) {
    finished_flag->store(true);
  }
}
}  // namespace hixl
