/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "connect_pool_executor.h"
#include "common/hixl_checker.h"

namespace hixl {
namespace {
constexpr const int32_t kOptionConnectPoolThreadNum = 2;
constexpr const int32_t kOptionConnectPoolTaskQueueCapacity = 128;

constexpr const int32_t kLimitThreadNumMin = 1;
constexpr const int32_t kLimitThreadNumMax = 64;
constexpr const int32_t kLimitTaskQueueCapacityMin = 1;
constexpr const int32_t kLimitTaskQueueCapacityMax = 65535;
}  // namespace

ConnectPoolExecutor::ConnectPoolExecutor() {}

ConnectPoolExecutor::~ConnectPoolExecutor() {
  if (is_initialized_.load(std::memory_order::memory_order_relaxed)) {
    Shutdown();
  }
}

Status ConnectPoolExecutor::Initialize(const HixlOptions &options) {
  HIXL_LOGI("ConnectPoolExecutor initialize start");
  if (IsInitialized()) {
    HIXL_LOGW("ConnectPoolExecutor is already initialized");
    return SUCCESS;
  }
  thread_num_ = kOptionConnectPoolThreadNum;
  task_queue_capacity_ = kOptionConnectPoolTaskQueueCapacity;
  auto grc = options.GlobalResourceCfg();
  if (grc.has_value()) {
    thread_num_ = grc->connect_pool.thread_num.value_or(kOptionConnectPoolThreadNum);
    task_queue_capacity_ = grc->connect_pool.task_queue_capacity.value_or(kOptionConnectPoolTaskQueueCapacity);
  }
  HIXL_CHK_BOOL_RET_STATUS(thread_num_ >= kLimitThreadNumMin && thread_num_ <= kLimitThreadNumMax, PARAM_INVALID,
                           "thread_num:%d must in [%d, %d]", thread_num_, kLimitThreadNumMin, kLimitThreadNumMax);
  HIXL_CHK_BOOL_RET_STATUS(
      task_queue_capacity_ >= kLimitTaskQueueCapacityMin && task_queue_capacity_ <= kLimitTaskQueueCapacityMax,
      PARAM_INVALID, "task_queue_capacity:%d must in [%d, %d]", task_queue_capacity_, kLimitTaskQueueCapacityMin,
      kLimitTaskQueueCapacityMax);

  is_initialized_.store(true, std::memory_order::memory_order_relaxed);
  (void)aclrtGetCurrentContext(&ctx_);
  for (int32_t i = 0; i < thread_num_; ++i) {
    workers_.emplace_back([this, i]() { WorkerHandler(i); });
  }
  HIXL_LOGI("ConnectPoolExecutor initialize success");
  return SUCCESS;
}

void ConnectPoolExecutor::Shutdown() {
  HIXL_LOGI("ConnectPoolExecutor shutdown start");
  if (!is_initialized_.load(std::memory_order::memory_order_relaxed)) {
    HIXL_LOGW("ConnectPoolExecutor is already shutdown");
    return;
  }

  is_initialized_.store(false, std::memory_order::memory_order_relaxed);
  task_queue_cv_.notify_all();
  HIXL_LOGI("ConnectPoolExecutor wait worker exit");
  for (auto &worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  workers_.clear();
  HIXL_LOGI("ConnectPoolExecutor shutdown success");
}

Status ConnectPoolExecutor::Submit(const std::function<void()> &task, const AscendString &remote_engine,
                                   const bool is_connect) {
  std::unique_lock<std::mutex> lock(task_queue_mutex_);
  if (task_list_.size() >= static_cast<std::size_t>(task_queue_capacity_)) {
    HIXL_LOGW("task_queue is full, task_queue_capacity:%d", task_queue_capacity_);
    return RESOURCE_EXHAUSTED;
  }

  // 新任务插入队尾
  SetStatus(remote_engine, is_connect ? AsyncConnectStatus::CONNECT_PENDING : AsyncConnectStatus::DISCONNECT_PENDING);
  task_list_.emplace_back(ConnectPoolExecutorTask{is_connect, remote_engine, task});
  HIXL_LOGI("submit task %s to %s", is_connect ? "connect" : "disconnect", remote_engine.GetString());

  task_queue_cv_.notify_one();
  return SUCCESS;
}

void ConnectPoolExecutor::SetStatus(const AscendString &remote_engine, const AsyncConnectStatus status) {
  std::unique_lock<std::mutex> lock(task_result_mutex_);
  // 用于保证状态与用户侧最新操作一致
  if (status == AsyncConnectStatus::CONNECTING || status == AsyncConnectStatus::CONNECTED ||
      status == AsyncConnectStatus::CONNECT_FAILED) {
    const auto &it = task_result_.find(remote_engine);
    if (it != task_result_.end() && it->second == AsyncConnectStatus::DISCONNECT_PENDING) {
      return;
    }
  }
  if (status == AsyncConnectStatus::DISCONNECTING || status == AsyncConnectStatus::NOT_CONNECT) {
    const auto &it = task_result_.find(remote_engine);
    if (it != task_result_.end() && it->second == AsyncConnectStatus::CONNECT_PENDING) {
      return;
    }
  }

  if (status == AsyncConnectStatus::NOT_CONNECT) {
    task_result_.erase(remote_engine);
  } else {
    task_result_[remote_engine] = status;
  }
}

Status ConnectPoolExecutor::GetStatus(const AscendString &remote_engine, AsyncConnectStatus &status) {
  std::unique_lock<std::mutex> lock(task_result_mutex_);
  const auto &it = task_result_.find(remote_engine);
  status = (it == task_result_.end()) ? AsyncConnectStatus::NOT_CONNECT : it->second;
  return SUCCESS;
}

Status ConnectPoolExecutor::GetStatus(std::map<AscendString, AsyncConnectStatus> &statuses) {
  statuses.clear();
  std::unique_lock<std::mutex> lock(task_result_mutex_);
  std::copy(task_result_.begin(), task_result_.end(), std::inserter(statuses, statuses.begin()));
  return SUCCESS;
}

bool ConnectPoolExecutor::IsInitialized() const {
  return is_initialized_.load(std::memory_order::memory_order_relaxed);
}

void ConnectPoolExecutor::WorkerHandler(const int32_t worker_id) {
  HIXL_LOGI("ConnectPoolExecutor worker %d start", worker_id);
  HIXL_CHK_ACL(aclrtSetCurrentContext(ctx_));
  auto cv_wait_func = [this]() {
    if (!is_initialized_.load(std::memory_order::memory_order_relaxed)) {
      return true;
    }
    // 有可执行的任务才醒
    for (const auto &t : task_list_) {
      if (task_doing_set_.count(t.remote_engine) == 0) {
        return true;
      }
    }
    return false;
  };

  while (true) {
    ConnectPoolExecutorTask executor_task{false, "", nullptr};
    {
      std::unique_lock<std::mutex> lock(task_queue_mutex_);
      task_queue_cv_.wait(lock, cv_wait_func);

      if (!is_initialized_.load(std::memory_order::memory_order_relaxed)) {
        HIXL_LOGW("ConnectPoolExecutor is shutdown, %llu task remain", task_list_.size());
        break;
      }

      if (task_list_.empty()) {
        continue;
      }

      for (auto it = task_list_.begin(); it != task_list_.end(); ++it) {
        // 保证一个remote_engine同一时刻只做一个任务
        if (task_doing_set_.count(it->remote_engine) == 0) {
          task_doing_set_.emplace(it->remote_engine);
          SetStatus(it->remote_engine,
                    it->is_connect ? AsyncConnectStatus::CONNECTING : AsyncConnectStatus::DISCONNECTING);
          executor_task = std::move(*it);
          task_list_.erase(it);
          break;
        }
      }
    }

    if (executor_task.task != nullptr) {
      HIXL_LOGI("worker %d exec task %s to %s start", worker_id, executor_task.is_connect ? "connect" : "disconnect",
                executor_task.remote_engine.GetString());
      executor_task.task();
      HIXL_LOGI("worker %d exec task %s to %s success", worker_id, executor_task.is_connect ? "connect" : "disconnect",
                executor_task.remote_engine.GetString());

      std::lock_guard<std::mutex> lock(task_queue_mutex_);
      task_doing_set_.erase(executor_task.remote_engine);
      task_queue_cv_.notify_one();
    }
  }
  HIXL_LOGI("ConnectPoolExecutor worker %d exit", worker_id);
}
}  // namespace hixl
