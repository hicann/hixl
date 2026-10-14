/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "engine/multi_channel_dispatcher.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"

namespace hixl {
namespace {
constexpr const char *kThreadPoolName = "mc_worker";
}  // namespace

Status MultiChannelDispatcher::CreateHandles(const HixlClientDesc &desc, const HixlClientConfig &config,
                                             uint32_t worker_num, uint32_t split_batch_size) {
  DestroyHandles();  // 防御：若复用实例，先销毁旧 handle 再重建
  handles_.reserve(worker_num);
  auto cleanup_fn = [this]() { DestroyHandles(); };
  HIXL_DISMISSABLE_GUARD(cleanup, cleanup_fn);
  for (uint32_t i = 0U; i < worker_num; ++i) {
    HixlClientHandle handle = nullptr;
    HIXL_CHK_STATUS_RET(HixlCSClientCreate(&desc, &config, &handle),
                        "MultiChannelDispatcher HixlCSClientCreate failed");
    handles_.push_back(handle);
  }
  HIXL_CHK_STATUS_RET(InitPool(split_batch_size), "MultiChannelDispatcher InitPool failed");
  HIXL_DISMISS_GUARD(cleanup);
  return SUCCESS;
}

void MultiChannelDispatcher::DestroyHandles() {
  for (auto &h : handles_) {
    if (h != nullptr) {
      HixlCSClientDestroy(h);
    }
  }
  handles_.clear();
}

Status MultiChannelDispatcher::InitPool(uint32_t split_batch_size) {
  split_batch_size_ = split_batch_size;
  const uint32_t worker_count = static_cast<uint32_t>(handles_.size());
  pool_ = MakeUnique<ThreadPool>(kThreadPoolName, worker_count, worker_count);
  HIXL_CHK_BOOL_RET_STATUS(pool_ != nullptr, FAILED, "MultiChannelDispatcher ThreadPool create failed, worker_count:%u",
                           worker_count);
  return SUCCESS;
}

void MultiChannelDispatcher::Reset() {
  pool_.reset();
  async_workers_.clear();
}

Status MultiChannelDispatcher::Submit(const std::vector<TransferOpDesc> &descs, bool is_get, uint32_t timeout_ms,
                                      SubmitMode mode, TransferReq *req) {
  HIXL_CHK_BOOL_RET_STATUS((mode != SubmitMode::ASYNC) || (req != nullptr), PARAM_INVALID,
                           "MultiChannelDispatcher Submit async requires non-null req");
  const uint32_t actual_n = ResolveActualWorkerNum(static_cast<uint32_t>(handles_.size()),
                                                   static_cast<uint32_t>(descs.size()), split_batch_size_);
  if (actual_n <= 1U) {
    return SubmitSingleWorker(is_get, descs, timeout_ms, mode, req);
  }
  HIXL_LOGD("MultiChannelDispatcher multi_worker submit, mode:%s, desc_count:%zu, workers:%u, split_batch_size:%u",
            (mode == SubmitMode::SYNC) ? "sync" : "async", descs.size(), actual_n, split_batch_size_);
  std::vector<WorkerResult> worker_results(actual_n);
  SubmitContext submit_ctx{&descs, actual_n, is_get, mode, timeout_ms};
  Status submit_ret = SubmitWorkersToPool(submit_ctx, worker_results);
  if (mode == SubmitMode::SYNC) {
    return submit_ret;
  }
  return CollectAsyncWorkers(submit_ret, worker_results, req);
}

Status MultiChannelDispatcher::SubmitSingleWorker(bool is_get, const std::vector<TransferOpDesc> &descs,
                                                  uint32_t timeout_ms, SubmitMode mode, TransferReq *req) {
  // 契约兜底：句柄已被销毁/未创建时干净报错，而非 front() 未定义行为
  HIXL_CHK_BOOL_RET_STATUS(!handles_.empty(), FAILED,
                           "MultiChannelDispatcher no worker handles available, submit rejected");
  if (mode == SubmitMode::SYNC) {
    return WorkerBatchSyncCall(is_get, handles_.front(), ConvertDescs(descs), timeout_ms);
  }
  CompleteHandle ch = nullptr;
  Status worker_ret = WorkerBatchAsyncCall(is_get, handles_.front(), ConvertDescs(descs), ch);
  HIXL_CHK_STATUS_RET(worker_ret, "MultiChannelDispatcher single worker async submit failed");
  HIXL_CHK_STATUS_RET(RecordAsyncWorkers(*req, {{0U, ch}}), "MultiChannelDispatcher record single worker failed");
  return SUCCESS;
}

Status MultiChannelDispatcher::SubmitWorkersToPool(const SubmitContext &ctx,
                                                   std::vector<WorkerResult> &worker_results) {
  std::vector<std::vector<HixlOneSideOpDesc>> worker_descs;
  SplitDescs(*ctx.descs, ctx.actual_n, worker_descs);
  std::vector<std::future<Status>> futures;
  for (size_t i = 0U; i < worker_descs.size(); ++i) {
    HixlClientHandle handle = handles_[i];
    if (ctx.mode == SubmitMode::SYNC) {
      futures.emplace_back(SubmitTask(
          [handle, is_get = ctx.is_get, descs_for_worker = std::move(worker_descs[i]), timeout_ms = ctx.timeout_ms]() {
            return WorkerBatchSyncCall(is_get, handle, descs_for_worker, timeout_ms);
          }));
      continue;
    }
    WorkerResult *slot = &worker_results[i];
    futures.emplace_back(SubmitTask(
        [slot, worker_idx = i, handle, is_get = ctx.is_get, descs_for_worker = std::move(worker_descs[i])]() {
          const Status ret = WorkerBatchAsyncCall(is_get, handle, descs_for_worker, slot->handle);
          HIXL_CHK_STATUS_RET(ret, "MultiChannelDispatcher worker %zu submit failed, desc_count:%zu", worker_idx,
                              descs_for_worker.size());
          return ret;
        }));
  }
  return CollectFutures(futures);
}

Status MultiChannelDispatcher::CollectAsyncWorkers(Status submit_ret, std::vector<WorkerResult> &worker_results,
                                                   TransferReq *req) {
  // 提交失败后在途传输由调用方负责（既有内核约定），此处仅透传错误
  HIXL_CHK_STATUS_RET(submit_ret, "MultiChannelDispatcher async submit failed, worker_count:%zu",
                      worker_results.size());
  std::vector<WorkerEntry> workers;
  workers.reserve(worker_results.size());
  for (size_t i = 0U; i < worker_results.size(); ++i) {
    workers.push_back({i, worker_results[i].handle});
  }
  HIXL_CHK_STATUS_RET(RecordAsyncWorkers(*req, std::move(workers)), "MultiChannelDispatcher record workers failed");
  return SUCCESS;
}

Status MultiChannelDispatcher::RecordAsyncWorkers(TransferReq &req, std::vector<WorkerEntry> workers) {
  bool has_nonempty = false;
  for (const auto &entry : workers) {
    if (entry.handle != nullptr) {
      has_nonempty = true;
      req = static_cast<TransferReq>(entry.handle);
      break;
    }
  }
  HIXL_CHK_BOOL_RET_STATUS(has_nonempty, PARAM_INVALID,
                           "MultiChannelDispatcher all worker complete handles are null, reject recording");
  async_workers_[req] = std::move(workers);
  return SUCCESS;
}

Status MultiChannelDispatcher::CheckStatus(const TransferReq &req, TransferStatus &status) {
  if (async_workers_.empty()) {
    status = TransferStatus::FAILED;
    HIXL_LOGE(FAILED, "MultiChannelDispatcher CheckStatus failed, no transfer tasks in progress, req:%p", req);
    return FAILED;
  }
  auto it = async_workers_.find(req);
  if (it == async_workers_.end()) {
    status = TransferStatus::FAILED;
    HIXL_LOGE(PARAM_INVALID, "MultiChannelDispatcher CheckStatus failed, invalid req:%p", req);
    return PARAM_INVALID;
  }
  bool any_waiting = false;
  bool any_failed = false;
  QueryWorkersStatus(it->second, any_waiting, any_failed);
  if (any_failed) {
    status = TransferStatus::FAILED;
    async_workers_.erase(it);
    return SUCCESS;
  }
  if (any_waiting) {
    status = TransferStatus::WAITING;
    return SUCCESS;
  }
  status = TransferStatus::COMPLETED;
  async_workers_.erase(it);
  return SUCCESS;
}

void MultiChannelDispatcher::QueryWorkersStatus(std::vector<WorkerEntry> &workers, bool &any_waiting,
                                                bool &any_failed) {
  for (auto &entry : workers) {
    if (entry.state == WorkerState::PENDING) {
      HixlCompleteStatus cs = HIXL_COMPLETE_STATUS_WAITING;
      Status ret = static_cast<Status>(HixlCSClientQueryCompleteStatus(handles_[entry.client_idx], entry.handle, &cs));
      if (ret != SUCCESS) {
        entry.state = WorkerState::FAILED;
        HIXL_LOGE(ret, "MultiChannelDispatcher CheckStatus worker query failed, client_idx:%zu", entry.client_idx);
      } else {
        const TransferStatus ts = ToTransferStatus(cs);
        if (ts == TransferStatus::WAITING) {
          any_waiting = true;
        } else {
          entry.state = (ts == TransferStatus::COMPLETED) ? WorkerState::COMPLETED : WorkerState::FAILED;
        }
      }
    }
    any_failed = any_failed || (entry.state == WorkerState::FAILED);
  }
}

std::future<Status> MultiChannelDispatcher::SubmitTask(WorkerTask task) {
  if (pool_ == nullptr) {
    HIXL_LOGE(FAILED, "MultiChannelDispatcher SubmitTask failed, thread pool is not initialized");
    return {};
  }
  return pool_->commit(std::move(task));
}

Status MultiChannelDispatcher::CollectFutures(std::vector<std::future<Status>> &futures) {
  Status first_error = SUCCESS;
  for (size_t i = 0U; i < futures.size(); ++i) {
    if (!futures[i].valid()) {
      HIXL_LOGE(FAILED, "MultiChannelDispatcher ThreadPool commit returned invalid future, worker:%zu", i);
      if (first_error == SUCCESS) {
        first_error = FAILED;
      }
      continue;
    }
    const Status worker_ret = futures[i].get();
    if (worker_ret != SUCCESS && first_error == SUCCESS) {
      first_error = worker_ret;
    }
  }
  return first_error;
}

uint32_t MultiChannelDispatcher::ResolveActualWorkerNum(uint32_t configured_n, uint32_t desc_count,
                                                        uint32_t split_batch_size) {
  if (configured_n <= 1U || split_batch_size == 0U) {
    return 1U;
  }
  if (desc_count <= split_batch_size) {
    return 1U;
  }
  const uint64_t threshold = static_cast<uint64_t>(split_batch_size) * (configured_n - 1U);
  if (static_cast<uint64_t>(desc_count) > threshold) {
    return configured_n;
  }
  return desc_count / split_batch_size;
}

void MultiChannelDispatcher::SplitDescs(const std::vector<TransferOpDesc> &descs, uint32_t worker_num,
                                        std::vector<std::vector<HixlOneSideOpDesc>> &worker_descs) {
  worker_descs.clear();
  worker_descs.resize(worker_num);
  if (worker_num == 0U || descs.empty()) {
    return;
  }
  const uint32_t desc_num = static_cast<uint32_t>(descs.size());
  const uint32_t base = desc_num / worker_num;
  const uint32_t remainder = desc_num % worker_num;
  uint32_t idx = 0U;
  for (uint32_t i = 0U; i < worker_num; ++i) {
    const uint32_t count = base + ((i < remainder) ? 1U : 0U);
    worker_descs[i].reserve(count);
    for (uint32_t j = 0U; j < count; ++j) {
      worker_descs[i].push_back({reinterpret_cast<void *>(descs[idx].remote_addr),
                                 reinterpret_cast<void *>(descs[idx].local_addr), descs[idx].len});
      ++idx;
    }
  }
}

std::vector<HixlOneSideOpDesc> MultiChannelDispatcher::ConvertDescs(const std::vector<TransferOpDesc> &descs) {
  std::vector<HixlOneSideOpDesc> hixl_descs(descs.size());
  for (size_t i = 0U; i < descs.size(); ++i) {
    hixl_descs[i].remote_buf = reinterpret_cast<void *>(descs[i].remote_addr);
    hixl_descs[i].local_buf = reinterpret_cast<void *>(descs[i].local_addr);
    hixl_descs[i].len = descs[i].len;
  }
  return hixl_descs;
}

Status MultiChannelDispatcher::WorkerBatchSyncCall(bool is_get, HixlClientHandle handle,
                                                   const std::vector<HixlOneSideOpDesc> &descs, uint32_t timeout_ms) {
  const uint32_t list_num = static_cast<uint32_t>(descs.size());
  if (is_get) {
    return static_cast<Status>(HixlCSClientBatchGetSync(handle, list_num, descs.data(), timeout_ms));
  }
  return static_cast<Status>(HixlCSClientBatchPutSync(handle, list_num, descs.data(), timeout_ms));
}

Status MultiChannelDispatcher::WorkerBatchAsyncCall(bool is_get, HixlClientHandle handle,
                                                    const std::vector<HixlOneSideOpDesc> &descs, CompleteHandle &ch) {
  const uint32_t list_num = static_cast<uint32_t>(descs.size());
  if (is_get) {
    return static_cast<Status>(HixlCSClientBatchGetAsync(handle, list_num, descs.data(), &ch));
  }
  return static_cast<Status>(HixlCSClientBatchPutAsync(handle, list_num, descs.data(), &ch));
}

}  // namespace hixl
