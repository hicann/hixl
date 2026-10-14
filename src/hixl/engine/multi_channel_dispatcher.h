/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_MULTI_CHANNEL_DISPATCHER_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_MULTI_CHANNEL_DISPATCHER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <vector>
#include "common/hixl_inner_types.h"
#include "common/thread_pool.h"
#include "cs/hixl_cs.h"
#include "engine/client_handler.h"
#include "hixl/hixl_types.h"

namespace hixl {

// 单链路多 worker 并发传输执行器：handle 生命周期、buffer 拆分、
// 线程池调度、完成状态收敛。
// 并发契约：本类无内部锁，调用方须串行化 Submit/CheckStatus/Reset/DestroyHandles
// （UB 路径由 complete/sync 锁保证，DIRECT 路径由 handler 的 mutex_ 保证）。
class MultiChannelDispatcher {
 public:
  MultiChannelDispatcher() = default;
  ~MultiChannelDispatcher() = default;

  Status CreateHandles(const HixlClientDesc &desc, const HixlClientConfig &config, uint32_t worker_num,
                       uint32_t split_batch_size);
  void DestroyHandles();

  void Reset();

  const std::vector<HixlClientHandle> &Handles() const {
    return handles_;
  }
  std::vector<HixlClientHandle> &Handles() {
    return handles_;
  }

  enum class SubmitMode { SYNC, ASYNC };

  Status Submit(const std::vector<TransferOpDesc> &descs, bool is_get, uint32_t timeout_ms, SubmitMode mode,
                TransferReq *req);
  Status CheckStatus(const TransferReq &req, TransferStatus &status);

  static Status CollectFutures(std::vector<std::future<Status>> &futures);
  static uint32_t ResolveActualWorkerNum(uint32_t configured_n, uint32_t desc_count,
                                         uint32_t split_batch_size = kDefaultSplitBatchSize);
  static void SplitDescs(const std::vector<TransferOpDesc> &descs, uint32_t worker_num,
                         std::vector<std::vector<HixlOneSideOpDesc>> &worker_descs);

 private:
  Status InitPool(uint32_t split_batch_size);

  using WorkerTask = std::function<Status()>;

  enum class WorkerState : uint8_t { PENDING = 0, COMPLETED = 1, FAILED = 2 };

  struct WorkerEntry {
    size_t client_idx{0U};
    CompleteHandle handle{nullptr};
    WorkerState state{WorkerState::PENDING};
  };

  struct WorkerResult {
    CompleteHandle handle{nullptr};
  };

  struct SubmitContext {
    const std::vector<TransferOpDesc> *descs{nullptr};
    uint32_t actual_n{1U};
    bool is_get{false};
    SubmitMode mode{SubmitMode::SYNC};
    uint32_t timeout_ms{0U};
  };

  static std::vector<HixlOneSideOpDesc> ConvertDescs(const std::vector<TransferOpDesc> &descs);
  static Status WorkerBatchSyncCall(bool is_get, HixlClientHandle handle, const std::vector<HixlOneSideOpDesc> &descs,
                                    uint32_t timeout_ms);
  static Status WorkerBatchAsyncCall(bool is_get, HixlClientHandle handle, const std::vector<HixlOneSideOpDesc> &descs,
                                     CompleteHandle &ch);
  std::future<Status> SubmitTask(WorkerTask task);
  Status SubmitSingleWorker(bool is_get, const std::vector<TransferOpDesc> &descs, uint32_t timeout_ms, SubmitMode mode,
                            TransferReq *req);
  Status SubmitWorkersToPool(const SubmitContext &ctx, std::vector<WorkerResult> &worker_results);
  Status CollectAsyncWorkers(Status submit_ret, std::vector<WorkerResult> &worker_results, TransferReq *req);
  Status RecordAsyncWorkers(TransferReq &req, std::vector<WorkerEntry> workers);
  void QueryWorkersStatus(std::vector<WorkerEntry> &workers, bool &any_waiting, bool &any_failed);

  std::vector<HixlClientHandle> handles_;
  uint32_t split_batch_size_{kDefaultSplitBatchSize};
  std::unique_ptr<ThreadPool> pool_;
  std::map<TransferReq, std::vector<WorkerEntry>> async_workers_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_MULTI_CHANNEL_DISPATCHER_H_
