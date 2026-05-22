/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_KV_BENCHMARK_KV_TRANSFER_EXECUTOR_H
#define HIXL_KV_BENCHMARK_KV_TRANSFER_EXECUTOR_H

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "acl/acl.h"
#include "hixl/hixl.h"

namespace hixl_kv_benchmark {

constexpr std::uint32_t kDefaultTransferThreads = 8U;

struct RankMeta {
  std::uint32_t rank = 0U;
  std::string endpoint;
  std::uintptr_t pool_addr = 0U;
  std::uint64_t pool_size = 0U;
};

struct KeyTransferTask {
  std::uint64_t key_index = 0U;
  std::uint32_t segment_id = 0U;
  std::string endpoint;
  std::vector<hixl::TransferOpDesc> descs;
  bool is_self = false;
};

class KvTransferExecutor {
 public:
  KvTransferExecutor(hixl::Hixl *hixl, std::map<std::uint32_t, RankMeta> metas_by_rank, std::uint32_t self_rank,
                     std::uint32_t num_threads, std::int32_t timeout_ms, aclrtContext device_context,
                     const char *(*recent_errmsg)(), bool local_copy_for_self = true);
  ~KvTransferExecutor();
  KvTransferExecutor(const KvTransferExecutor &) = delete;
  KvTransferExecutor &operator=(const KvTransferExecutor &) = delete;

  void Transfer(hixl::TransferOp op, std::vector<KeyTransferTask> tasks, bool trace_transfer);

 private:
  void StartWorkers();
  void StopWorkers();
  void WaitWorkersReady();
  void WorkerLoop(std::uint32_t worker_id);
  bool AcquireWorkerTask(KeyTransferTask *task, hixl::TransferOp *op, bool *trace_transfer);
  void RunWorkerTask(std::uint32_t worker_id, const KeyTransferTask &task, hixl::TransferOp op, bool trace_transfer);
  void RecordErrorAndCancelPending();

  hixl::Hixl *hixl_ = nullptr;
  std::map<std::uint32_t, RankMeta> metas_by_rank_;
  std::uint32_t self_rank_ = 0U;
  bool local_copy_for_self_ = true;
  std::uint32_t worker_count_ = 0U;
  std::int32_t timeout_ms_ = 0;
  aclrtContext device_context_ = nullptr;
  const char *(*recent_errmsg_)() = nullptr;

  std::mutex mutex_;
  std::condition_variable work_cv_;
  std::condition_variable done_cv_;
  std::condition_variable ready_cv_;
  std::vector<std::thread> workers_;
  std::vector<KeyTransferTask> tasks_;
  hixl::TransferOp op_ = hixl::WRITE;
  std::size_t next_task_ = 0U;
  std::size_t remaining_tasks_ = 0U;
  std::uint32_t ready_workers_ = 0U;
  bool work_active_ = false;
  bool stop_ = false;
  bool trace_transfer_ = false;
  std::exception_ptr first_error_;
  std::exception_ptr startup_error_;
  std::mutex trace_mu_;
};

}  // namespace hixl_kv_benchmark

#endif  // HIXL_KV_BENCHMARK_KV_TRANSFER_EXECUTOR_H
