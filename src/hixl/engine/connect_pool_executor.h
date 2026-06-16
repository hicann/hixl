/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_CONNECT_POOL_EXECUTOR_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_CONNECT_POOL_EXECUTOR_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <vector>
#include <thread>
#include "acl/acl.h"
#include "hixl/hixl_types.h"
#include "hixl_options.h"

namespace hixl {
struct ConnectPoolExecutorTask {
  bool is_connect;
  AscendString remote_engine;
  std::function<void()> task;
};

class ConnectPoolExecutor {
 public:
  ConnectPoolExecutor();
  ~ConnectPoolExecutor();

  Status Initialize(const HixlOptions &options);

  void Shutdown();

  Status Submit(const std::function<void()> &task, const AscendString &remote_engine, const bool is_connect);

  void SetStatus(const AscendString &remote_engine, const AsyncConnectStatus status);

  Status GetStatus(const AscendString &remote_engine, AsyncConnectStatus &status);

  Status GetStatus(std::map<AscendString, AsyncConnectStatus> &statuses);

 private:
  bool IsInitialized() const;

  void WorkerHandler(const int32_t worker_id);

  int32_t thread_num_{0};
  int32_t task_queue_capacity_{0};
  std::atomic<bool> is_initialized_{false};
  std::vector<std::thread> workers_;

  std::mutex task_queue_mutex_;
  std::condition_variable task_queue_cv_;
  std::list<ConnectPoolExecutorTask> task_list_;
  std::set<AscendString> task_doing_set_;

  std::mutex task_result_mutex_;
  std::map<AscendString, AsyncConnectStatus> task_result_;

  aclrtContext ctx_ = nullptr;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_CONNECT_POOL_EXECUTOR_H_
