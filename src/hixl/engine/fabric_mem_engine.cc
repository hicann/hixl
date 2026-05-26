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

#include <limits>
#include <string>
#include <unistd.h>
#include <unordered_set>

#include "acl/acl_rt.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "fabric_mem/virtual_memory_manager.h"
#include "profiling/prof_api_reg.h"

namespace hixl {
namespace {
std::mutex g_fabric_mem_vm_mutex;
size_t g_fabric_mem_vm_ref_count = 0;

Status BuildAddrInfo(const MemDesc &mem, MemType type, AddrInfo &addr_info) {
  HIXL_CHK_BOOL_RET_STATUS(mem.len > 0, PARAM_INVALID, "[FabricMemEngine] Memory length must be greater than zero.");
  const auto max_addr = std::numeric_limits<uintptr_t>::max();
  HIXL_CHK_BOOL_RET_STATUS(mem.addr <= max_addr - mem.len, PARAM_INVALID,
                           "[FabricMemEngine] Memory range overflow, addr:%p, size:%lu.",
                           reinterpret_cast<void *>(mem.addr), mem.len);
  addr_info = AddrInfo{mem.addr, mem.addr + mem.len, type};
  return SUCCESS;
}
}  // namespace

bool FabricMemEngine::IsInitialized() const {
  return is_initialized_.load(std::memory_order_relaxed);
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

Status FabricMemEngine::InitTransferService() {
  fabric_mem_transfer_service_ = MakeUnique<FabricMemTransferService>();
  HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(),
                     "[FabricMemEngine] Failed to create fabric mem transfer service.");
  HIXL_CHK_STATUS_RET(
      fabric_mem_transfer_service_->Initialize(fabric_mem_config_.max_stream_num, fabric_mem_config_.task_stream_num,
                                               &fabric_mem_statistic_),
      "[FabricMemEngine] Failed to initialize fabric mem transfer service.");
  return SUCCESS;
}

Status FabricMemEngine::StartControlServer() {
  fabric_mem_control_server_ = MakeUnique<FabricMemControlServer>();
  HIXL_CHECK_NOTNULL(fabric_mem_control_server_.get(), "[FabricMemEngine] Failed to create fabric mem control server.");
  auto provider = [this](std::vector<ShareHandleInfo> &share_handles) -> Status {
    HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(), "[FabricMemEngine] FabricMem service is null.");
    share_handles = fabric_mem_transfer_service_->GetShareHandles();
    return SUCCESS;
  };
  HIXL_CHK_STATUS_RET(fabric_mem_control_server_->Start(local_engine_, provider),
                      "[FabricMemEngine] Failed to start fabric mem control server.");
  return SUCCESS;
}

Status FabricMemEngine::AcquireVirtualMemoryManager() {
  std::lock_guard<std::mutex> lock(g_fabric_mem_vm_mutex);
  HIXL_CHK_STATUS_RET(ApplyVirtualMemoryConfig(), "[FabricMemEngine] Failed to apply virtual memory config.");
  HIXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().Initialize(),
                      "[FabricMemEngine] Failed to initialize fabric virtual memory manager.");
  ++g_fabric_mem_vm_ref_count;
  has_acquired_virtual_memory_ = true;
  return SUCCESS;
}

void FabricMemEngine::ReleaseVirtualMemoryManager() {
  if (!has_acquired_virtual_memory_) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_fabric_mem_vm_mutex);
  if (g_fabric_mem_vm_ref_count > 0) {
    --g_fabric_mem_vm_ref_count;
    if (g_fabric_mem_vm_ref_count == 0) {
      VirtualMemoryManager::GetInstance().Finalize();
    }
  }
  has_acquired_virtual_memory_ = false;
}

Status FabricMemEngine::InitFabricMem() {
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this]() { CleanupFabricMemLocked(); }));
  HIXL_CHK_STATUS_RET(AcquireVirtualMemoryManager(), "[FabricMemEngine] Failed to acquire virtual memory manager.");
  HIXL_CHK_STATUS_RET(InitTransferService(), "[FabricMemEngine] Failed to initialize transfer service.");
  HIXL_CHK_STATUS_RET(fabric_mem_statistic_.StartPeriodicDump(),
                      "[FabricMemEngine] Failed to start fabric mem statistic dump.");
  HIXL_CHK_STATUS_RET(StartControlServer(), "[FabricMemEngine] Failed to start control server.");
  HIXL_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

Status FabricMemEngine::Initialize(const std::map<AscendString, AscendString> &options) {
  HIXL_LOGI("[FabricMemEngine] Initialization started, local_engine:%s", local_engine_.c_str());
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_STATUS_RET(FabricMemConfigParser::Parse(options, fabric_mem_config_),
                      "[FabricMemEngine] Failed to parse fabric mem config.");
  HIXL_CHK_BOOL_RET_STATUS(fabric_mem_config_.enabled, PARAM_INVALID,
                           "[FabricMemEngine] EnableUseFabricMem must be 1.");
  auto_connect_ = fabric_mem_config_.auto_connect;
  HIXL_CHK_STATUS_RET(InitFabricMem(), "[FabricMemEngine] Failed to initialize.");
  is_initialized_ = true;
  HIXL_LOGI("[FabricMemEngine] Initialization succeeded, local_engine:%s", local_engine_.c_str());
  return SUCCESS;
}

bool FabricMemEngine::HasConnectionsLocked() const {
  return !fabric_mem_remote_mems_.empty();
}

Status FabricMemEngine::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HIXL_LOGI("[FabricMemEngine] Registration started, type:%s, addr:%p, size:%lu", MemTypeToString(type).c_str(),
            reinterpret_cast<void *>(mem.addr), mem.len);
  HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(), "[FabricMemEngine] FabricMem service is null.");
  AddrInfo cur_info{};
  HIXL_CHK_STATUS_RET(BuildAddrInfo(mem, type, cur_info), "[FabricMemEngine] Invalid memory range.");
  std::lock_guard<std::mutex> lock(mutex_);
  std::map<MemHandle, AddrInfo> addr_map;
  for (const auto &item : mem_map_) {
    AddrInfo registered_info{};
    HIXL_CHK_STATUS_RET(BuildAddrInfo(item.second.mem, item.second.type, registered_info),
                        "[FabricMemEngine] Registered memory range is invalid.");
    addr_map[item.first] = registered_info;
  }
  bool is_duplicate = false;
  MemHandle existing_handle = nullptr;
  HIXL_CHK_STATUS_RET(CheckAddrOverlap(cur_info, addr_map, is_duplicate, existing_handle),
                      "[FabricMemEngine] Failed to check address overlap.");
  if (is_duplicate) {
    mem_handle = existing_handle;
    return SUCCESS;
  }
  HIXL_CHK_STATUS_RET(fabric_mem_transfer_service_->RegisterMem(mem, type, mem_handle),
                      "[FabricMemEngine] Failed to register memory.");
  mem_map_.emplace(mem_handle, MemInfo{mem_handle, mem, type});
  HIXL_LOGI("[FabricMemEngine] Registration succeeded, handle:%p.", mem_handle);
  return SUCCESS;
}

Status FabricMemEngine::DeregisterMem(MemHandle mem_handle) {
  HIXL_LOGI("[FabricMemEngine] Deregistration started, handle:%p.", mem_handle);
  HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(), "[FabricMemEngine] FabricMem service is null.");
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = mem_map_.find(mem_handle);
  if (it == mem_map_.end()) {
    HIXL_LOGW("[FabricMemEngine] handle:%p is not registered.", mem_handle);
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(!HasConnectionsLocked(), FAILED,
                           "[FabricMemEngine] Disconnect peers before deregistering memory.");
  HIXL_CHK_STATUS_RET(fabric_mem_transfer_service_->DeregisterMem(mem_handle),
                      "[FabricMemEngine] Failed to deregister memory.");
  mem_map_.erase(it);
  return SUCCESS;
}

Status FabricMemEngine::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  std::lock_guard<std::mutex> lock(mutex_);
  return ConnectLocked(remote_engine, timeout_in_millis);
}

Status FabricMemEngine::ConnectLocked(const AscendString &remote_engine, int32_t timeout_in_millis) {
  const std::string remote = remote_engine.GetString();
  HIXL_CHK_BOOL_RET_STATUS(fabric_mem_remote_mems_.find(remote) == fabric_mem_remote_mems_.end(), ALREADY_CONNECTED,
                           "[FabricMemEngine] remote engine:%s is already connected.", remote.c_str());
  std::vector<ShareHandleInfo> share_handles;
  int32_t keepalive_fd = -1;
  HIXL_CHK_STATUS_RET(FabricMemControlClient::Fetch(remote, timeout_in_millis, share_handles, keepalive_fd),
                      "[FabricMemEngine] Failed to fetch share handles from remote:%s.", remote.c_str());
  if (keepalive_fd >= 0) {
    keepalive_fds_[remote] = keepalive_fd;
  }
  return CreateAndRegisterRemoteMemory(share_handles, remote);
}

Status FabricMemEngine::CreateAndRegisterRemoteMemory(const std::vector<ShareHandleInfo> &share_handles,
                                                       const std::string &remote) {
  auto remote_memory = MakeUnique<FabricMemRemoteMemory>();
  HIXL_CHECK_NOTNULL(remote_memory.get(), "[FabricMemEngine] Failed to create remote memory.");
  int32_t device_id = -1;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&device_id), "[FabricMemEngine] Failed to get device id.");
  HIXL_CHK_STATUS_RET(remote_memory->Import(share_handles, device_id),
                      "[FabricMemEngine] Failed to import remote memory, remote:%s.", remote.c_str());
  fabric_mem_statistic_.RegisterChannel(FabricMemStatistic::GetClientStatisticChannelId(remote));
  fabric_mem_remote_mems_[remote] = std::move(remote_memory);
  HIXL_EVENT("[FabricMemEngine] Connected, local_engine:%s, remote_engine:%s.", local_engine_.c_str(), remote.c_str());
  return SUCCESS;
}

Status FabricMemEngine::EnsureConnected(const AscendString &remote_engine, int32_t timeout_in_millis) {
  const std::string remote = remote_engine.GetString();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fabric_mem_remote_mems_.find(remote) != fabric_mem_remote_mems_.end()) {
      return SUCCESS;
    }
  }
  std::vector<ShareHandleInfo> share_handles;
  int32_t keepalive_fd = -1;
  Status ret = FabricMemControlClient::Fetch(remote, timeout_in_millis, share_handles, keepalive_fd);
  HIXL_CHK_STATUS_RET(ret,
                      "[FabricMemEngine] Failed to fetch share handles from remote:%s.", remote.c_str());
  std::lock_guard<std::mutex> lock(mutex_);
  if (fabric_mem_remote_mems_.find(remote) != fabric_mem_remote_mems_.end()) {
    if (keepalive_fd >= 0) {
      (void)close(keepalive_fd);
    }
    return SUCCESS;
  }
  if (keepalive_fd >= 0) {
    auto [it, inserted] = keepalive_fds_.emplace(remote, keepalive_fd);
    (void)it;
    if (!inserted) {
      (void)close(keepalive_fd);
    }
  }
  if (fabric_mem_transfer_service_ == nullptr) {
    HIXL_LOGE(FAILED, "[FabricMemEngine] Service finalized during auto-connect, remote:%s.", remote.c_str());
    return FAILED;
  }
  return CreateAndRegisterRemoteMemory(share_handles, remote);
}

Status FabricMemEngine::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  std::lock_guard<std::mutex> lock(mutex_);
  return DisconnectLocked(remote_engine, timeout_in_millis);
}

Status FabricMemEngine::DisconnectLocked(const AscendString &remote_engine, int32_t timeout_in_millis) {
  (void)timeout_in_millis;
  const std::string remote = remote_engine.GetString();
  const auto it = fabric_mem_remote_mems_.find(remote);
  if (it == fabric_mem_remote_mems_.end()) {
    return NOT_CONNECTED;
  }
  if (fabric_mem_transfer_service_ != nullptr) {
    fabric_mem_transfer_service_->RemoveChannel(remote);
  }
  RemoveChannelReqMapLocked(remote);
  fabric_mem_statistic_.RemoveStatisticChannel(FabricMemStatistic::GetClientStatisticChannelId(remote));
  fabric_mem_remote_mems_.erase(it);
  auto legacy_it = keepalive_fds_.find(remote);
  if (legacy_it != keepalive_fds_.end()) {
    if (legacy_it->second >= 0) {
      (void)close(legacy_it->second);
    }
    keepalive_fds_.erase(legacy_it);
  }
  return SUCCESS;
}

void FabricMemEngine::Disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (fabric_mem_transfer_service_ != nullptr) {
    for (const auto &item : fabric_mem_remote_mems_) {
      fabric_mem_transfer_service_->RemoveChannel(item.first);
      fabric_mem_statistic_.RemoveStatisticChannel(FabricMemStatistic::GetClientStatisticChannelId(item.first));
    }
  }
  fabric_mem_remote_mems_.clear();
  for (auto &item : keepalive_fds_) {
    if (item.second >= 0) {
      (void)close(item.second);
    }
  }
  keepalive_fds_.clear();
  req_map_.clear();
}

void FabricMemEngine::RemoveChannelReqMapLocked(const std::string &remote_engine) {
  for (auto it = req_map_.begin(); it != req_map_.end();) {
    if (it->second.remote_engine.GetString() == remote_engine) {
      it = req_map_.erase(it);
    } else {
      ++it;
    }
  }
}

Status FabricMemEngine::BuildTransferContextLocked(const std::string &remote_engine,
                                                   FabricMemTransferContext &context) {
  const auto it = fabric_mem_remote_mems_.find(remote_engine);
  HIXL_CHK_BOOL_RET_STATUS(it != fabric_mem_remote_mems_.end(), NOT_CONNECTED,
                           "[FabricMemEngine] remote engine:%s is not connected.", remote_engine.c_str());
  context.channel_id = remote_engine;
  context.statistic_channel_id = FabricMemStatistic::GetClientStatisticChannelId(remote_engine);
  context.remote_va_to_old_va = it->second->GetNewVaToOldVa();
  context.stat_info = fabric_mem_statistic_.GetOrCreateStatisticInfo(context.statistic_channel_id).get();
  return SUCCESS;
}

Status FabricMemEngine::TransferSync(const AscendString &remote_engine, TransferOp operation,
                                     const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) {
  HixlProfType type = (operation == READ ? HixlProfType::HixlOpBatchRead : HixlProfType::HixlOpBatchWrite);
  HIXL_API_PROFILING(type);
  if (auto_connect_) {
    HIXL_CHK_STATUS_RET(EnsureConnected(remote_engine, timeout_in_millis),
                        "[FabricMemEngine] Auto-connect failed, remote:%s.", remote_engine.GetString());
  }
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(), "[FabricMemEngine] FabricMem service is null.");
  FabricMemTransferContext context;
  Status ret = BuildTransferContextLocked(remote_engine.GetString(), context);
  if (ret != SUCCESS) {
    if (auto_connect_) {
      (void)DisconnectLocked(remote_engine, timeout_in_millis);
    }
    return ret;
  }
  ret = fabric_mem_transfer_service_->Transfer(context, operation, op_descs, timeout_in_millis);
  if (ret != SUCCESS && auto_connect_) {
    (void)DisconnectLocked(remote_engine, timeout_in_millis);
  }
  return ret;
}

Status FabricMemEngine::TransferAsync(const AscendString &remote_engine, TransferOp operation,
                                      const std::vector<TransferOpDesc> &op_descs, const TransferArgs &optional_args,
                                      TransferReq &req) {
  (void)optional_args;
  req = nullptr;
  if (auto_connect_) {
    constexpr int32_t kAutoConnectTimeoutMs = 3000;
    HIXL_CHK_STATUS_RET(EnsureConnected(remote_engine, kAutoConnectTimeoutMs),
                        "[FabricMemEngine] Auto-connect failed, remote:%s.", remote_engine.GetString());
  }
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(), "[FabricMemEngine] FabricMem service is null.");
  FabricMemTransferContext context;
  Status ret = BuildTransferContextLocked(remote_engine.GetString(), context);
  if (ret != SUCCESS) {
    if (auto_connect_) {
      (void)DisconnectLocked(remote_engine, 0);
    }
    return ret;
  }
  const uint64_t id = next_req_id_.fetch_add(1U, std::memory_order_relaxed);
  TransferReq new_req = reinterpret_cast<void *>(static_cast<uintptr_t>(id));
  ret = fabric_mem_transfer_service_->TransferAsync(context, operation, op_descs, new_req);
  if (ret != SUCCESS && auto_connect_) {
    (void)DisconnectLocked(remote_engine, 0);
    return ret;
  }
  const uint64_t start_time = HixlProfilingReporter::GetSysCycleTime();
  req_map_.emplace(id, TransferInfo{start_time, operation, remote_engine});
  req = new_req;
  return SUCCESS;
}

Status FabricMemEngine::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(req));
  const auto it = req_map_.find(id);
  HIXL_CHK_BOOL_RET_STATUS(it != req_map_.end(), PARAM_INVALID, "[FabricMemEngine] request:%p not found.", req);
  HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(), "[FabricMemEngine] FabricMem service is null.");
  const AscendString remote_engine = it->second.remote_engine;
  FabricMemTransferContext context;
  Status ret = BuildTransferContextLocked(remote_engine.GetString(), context);
  if (ret != SUCCESS) {
    req_map_.erase(it);
    if (auto_connect_) {
      (void)DisconnectLocked(remote_engine, 0);
    }
    return ret;
  }
  ret = fabric_mem_transfer_service_->GetTransferStatus(context, req, status);
  if (ret != SUCCESS) {
    req_map_.erase(it);
    if (auto_connect_) {
      (void)DisconnectLocked(remote_engine, 0);
    }
    return ret;
  }
  if (status != TransferStatus::WAITING) {
    if (status == TransferStatus::COMPLETED) {
      const auto type = (it->second.op_type == READ ? HixlProfType::HixlOpBatchRead : HixlProfType::HixlOpBatchWrite);
      HIXL_API_PROFILING_WITH_TIME(type, it->second.start_time);
    }
    req_map_.erase(it);
  }
  return SUCCESS;
}

void FabricMemEngine::CleanupFabricMemLocked() {
  if (fabric_mem_control_server_ != nullptr) {
    fabric_mem_control_server_->Stop();
    fabric_mem_control_server_.reset();
  }
  fabric_mem_remote_mems_.clear();
  for (auto &item : keepalive_fds_) {
    if (item.second >= 0) {
      (void)close(item.second);
    }
  }
  keepalive_fds_.clear();
  if (fabric_mem_transfer_service_ != nullptr) {
    fabric_mem_transfer_service_->Finalize();
    fabric_mem_transfer_service_.reset();
  }
  fabric_mem_statistic_.StopPeriodicDump();
  mem_map_.clear();
  req_map_.clear();
  ReleaseVirtualMemoryManager();
  is_initialized_ = false;
}

void FabricMemEngine::Finalize() {
  HIXL_LOGI("[FabricMemEngine] Finalization started");
  std::lock_guard<std::mutex> lock(mutex_);
  CleanupFabricMemLocked();
  HIXL_LOGI("[FabricMemEngine] Finalization succeeded");
}

Status FabricMemEngine::SendNotify(const AscendString &remote_engine, const NotifyDesc &notify,
                                   int32_t timeout_in_millis) {
  HIXL_LOGI("[FabricMemEngine] Sending notify to remote:%s, name:%s", remote_engine.GetString(),
            notify.name.GetString());
  HIXL_CHK_STATUS_RET(FabricMemControlClient::SendNotify(remote_engine.GetString(), notify, timeout_in_millis),
                      "[FabricMemEngine] Failed to send notify to remote:%s.", remote_engine.GetString());
  HIXL_EVENT("[FabricMemEngine] Notify sent, remote:%s, name:%s", remote_engine.GetString(), notify.name.GetString());
  return SUCCESS;
}

Status FabricMemEngine::GetNotifies(std::vector<NotifyDesc> &notifies) {
  if (fabric_mem_control_server_ == nullptr) {
    notifies.clear();
    return SUCCESS;
  }
  HIXL_CHK_STATUS_RET(fabric_mem_control_server_->DequeueNotifies(notifies),
                      "[FabricMemEngine] Failed to dequeue notifies.");
  return SUCCESS;
}

Status FabricMemEngine::RegisterCallbackProcessor(int32_t msg_type, CallbackProcessor processor) {
  (void)msg_type;
  (void)processor;
  HIXL_LOGE(UNSUPPORTED, "[FabricMemEngine] Method RegisterCallbackProcessor is not supported.");
  return UNSUPPORTED;
}
}  // namespace hixl
