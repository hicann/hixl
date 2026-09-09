/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_DIRECT_MULTI_CHANNEL_HANDLER_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_DIRECT_MULTI_CHANNEL_HANDLER_H_

#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "common/hixl_inner_types.h"
#include "common/thread_pool.h"
#include "cs/hixl_cs.h"
#include "engine/client_handler.h"
#include "engine/client_handler_factory.h"
#include "hixl/hixl_types.h"

namespace hixl {

class DirectMultiChannelHandler : public IClientHandler {
 public:
  static Status Create(const HandlerCreateArgs &args, std::unique_ptr<DirectMultiChannelHandler> &out);
  ~DirectMultiChannelHandler() override = default;

  Status Connect(uint32_t timeout_ms) override;
  Status RegisterMem(const MemHandleInfo &mem_info) override;
  Status TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, TransferReq &req) override;
  Status TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, uint32_t timeout_ms) override;
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status) override;
  Status Finalize() override;
  void Dump(const char *reason, DumpLogLevel level = DumpLogLevel::EVENT) const override;

  static uint32_t ResolveActualWorkerNum(uint32_t configured_n, uint32_t desc_count,
                                         uint32_t split_batch_size = kDefaultSplitBatchSize);
  static void SplitDescs(const std::vector<TransferOpDesc> &descs, uint32_t worker_num,
                         std::vector<std::vector<HixlOneSideOpDesc>> &worker_descs);

 public:
  explicit DirectMultiChannelHandler(std::vector<HixlClientHandle> handles, const std::string &local_engine = "",
                                     const std::string &remote_engine = "",
                                     const HandlerCreateArgs::EndpointPair &pair = HandlerCreateArgs::EndpointPair{});

 private:
  enum class SubmitMode { SYNC, ASYNC };
  using WorkerTask = std::function<Status()>;

  enum class WorkerState : uint8_t {
    PENDING = 0,
    COMPLETED = 1,
    FAILED = 2,
  };

  struct WorkerEntry {
    size_t clientIdx{0U};
    CompleteHandle handle{nullptr};
    WorkerState state{WorkerState::PENDING};
  };

  struct WorkerResult {
    Status ret{FAILED};
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
  Status Submit(const std::vector<TransferOpDesc> &descs, bool is_get, uint32_t timeout_ms, SubmitMode mode,
                TransferReq *req);
  Status SubmitSingleWorker(bool is_get, const std::vector<TransferOpDesc> &descs, uint32_t timeout_ms, SubmitMode mode,
                            TransferReq *req);
  Status SubmitWorkersToPool(const SubmitContext &ctx, std::vector<WorkerResult> &worker_results);
  Status CollectAsyncWorkers(Status submit_ret, std::vector<WorkerResult> &worker_results, TransferReq *req);
  void RecordAsyncWorkers(TransferReq &req, std::vector<WorkerEntry> workers);
  Status CheckStatus(const TransferReq &req, TransferStatus &status);
  void QueryWorkersStatus(std::vector<WorkerEntry> &workers, bool &any_waiting, bool &any_failed);
  static Status CollectFutures(std::vector<std::future<Status>> &futures);

 private:
  std::vector<HixlClientHandle> handles_;
  std::string local_engine_;
  std::string remote_engine_;
  HandlerCreateArgs::EndpointPair pair_{};
  uint32_t split_batch_size_{kDefaultSplitBatchSize};
  std::unique_ptr<ThreadPool> pool_;
  std::map<TransferReq, std::vector<WorkerEntry>> async_workers_;
  bool is_connected_{false};
  std::vector<std::pair<HixlClientHandle, MemHandle>> mem_handles_;
  mutable std::mutex mutex_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_DIRECT_MULTI_CHANNEL_HANDLER_H_
