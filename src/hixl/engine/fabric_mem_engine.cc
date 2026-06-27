/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem_engine.h"

#include <string>
#include <unordered_set>
#include <utility>

#include "acl/acl_rt.h"
#include "common/ctrl_msg.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "fabric_mem/virtual_memory_manager.h"
#include "profiling/prof_api_reg.h"

namespace hixl {

const std::unordered_set<std::string> FabricMemEngine::kSupportedOptions = {
    OPTION_ENABLE_USE_FABRIC_MEM, OPTION_AUTO_CONNECT, OPTION_GLOBAL_RESOURCE_CONFIG};

void FabricMemEngine::SetKeepaliveCheckIntervalMs(int64_t interval_ms) {
  FabricMemTransferService::SetKeepaliveCheckIntervalMs(interval_ms);
}

bool FabricMemEngine::IsInitialized() const {
  return is_initialized_.load(std::memory_order_relaxed);
}

Status FabricMemEngine::CheckInitialized() const {
  HIXL_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order_relaxed), FAILED,
                           "[FabricMemEngine] Engine is not initialized.");
  return SUCCESS;
}

Status FabricMemEngine::ApplyVirtualMemoryConfig() {
  if (fabric_mem_config_.has_capacity_tb) {
    HIXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().SetVirtualMemoryCapacity(fabric_mem_config_.capacity_tb),
                        "[FabricMemEngine] Failed to set fabric memory capacity.");
  }
  if (fabric_mem_config_.has_start_address_tb) {
    HIXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().SetGlobalStartAddress(fabric_mem_config_.start_address_tb),
                        "[FabricMemEngine] Failed to set fabric memory start address.");
  }
  return SUCCESS;
}

Status FabricMemEngine::StartControlServer() {
  fabric_mem_control_server_ = MakeUnique<FabricMemControlServer>();
  FabricMemLocalMemory *local_memory = &local_memory_;
  auto provider = [local_memory](std::vector<ShareHandleInfo> &share_handles) -> Status {
    share_handles = local_memory->GetShareHandles();
    return SUCCESS;
  };
  HIXL_CHK_STATUS_RET(fabric_mem_control_server_->Start(local_engine_, provider),
                      "[FabricMemEngine] Failed to start fabric mem control server.");
  return SUCCESS;
}

Status FabricMemEngine::InitTransferService() {
  fabric_mem_transfer_service_ = std::make_shared<FabricMemTransferService>();
  FabricMemTransferServiceInitParam param;
  param.device_id = device_id_;
  param.max_stream_num = fabric_mem_config_.max_stream_num;
  param.task_stream_num = fabric_mem_config_.task_stream_num;
  param.local_engine = local_engine_;
  param.auto_connect = auto_connect_;
  param.statistic = &fabric_mem_statistic_;
  param.local_memory = &local_memory_;
  param.control_server = fabric_mem_control_server_.get();
  param.aclrt_context = aclrt_context_;
  HIXL_CHK_STATUS_RET(fabric_mem_transfer_service_->Initialize(param),
                      "[FabricMemEngine] Failed to initialize fabric mem transfer service.");
  return SUCCESS;
}

Status FabricMemEngine::InitFabricMem() {
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this]() { CleanupFabricMemLocked(); }));
  HIXL_CHK_STATUS_RET(ApplyVirtualMemoryConfig(), "[FabricMemEngine] Failed to apply virtual memory config.");
  HIXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().Initialize(),
                      "[FabricMemEngine] Failed to initialize fabric virtual memory manager.");
  HIXL_CHK_STATUS_RET(fabric_mem_statistic_.StartPeriodicDump(),
                      "[FabricMemEngine] Failed to start fabric mem statistic dump.");
  HIXL_CHK_STATUS_RET(StartControlServer(), "[FabricMemEngine] Failed to start control server.");
  HIXL_CHK_STATUS_RET(InitTransferService(), "[FabricMemEngine] Failed to initialize transfer service.");
  HIXL_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

Status FabricMemEngine::InitializeLocked(const HixlOptions &options, bool &start_keepalive_monitor) {
  for (const auto &key : options.RawOptions()) {
    if (kSupportedOptions.find(key.first.GetString()) == kSupportedOptions.end()) {
      HIXL_LOGW("[FabricMemEngine] Unsupported option '%s' will be ignored", key.first.GetString());
    }
  }
  HIXL_CHK_BOOL_RET_STATUS(options.EnableFabricMem().value_or(false), PARAM_INVALID,
                           "[FabricMemEngine] EnableUseFabricMem must be 1.");
  HIXL_CHK_ACL_RET(aclrtGetDevice(&device_id_), "[FabricMemEngine] Failed to get device id.");
  TemporaryRtContext with_context(nullptr);
  HIXL_CHK_ACL_RET(aclrtCreateContext(&aclrt_context_, device_id_),
                   "[FabricMemEngine] Failed to create aclrt context.");
  aclrt_context_holder_ = std::shared_ptr<void>(aclrt_context_, [](void *ctx) {
    if (ctx != nullptr) {
      (void)aclrtDestroyContext(static_cast<aclrtContext>(ctx));
    }
  });
  HIXL_EVENT("[FabricMemEngine] Created aclrt ctx:%p, device:%d.", aclrt_context_, device_id_);
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this]() {
                           if (aclrt_context_ != nullptr) {
                             aclrt_context_holder_.reset();
                             aclrt_context_ = nullptr;
                           }
                         }));
  TemporaryRtContext engine_context(aclrt_context_);

  fabric_mem_config_.enabled = true;
  auto grc = options.GlobalResourceCfg();
  if (grc.has_value()) {
    if (grc->fabric_memory.max_capacity.has_value()) {
      fabric_mem_config_.capacity_tb = *grc->fabric_memory.max_capacity;
      fabric_mem_config_.has_capacity_tb = true;
    }
    if (grc->fabric_memory.start_address.has_value()) {
      fabric_mem_config_.start_address_tb = *grc->fabric_memory.start_address;
      fabric_mem_config_.has_start_address_tb = true;
    }
    fabric_mem_config_.task_stream_num = grc->fabric_memory.task_stream_num.value_or(4U);
  }
  auto_connect_ = options.AutoConnect().value_or(false);

  HIXL_CHK_STATUS_RET(InitFabricMem(), "[FabricMemEngine] Failed to initialize.");
  is_initialized_ = true;
  int32_t listen_port = 0;
  std::string listen_ip;
  (void)ParseListenInfo(local_engine_, listen_ip, listen_port);
  start_keepalive_monitor = auto_connect_ || listen_port > 0;
  HIXL_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

Status FabricMemEngine::Initialize(const HixlOptions &options) {
  // Lock: mutex_.
  HIXL_LOGI("[FabricMemEngine] Initialization started, local_engine:%s", local_engine_.c_str());
  bool start_keepalive_monitor = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HIXL_CHK_STATUS_RET(InitializeLocked(options, start_keepalive_monitor), "[FabricMemEngine] Failed to initialize.");
    HIXL_LOGI("[FabricMemEngine] Initialization succeeded, local_engine:%s", local_engine_.c_str());
  }
  if (start_keepalive_monitor) {
    HIXL_CHK_STATUS_RET(fabric_mem_transfer_service_->StartKeepaliveMonitor(),
                        "[FabricMemEngine] Failed to start keepalive monitor, local_engine:%s", local_engine_.c_str());
  }
  return SUCCESS;
}

Status FabricMemEngine::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  // Lock: local memory share handle lock.
  HIXL_CHK_STATUS_RET(CheckInitialized(), "[FabricMemEngine] Engine is not initialized.");
  TemporaryRtContext with_context(aclrt_context_);
  HIXL_LOGI("[FabricMemEngine] Registration started, type:%s, addr:%p, size:%lu", MemTypeToString(type).c_str(),
            reinterpret_cast<void *>(mem.addr), mem.len);
  HIXL_CHK_STATUS_RET(local_memory_.RegisterMem(mem, type, mem_handle), "[FabricMemEngine] Failed to register memory.");
  HIXL_LOGI("[FabricMemEngine] Registration succeeded, handle:%p.", mem_handle);
  return SUCCESS;
}

Status FabricMemEngine::DeregisterMem(MemHandle mem_handle) {
  // Lock: local memory share handle lock.
  HIXL_CHK_STATUS_RET(CheckInitialized(), "[FabricMemEngine] Engine is not initialized.");
  TemporaryRtContext with_context(aclrt_context_);
  HIXL_LOGI("[FabricMemEngine] Deregistration started, handle:%p.", mem_handle);
  HIXL_CHK_BOOL_RET_STATUS(!fabric_mem_transfer_service_->HasChannels(), FAILED,
                           "[FabricMemEngine] Disconnect peers before deregistering memory.");
  HIXL_CHK_STATUS_RET(local_memory_.DeregisterMem(mem_handle), "[FabricMemEngine] Failed to deregister memory.");
  return SUCCESS;
}

Status FabricMemEngine::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  // Lock: none (delegates to transfer service facade).
  HIXL_CHK_STATUS_RET(CheckInitialized(), "[FabricMemEngine] Engine is not initialized.");
  TemporaryRtContext with_context(aclrt_context_);
  return fabric_mem_transfer_service_->Connect(remote_engine, timeout_in_millis);
}

Status FabricMemEngine::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  // Lock: none (delegates to transfer service facade).
  HIXL_CHK_STATUS_RET(CheckInitialized(), "[FabricMemEngine] Engine is not initialized.");
  TemporaryRtContext with_context(aclrt_context_);
  return fabric_mem_transfer_service_->Disconnect(remote_engine, timeout_in_millis);
}

void FabricMemEngine::Disconnect() {
  // Lock: none (delegates to transfer service facade).
  if (!is_initialized_.load(std::memory_order_relaxed) || fabric_mem_transfer_service_ == nullptr) {
    return;
  }
  TemporaryRtContext with_context(aclrt_context_);
  fabric_mem_transfer_service_->DisconnectAll();
}

Status FabricMemEngine::EnsureAutoConnected(const AscendString &remote_engine) {
  if (!auto_connect_) {
    return SUCCESS;
  }
  constexpr int32_t kAutoConnectTimeoutMs = 3000;
  HIXL_CHK_STATUS_RET(fabric_mem_transfer_service_->EnsureConnected(remote_engine, kAutoConnectTimeoutMs),
                      "[FabricMemEngine] Auto-connect failed, remote:%s.", remote_engine.GetString());
  return SUCCESS;
}

void FabricMemEngine::DisconnectOnTransferError(const AscendString &remote_engine) {
  if (!auto_connect_) {
    return;
  }
  const Status ret = fabric_mem_transfer_service_->Disconnect(remote_engine, 0);
  if (ret != SUCCESS && ret != NOT_CONNECTED) {
    HIXL_LOGW("[FabricMemEngine] Failed to disconnect on transfer error, remote:%s, ret:%u.", remote_engine.GetString(),
              static_cast<uint32_t>(ret));
  }
}

Status FabricMemEngine::TransferSync(const AscendString &remote_engine, TransferOp operation,
                                     const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) {
  // Lock: none (transfer service routes via channel manager + slot pool).
  HIXL_CHK_STATUS_RET(CheckInitialized(), "[FabricMemEngine] Engine is not initialized.");
  TemporaryRtContext with_context(aclrt_context_);
  HIXL_CHK_BOOL_RET_STATUS(!op_descs.empty(), PARAM_INVALID,
                           "[FabricMemEngine] TransferSync failed, op_descs is empty.");
  HIXL_CHK_STATUS_RET(EnsureAutoConnected(remote_engine), "[FabricMemEngine] Failed to prepare transfer.");
  const Status ret =
      fabric_mem_transfer_service_->TransferSync(remote_engine.GetString(), operation, op_descs, timeout_in_millis);
  if (ret != SUCCESS) {
    DisconnectOnTransferError(remote_engine);
    return ret;
  }
  return SUCCESS;
}

Status FabricMemEngine::TransferAsync(const AscendString &remote_engine, TransferOp operation,
                                      const std::vector<TransferOpDesc> &op_descs, const TransferArgs &optional_args,
                                      TransferReq &req) {
  // Lock: none (transfer service owns async record on success).
  (void)optional_args;
  HIXL_CHK_STATUS_RET(CheckInitialized(), "[FabricMemEngine] Engine is not initialized.");
  TemporaryRtContext with_context(aclrt_context_);
  req = nullptr;
  HIXL_CHK_BOOL_RET_STATUS(!op_descs.empty(), PARAM_INVALID,
                           "[FabricMemEngine] TransferAsync failed, op_descs is empty.");
  HIXL_CHK_STATUS_RET(EnsureAutoConnected(remote_engine), "[FabricMemEngine] Failed to prepare transfer.");
  TransferReq new_req = nullptr;
  const Status ret =
      fabric_mem_transfer_service_->TransferAsync(remote_engine.GetString(), operation, op_descs, new_req);
  if (ret != SUCCESS) {
    DisconnectOnTransferError(remote_engine);
    return ret;
  }
  req = new_req;
  return SUCCESS;
}

Status FabricMemEngine::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  // Lock: transfer service per-channel records lock (poll).
  HIXL_CHK_STATUS_RET(CheckInitialized(), "[FabricMemEngine] Engine is not initialized.");
  AsyncTransferPollInfo poll_info;
  Status ret = fabric_mem_transfer_service_->GetTransferStatus(req, status, &poll_info);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[FabricMemEngine] GetTransferStatus failed, req:%p, ret:%u.", req, static_cast<uint32_t>(ret));
    fabric_mem_transfer_service_->CleanupAsyncTransfer(req);
    return ret;
  }
  if (status != TransferStatus::WAITING) {
    if (status == TransferStatus::COMPLETED) {
      const auto prof_type =
          (poll_info.op_type == READ ? HixlProfType::HixlOpBatchRead : HixlProfType::HixlOpBatchWrite);
      HIXL_API_PROFILING_WITH_TIME(prof_type, poll_info.prof_start_time);
    }
    if (status == TransferStatus::FAILED) {
      DisconnectOnTransferError(AscendString(poll_info.channel_id.c_str()));
    }
  }
  return SUCCESS;
}

Status FabricMemEngine::GetTransferStatus(const GetTransferStatusArgs &args, std::vector<TransferResult> &results) {
  (void)args;
  (void)results;
  return UNSUPPORTED;
}

void FabricMemEngine::CleanupFabricMemLocked() {
  is_initialized_ = false;
  {
    TemporaryRtContext with_context(aclrt_context_);
    // Finalize the transfer service first: it stops the keepalive monitor and disconnects every
    // remote (aborting in-flight transfers, unmapping remote memory, releasing slots).
    if (fabric_mem_transfer_service_ != nullptr) {
      fabric_mem_transfer_service_->Finalize();
      fabric_mem_transfer_service_.reset();
    }
    if (fabric_mem_control_server_ != nullptr) {
      fabric_mem_control_server_->Stop();
      fabric_mem_control_server_.reset();
    }
    local_memory_.Finalize();
    fabric_mem_statistic_.StopPeriodicDump();
  }
  if (aclrt_context_ != nullptr) {
    aclrt_context_holder_.reset();
    aclrt_context_ = nullptr;
  }
}

void FabricMemEngine::Finalize() {
  // Lock: mutex_.
  HIXL_LOGI("[FabricMemEngine] Finalization started");
  {
    std::lock_guard<std::mutex> lock(mutex_);
    CleanupFabricMemLocked();
  }
  HIXL_LOGI("[FabricMemEngine] Finalization succeeded");
}

Status FabricMemEngine::SendNotify(const AscendString &remote_engine, const NotifyDesc &notify,
                                   int32_t timeout_in_millis) {
  // Lock: none.
  HIXL_CHK_STATUS_RET(CheckInitialized(), "[FabricMemEngine] Engine is not initialized.");
  TemporaryRtContext with_context(aclrt_context_);
  HIXL_CHK_BOOL_RET_STATUS(notify.name.GetLength() <= kMaxNotifyNameLen, PARAM_INVALID,
                           "[FabricMemEngine] notify.name length exceed max limit:%zu, current:%zu.", kMaxNotifyNameLen,
                           notify.name.GetLength());
  HIXL_CHK_BOOL_RET_STATUS(notify.notify_msg.GetLength() <= kMaxNotifyMsgLen, PARAM_INVALID,
                           "[FabricMemEngine] notify.notify_msg length exceed max limit:%zu, current:%zu.",
                           kMaxNotifyMsgLen, notify.notify_msg.GetLength());
  HIXL_LOGI("[FabricMemEngine] Sending notify to remote:%s, name:%s", remote_engine.GetString(),
            notify.name.GetString());
  HIXL_CHK_STATUS_RET(FabricMemControlClient::SendNotify(remote_engine.GetString(), notify, timeout_in_millis),
                      "[FabricMemEngine] Failed to send notify to remote:%s.", remote_engine.GetString());
  HIXL_EVENT("[FabricMemEngine] Notify sent, remote:%s, name:%s", remote_engine.GetString(), notify.name.GetString());
  return SUCCESS;
}

Status FabricMemEngine::GetNotifies(std::vector<NotifyDesc> &notifies) {
  // Lock: none (control server internal lock).
  HIXL_CHK_STATUS_RET(CheckInitialized(), "[FabricMemEngine] Engine is not initialized.");
  TemporaryRtContext with_context(aclrt_context_);
  HIXL_CHK_STATUS_RET(fabric_mem_control_server_->DequeueNotifies(notifies),
                      "[FabricMemEngine] Failed to dequeue notifies.");
  return SUCCESS;
}

Status FabricMemEngine::RegisterCallbackProcessor(int32_t msg_type, CallbackProcessor processor) {
  (void)msg_type;
  (void)processor;
  return UNSUPPORTED;
}

}  // namespace hixl
