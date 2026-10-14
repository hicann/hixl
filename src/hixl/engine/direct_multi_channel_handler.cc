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
#include <algorithm>
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "engine/client_handler_config_helper.h"

namespace hixl {

DirectMultiChannelHandler::DirectMultiChannelHandler(const std::string &local_engine, const std::string &remote_engine,
                                                     const HandlerCreateArgs::EndpointPair &pair)
    : local_engine_(local_engine), remote_engine_(remote_engine), pair_(pair) {}

Status DirectMultiChannelHandler::Create(const HandlerCreateArgs &args,
                                         std::unique_ptr<DirectMultiChannelHandler> &out) {
  const auto &pair = args.matched_pairs[0];
  EndpointDesc local_endpoint{};
  EndpointDesc remote_endpoint{};
  HixlClientDesc desc{};
  HIXL_CHK_STATUS_RET(ClientHandlerConfigHelper::FillClientDesc(args, pair, local_endpoint, remote_endpoint, desc),
                      "DirectMultiChannelHandler fill client desc failed");
  HixlClientConfig config{};
  const std::string global_resource_config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(args);
  if (!global_resource_config.empty()) {
    config.global_resource_config = global_resource_config.c_str();
  }
  auto handler = MakeUnique<DirectMultiChannelHandler>(args.local_engine, args.remote_engine, pair);
  HIXL_CHK_BOOL_RET_STATUS(handler != nullptr, FAILED,
                           "DirectMultiChannelHandler create failed: MakeUnique returned nullptr");
  HIXL_CHK_STATUS_RET(
      handler->dispatcher_.CreateHandles(desc, config, args.MultiWorkerNum(), args.MultiChannelSplitBatchSize()),
      "DirectMultiChannelHandler CreateHandles failed for type %s", CommTypeToString(pair.type));
  out = std::move(handler);
  return SUCCESS;
}

Status DirectMultiChannelHandler::Connect(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (is_connected_) {
    HIXL_LOGE(ALREADY_CONNECTED, "DirectMultiChannelHandler already connected");
    return ALREADY_CONNECTED;
  }
  for (HixlClientHandle handle : dispatcher_.Handles()) {
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
  if (handle_to_mem_handles_.find(mem_info.mem_handle) != handle_to_mem_handles_.end()) {
    return SUCCESS;
  }
  const size_t rollback_start = mem_handles_.size();
  auto rollback_fn = [this, rollback_start]() {
    while (mem_handles_.size() > rollback_start) {
      auto &[h, mh] = mem_handles_.back();
      HixlCSClientUnregMem(h, mh);
      mem_handles_.pop_back();
    }
  };
  HIXL_DISMISSABLE_GUARD(rollback, rollback_fn);
  for (HixlClientHandle handle : dispatcher_.Handles()) {
    MemHandle mem_handle = nullptr;
    Status ret = HixlCSClientRegMem(handle, nullptr, &hccl_mem, &mem_handle);
    HIXL_CHK_STATUS_RET(ret, "DirectMultiChannelHandler register memory failed, addr: 0x%lx", mem_info.mem.addr);
    mem_handles_.push_back({handle, mem_handle});
  }
  handle_to_mem_handles_[mem_info.mem_handle] = std::vector<std::pair<HixlClientHandle, MemHandle>>(
      mem_handles_.begin() + static_cast<std::ptrdiff_t>(rollback_start), mem_handles_.end());
  HIXL_DISMISS_GUARD(rollback);
  return SUCCESS;
}

Status DirectMultiChannelHandler::UnregOneMemHandle(HixlClientHandle handle, MemHandle mh, MemHandle mem_handle) const {
  HIXL_CHK_STATUS_RET(HixlCSClientUnregMem(handle, mh),
                      "Call api:HixlCSClientUnregMem failed, mem_handle:%p, client_handle:%p, cs_mem_handle:%p",
                      mem_handle, handle, mh);
  return SUCCESS;
}

void DirectMultiChannelHandler::EraseMemHandle(HixlClientHandle handle, MemHandle mh) {
  auto mh_it = std::find(mem_handles_.begin(), mem_handles_.end(), std::make_pair(handle, mh));
  if (mh_it != mem_handles_.end()) {
    mem_handles_.erase(mh_it);
  }
}

Status DirectMultiChannelHandler::DeregisterMem(MemHandle mem_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = handle_to_mem_handles_.find(mem_handle);
  if (it == handle_to_mem_handles_.end()) {
    return SUCCESS;
  }
  Status first_err = SUCCESS;
  std::vector<std::pair<HixlClientHandle, MemHandle>> remaining;
  for (const auto &entry : it->second) {
    const Status ret = UnregOneMemHandle(entry.first, entry.second, mem_handle);
    if (ret != SUCCESS) {
      if (first_err == SUCCESS) {
        first_err = ret;
      }
      remaining.push_back(entry);
      continue;
    }
    EraseMemHandle(entry.first, entry.second);
  }
  if (first_err != SUCCESS) {
    it->second = std::move(remaining);
  }
  HIXL_CHK_BOOL_RET_STATUS(
      first_err == SUCCESS, first_err,
      "[DirectMultiChannelHandler] DeregisterMem partial failure, mem_handle:%p, remaining_channels:%zu", mem_handle,
      it->second.size());
  handle_to_mem_handles_.erase(it);
  return SUCCESS;
}

Status DirectMultiChannelHandler::TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                                TransferReq &req) {
  std::lock_guard<std::mutex> lock(mutex_);
  return dispatcher_.Submit(op_descs, (operation != WRITE), 0U, MultiChannelDispatcher::SubmitMode::ASYNC, &req);
}

Status DirectMultiChannelHandler::TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                               uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  return dispatcher_.Submit(op_descs, (operation != WRITE), timeout_ms, MultiChannelDispatcher::SubmitMode::SYNC,
                            nullptr);
}

Status DirectMultiChannelHandler::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  std::lock_guard<std::mutex> lock(mutex_);
  return dispatcher_.CheckStatus(req, status);
}

Status DirectMultiChannelHandler::Finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  dispatcher_.Reset();
  for (auto &[handle, mh] : mem_handles_) {
    if (mh != nullptr) {
      HixlCSClientUnregMem(handle, mh);
    }
  }
  mem_handles_.clear();
  handle_to_mem_handles_.clear();
  dispatcher_.DestroyHandles();
  is_connected_ = false;
  return SUCCESS;
}

void DirectMultiChannelHandler::Dump(const char *reason, DumpLogLevel level) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const char *comm_type = CommTypeToString(pair_.type);
  const std::string local_endpoint = pair_.local.ToString();
  const std::string remote_endpoint = pair_.remote.ToString();
  for (size_t i = 0U; i < dispatcher_.Handles().size(); ++i) {
    if (level == DumpLogLevel::ERROR) {
      HIXL_LOGE(FAILED,
                "[DirectMultiChannelHandler] dump, reason:%s, local_engine:%s, remote_engine:%s, handle:%p, "
                "is_connected:%d, mem_handle_count:%zu, worker_count:%zu, comm_type:%s, "
                "local_endpoint:{%s}, remote_endpoint:{%s}",
                reason, local_engine_.c_str(), remote_engine_.c_str(), dispatcher_.Handles()[i],
                static_cast<int32_t>(is_connected_), mem_handles_.size(), dispatcher_.Handles().size(), comm_type,
                local_endpoint.c_str(), remote_endpoint.c_str());
      continue;
    }
    HIXL_EVENT(
        "[DirectMultiChannelHandler] dump, reason:%s, local_engine:%s, remote_engine:%s, handle:%p, "
        "is_connected:%d, mem_handle_count:%zu, worker_count:%zu, comm_type:%s, "
        "local_endpoint:{%s}, remote_endpoint:{%s}",
        reason, local_engine_.c_str(), remote_engine_.c_str(), dispatcher_.Handles()[i],
        static_cast<int32_t>(is_connected_), mem_handles_.size(), dispatcher_.Handles().size(), comm_type,
        local_endpoint.c_str(), remote_endpoint.c_str());
  }
}

}  // namespace hixl
