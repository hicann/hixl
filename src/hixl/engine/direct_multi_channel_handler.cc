/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "engine/direct_multi_channel_handler.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "engine/client_handler_config_helper.h"
#include "engine/endpoint_generator/endpoint_generator.h"

namespace hixl {
namespace {

constexpr const char *kThreadPoolName = "dc_multi_worker";

void DestroyAllHandles(std::vector<HixlClientHandle> &handles) {
  for (auto &h : handles) {
    if (h != nullptr) {
      HixlCSClientDestroy(h);
    }
  }
  handles.clear();
}

}  // namespace

DirectMultiChannelHandler::DirectMultiChannelHandler(std::vector<HixlClientHandle> handles,
                                                     const std::string &local_engine, const std::string &remote_engine,
                                                     const HandlerCreateArgs::EndpointPair &pair)
    : handles_(std::move(handles)), local_engine_(local_engine), remote_engine_(remote_engine), pair_(pair) {}

Status DirectMultiChannelHandler::Create(const HandlerCreateArgs &args,
                                         std::unique_ptr<DirectMultiChannelHandler> &out) {
  const auto &pair = args.matched_pairs[0];
  EndpointDesc local_endpoint{};
  EndpointDesc remote_endpoint{};
  HIXL_CHK_STATUS_RET(EndpointGenerator::ConvertToEndpointDesc(pair.local, local_endpoint));
  HIXL_CHK_STATUS_RET(EndpointGenerator::ConvertToEndpointDesc(pair.remote, remote_endpoint));
  HixlClientDesc desc{};
  desc.server_ip = args.server_ip.c_str();
  desc.server_port = args.server_port;
  desc.local_endpoint = &local_endpoint;
  desc.remote_endpoint = &remote_endpoint;
  desc.tc = args.rdma_tc;
  desc.sl = args.rdma_sl;
  HixlClientConfig config{};
  const std::string global_resource_config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(args);
  if (!global_resource_config.empty()) {
    config.global_resource_config = global_resource_config.c_str();
  }
  auto handler = MakeUnique<DirectMultiChannelHandler>(std::vector<HixlClientHandle>{}, args.local_engine,
                                                       args.remote_engine, pair);
  HIXL_CHK_BOOL_RET_STATUS(handler != nullptr, FAILED,
                           "DirectMultiChannelHandler create failed: MakeUnique returned nullptr");
  auto cleanup_fn = [&handler]() {
    if (handler != nullptr) {
      DestroyAllHandles(handler->handles_);
    }
  };
  HIXL_DISMISSABLE_GUARD(cleanup, cleanup_fn);
  for (uint32_t i = 0U; i < args.multi_worker_num; ++i) {
    HixlClientHandle handle = nullptr;
    Status create_ret = HixlCSClientCreate(&desc, &config, &handle);
    HIXL_CHK_STATUS_RET(create_ret, "HixlCSClientCreate failed for type %s", CommTypeToString(pair.type));
    handler->handles_.push_back(handle);
  }
  handler->split_batch_size_ = args.multi_channel_split_batch_size;
  handler->pool_ =
      MakeUnique<ThreadPool>(kThreadPoolName, args.multi_worker_num, static_cast<uint32_t>(handler->handles_.size()));
  HIXL_CHK_BOOL_RET_STATUS(handler->pool_ != nullptr, FAILED,
                           "DirectMultiChannelHandler ThreadPool create failed, worker_count:%zu",
                           handler->handles_.size());
  HIXL_DISMISS_GUARD(cleanup);
  out = std::move(handler);
  return SUCCESS;
}

Status DirectMultiChannelHandler::Connect(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (is_connected_) {
    HIXL_LOGE(ALREADY_CONNECTED, "DirectMultiChannelHandler already connected");
    return ALREADY_CONNECTED;
  }
  for (HixlClientHandle handle : handles_) {
    Status ret = static_cast<Status>(HixlCSClientConnect(handle, timeout_ms));
    HIXL_CHK_STATUS_RET(ret, "DirectMultiChannelHandler connect failed, ret: %d", ret);
  }
  is_connected_ = true;
  return SUCCESS;
}

Status DirectMultiChannelHandler::RegisterMem(const MemHandleInfo &mem_info) {
  CommMem hccl_mem{};
  hccl_mem.type = (mem_info.type == MemType::MEM_DEVICE) ? COMM_MEM_TYPE_DEVICE : COMM_MEM_TYPE_HOST;
  hccl_mem.addr = reinterpret_cast<void *>(mem_info.mem.addr);
  hccl_mem.size = mem_info.mem.len;

  std::lock_guard<std::mutex> lock(mutex_);
  const size_t rollback_start = mem_handles_.size();
  auto rollback_fn = [this, rollback_start]() {
    while (mem_handles_.size() > rollback_start) {
      auto &[h, mh] = mem_handles_.back();
      HixlCSClientUnregMem(h, mh);
      mem_handles_.pop_back();
    }
  };
  HIXL_DISMISSABLE_GUARD(rollback, rollback_fn);
  for (HixlClientHandle handle : handles_) {
    MemHandle mem_handle = nullptr;
    Status ret = HixlCSClientRegMem(handle, nullptr, &hccl_mem, &mem_handle);
    HIXL_CHK_STATUS_RET(ret, "DirectMultiChannelHandler register memory failed, addr: 0x%lx", mem_info.mem.addr);
    mem_handles_.push_back({handle, mem_handle});
  }
  HIXL_DISMISS_GUARD(rollback);
  return SUCCESS;
}

Status DirectMultiChannelHandler::TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                                TransferReq &req) {
  std::lock_guard<std::mutex> lock(mutex_);
  return Submit(op_descs, (operation != WRITE), 0U, SubmitMode::ASYNC, &req);
}

Status DirectMultiChannelHandler::TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                               uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  return Submit(op_descs, (operation != WRITE), timeout_ms, SubmitMode::SYNC, nullptr);
}

Status DirectMultiChannelHandler::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  std::lock_guard<std::mutex> lock(mutex_);
  return CheckStatus(req, status);
}

Status DirectMultiChannelHandler::Finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  pool_.reset();
  async_workers_.clear();
  for (auto &[handle, mh] : mem_handles_) {
    if (mh != nullptr) {
      HixlCSClientUnregMem(handle, mh);
    }
  }
  mem_handles_.clear();
  DestroyAllHandles(handles_);
  is_connected_ = false;
  return SUCCESS;
}

void DirectMultiChannelHandler::Dump(const char *reason, DumpLogLevel level) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const char *comm_type = CommTypeToString(pair_.type);
  const std::string local_endpoint = pair_.local.ToString();
  const std::string remote_endpoint = pair_.remote.ToString();
  for (size_t i = 0U; i < handles_.size(); ++i) {
    if (level == DumpLogLevel::ERROR) {
      HIXL_LOGE(FAILED,
                "[DirectMultiChannelHandler] dump, reason:%s, local_engine:%s, remote_engine:%s, handle:%p, "
                "is_connected:%d, mem_handle_count:%zu, worker_count:%zu, comm_type:%s, "
                "local_endpoint:{%s}, remote_endpoint:{%s}",
                reason, local_engine_.c_str(), remote_engine_.c_str(), handles_[i], static_cast<int32_t>(is_connected_),
                mem_handles_.size(), handles_.size(), comm_type, local_endpoint.c_str(), remote_endpoint.c_str());
      continue;
    }
    HIXL_EVENT(
        "[DirectMultiChannelHandler] dump, reason:%s, local_engine:%s, remote_engine:%s, handle:%p, "
        "is_connected:%d, mem_handle_count:%zu, worker_count:%zu, comm_type:%s, "
        "local_endpoint:{%s}, remote_endpoint:{%s}",
        reason, local_engine_.c_str(), remote_engine_.c_str(), handles_[i], static_cast<int32_t>(is_connected_),
        mem_handles_.size(), handles_.size(), comm_type, local_endpoint.c_str(), remote_endpoint.c_str());
  }
}

uint32_t DirectMultiChannelHandler::ResolveActualWorkerNum(uint32_t configured_n, uint32_t desc_count,
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

void DirectMultiChannelHandler::SplitDescs(const std::vector<TransferOpDesc> &descs, uint32_t worker_num,
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

std::vector<HixlOneSideOpDesc> DirectMultiChannelHandler::ConvertDescs(const std::vector<TransferOpDesc> &descs) {
  std::vector<HixlOneSideOpDesc> hixl_descs(descs.size());
  for (size_t i = 0U; i < descs.size(); ++i) {
    hixl_descs[i].remote_buf = reinterpret_cast<void *>(descs[i].remote_addr);
    hixl_descs[i].local_buf = reinterpret_cast<void *>(descs[i].local_addr);
    hixl_descs[i].len = descs[i].len;
  }
  return hixl_descs;
}

Status DirectMultiChannelHandler::WorkerBatchSyncCall(bool is_get, HixlClientHandle handle,
                                                      const std::vector<HixlOneSideOpDesc> &descs,
                                                      uint32_t timeout_ms) {
  const uint32_t list_num = static_cast<uint32_t>(descs.size());
  if (is_get) {
    return static_cast<Status>(HixlCSClientBatchGetSync(handle, list_num, descs.data(), timeout_ms));
  }
  return static_cast<Status>(HixlCSClientBatchPutSync(handle, list_num, descs.data(), timeout_ms));
}

Status DirectMultiChannelHandler::WorkerBatchAsyncCall(bool is_get, HixlClientHandle handle,
                                                       const std::vector<HixlOneSideOpDesc> &descs,
                                                       CompleteHandle &ch) {
  const uint32_t list_num = static_cast<uint32_t>(descs.size());
  if (is_get) {
    return static_cast<Status>(HixlCSClientBatchGetAsync(handle, list_num, descs.data(), &ch));
  }
  return static_cast<Status>(HixlCSClientBatchPutAsync(handle, list_num, descs.data(), &ch));
}

std::future<Status> DirectMultiChannelHandler::SubmitTask(WorkerTask task) {
  if (pool_ == nullptr) {
    HIXL_LOGE(FAILED, "[DirectMultiChannelHandler] SubmitTask failed, thread pool is not initialized");
    return {};
  }
  return pool_->commit(std::move(task));
}

Status DirectMultiChannelHandler::Submit(const std::vector<TransferOpDesc> &descs, bool is_get, uint32_t timeout_ms,
                                         SubmitMode mode, TransferReq *req) {
  HIXL_CHK_BOOL_RET_STATUS((mode != SubmitMode::ASYNC) || (req != nullptr), PARAM_INVALID,
                           "[DirectMultiChannelHandler] Submit async requires non-null req");
  const uint32_t actual_n = ResolveActualWorkerNum(static_cast<uint32_t>(handles_.size()),
                                                   static_cast<uint32_t>(descs.size()), split_batch_size_);
  if (actual_n <= 1U) {
    return SubmitSingleWorker(is_get, descs, timeout_ms, mode, req);
  }
  HIXL_LOGD(
      "[DirectMultiChannelHandler] multi_worker submit, mode:%s, desc_count:%zu, workers:%u, "
      "split_batch_size:%u",
      (mode == SubmitMode::SYNC) ? "sync" : "async", descs.size(), actual_n, split_batch_size_);
  std::vector<WorkerResult> worker_results(actual_n);
  SubmitContext submit_ctx{&descs, actual_n, is_get, mode, timeout_ms};
  Status submit_ret = SubmitWorkersToPool(submit_ctx, worker_results);
  if (mode == SubmitMode::SYNC) {
    return submit_ret;
  }
  return CollectAsyncWorkers(submit_ret, worker_results, req);
}

Status DirectMultiChannelHandler::SubmitSingleWorker(bool is_get, const std::vector<TransferOpDesc> &descs,
                                                     uint32_t timeout_ms, SubmitMode mode, TransferReq *req) {
  if (mode == SubmitMode::SYNC) {
    return WorkerBatchSyncCall(is_get, handles_.front(), ConvertDescs(descs), timeout_ms);
  }
  CompleteHandle ch = nullptr;
  Status worker_ret = WorkerBatchAsyncCall(is_get, handles_.front(), ConvertDescs(descs), ch);
  HIXL_CHK_STATUS_RET(worker_ret, "[DirectMultiChannelHandler] single worker async submit failed");
  RecordAsyncWorkers(*req, {{0U, ch}});
  return SUCCESS;
}

Status DirectMultiChannelHandler::SubmitWorkersToPool(const SubmitContext &ctx,
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
          slot->ret = WorkerBatchAsyncCall(is_get, handle, descs_for_worker, slot->handle);
          HIXL_CHK_STATUS_RET(slot->ret, "[DirectMultiChannelHandler] worker %zu submit failed, desc_count:%zu",
                              worker_idx, descs_for_worker.size());
          return slot->ret;
        }));
  }
  return CollectFutures(futures);
}

Status DirectMultiChannelHandler::CollectAsyncWorkers(Status submit_ret, std::vector<WorkerResult> &worker_results,
                                                      TransferReq *req) {
  HIXL_CHK_STATUS_RET(submit_ret, "[DirectMultiChannelHandler] async submit failed, worker_count:%zu",
                      worker_results.size());
  std::vector<WorkerEntry> workers;
  workers.reserve(worker_results.size());
  for (size_t i = 0U; i < worker_results.size(); ++i) {
    workers.push_back({i, worker_results[i].handle});
  }
  RecordAsyncWorkers(*req, std::move(workers));
  return SUCCESS;
}

void DirectMultiChannelHandler::RecordAsyncWorkers(TransferReq &req, std::vector<WorkerEntry> workers) {
  for (const auto &entry : workers) {
    if (entry.handle != nullptr) {
      req = static_cast<TransferReq>(entry.handle);
      break;
    }
  }
  async_workers_[req] = std::move(workers);
}

Status DirectMultiChannelHandler::CheckStatus(const TransferReq &req, TransferStatus &status) {
  if (async_workers_.empty()) {
    status = TransferStatus::FAILED;
    HIXL_LOGE(FAILED, "[DirectMultiChannelHandler] CheckStatus failed, no transfer tasks in progress, req:%p", req);
    return FAILED;
  }
  auto it = async_workers_.find(req);
  if (it == async_workers_.end()) {
    status = TransferStatus::FAILED;
    HIXL_LOGE(PARAM_INVALID, "[DirectMultiChannelHandler] CheckStatus failed, invalid req:%p", req);
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

void DirectMultiChannelHandler::QueryWorkersStatus(std::vector<WorkerEntry> &workers, bool &any_waiting,
                                                   bool &any_failed) {
  for (auto &entry : workers) {
    if (entry.state == WorkerState::PENDING) {
      HixlCompleteStatus cs = HIXL_COMPLETE_STATUS_WAITING;
      Status ret = static_cast<Status>(HixlCSClientQueryCompleteStatus(handles_[entry.clientIdx], entry.handle, &cs));
      if (ret != SUCCESS) {
        entry.state = WorkerState::FAILED;
        HIXL_LOGE(ret, "[DirectMultiChannelHandler] CheckStatus worker query failed, clientIdx:%zu", entry.clientIdx);
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

Status DirectMultiChannelHandler::CollectFutures(std::vector<std::future<Status>> &futures) {
  Status first_error = SUCCESS;
  for (size_t i = 0U; i < futures.size(); ++i) {
    if (!futures[i].valid()) {
      HIXL_LOGE(FAILED, "[DirectMultiChannelHandler] ThreadPool commit returned invalid future, worker:%zu", i);
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

}  // namespace hixl
