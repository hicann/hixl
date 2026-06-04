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
#include <utility>

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

bool FabricMemEngine::TryAcquireOperation(aclrtContext &ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (finalizing_) {
    ctx = nullptr;
    return false;
  }
  ctx = aclrt_context_;
  ++active_operations_;
  return true;
}

void FabricMemEngine::ReleaseOperation() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_operations_ > 0U) {
      --active_operations_;
    }
  }
  lifecycle_cv_.notify_all();
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
  fabric_mem_transfer_service_ = std::make_shared<FabricMemTransferService>();
  HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(),
                     "[FabricMemEngine] Failed to create fabric mem transfer service.");
  HIXL_CHK_STATUS_RET(
      fabric_mem_transfer_service_->Initialize(device_id_, fabric_mem_config_.max_stream_num,
                                               fabric_mem_config_.task_stream_num, &fabric_mem_statistic_),
      "[FabricMemEngine] Failed to initialize fabric mem transfer service.");
  return SUCCESS;
}

Status FabricMemEngine::StartControlServer() {
  fabric_mem_control_server_ = MakeUnique<FabricMemControlServer>();
  HIXL_CHECK_NOTNULL(fabric_mem_control_server_.get(), "[FabricMemEngine] Failed to create fabric mem control server.");
  auto transfer_service = fabric_mem_transfer_service_;
  auto provider = [transfer_service](std::vector<ShareHandleInfo> &share_handles) -> Status {
    HIXL_CHECK_NOTNULL(transfer_service.get(), "[FabricMemEngine] FabricMem service is null.");
    share_handles = transfer_service->GetShareHandles();
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
  HIXL_CHK_STATUS_RET(InitFabricMem(), "[FabricMemEngine] Failed to initialize.");
  is_initialized_ = true;
  HIXL_DISMISS_GUARD(fail_guard);
  HIXL_LOGI("[FabricMemEngine] Initialization succeeded, local_engine:%s", local_engine_.c_str());
  return SUCCESS;
}

Status FabricMemEngine::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  OperationGuard op_guard(*this);
  HIXL_CHK_BOOL_RET_STATUS(op_guard.Acquired(), FAILED, "[FabricMemEngine] Engine is finalizing.");
  aclrtContext ctx = op_guard.Context();
  HIXL_CHK_BOOL_RET_STATUS(ctx != nullptr, FAILED, "[FabricMemEngine] ACL runtime context is not available.");
  TemporaryRtContext with_context(ctx);
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
  OperationGuard op_guard(*this);
  HIXL_CHK_BOOL_RET_STATUS(op_guard.Acquired(), FAILED, "[FabricMemEngine] Engine is finalizing.");
  aclrtContext ctx = op_guard.Context();
  HIXL_CHK_BOOL_RET_STATUS(ctx != nullptr, FAILED, "[FabricMemEngine] ACL runtime context is not available.");
  TemporaryRtContext with_context(ctx);
  HIXL_LOGI("[FabricMemEngine] Deregistration started, handle:%p.", mem_handle);
  HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(), "[FabricMemEngine] FabricMem service is null.");
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = mem_map_.find(mem_handle);
  if (it == mem_map_.end()) {
    HIXL_LOGW("[FabricMemEngine] handle:%p is not registered.", mem_handle);
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(fabric_mem_remote_mems_.empty(), FAILED,
                           "[FabricMemEngine] Disconnect peers before deregistering memory.");
  HIXL_CHK_STATUS_RET(fabric_mem_transfer_service_->DeregisterMem(mem_handle),
                      "[FabricMemEngine] Failed to deregister memory.");
  mem_map_.erase(it);
  return SUCCESS;
}

Status FabricMemEngine::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  OperationGuard op_guard(*this);
  HIXL_CHK_BOOL_RET_STATUS(op_guard.Acquired(), FAILED, "[FabricMemEngine] Engine is finalizing.");
  aclrtContext ctx = op_guard.Context();
  HIXL_CHK_BOOL_RET_STATUS(ctx != nullptr, FAILED, "[FabricMemEngine] ACL runtime context is not available.");
  TemporaryRtContext with_context(ctx);
  const std::string remote = remote_engine.GetString();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HIXL_CHK_BOOL_RET_STATUS(fabric_mem_remote_mems_.find(remote) == fabric_mem_remote_mems_.end(), ALREADY_CONNECTED,
                             "[FabricMemEngine] remote engine:%s is already connected.", remote.c_str());
  }
  std::vector<ShareHandleInfo> share_handles;
  int32_t keepalive_fd = -1;
  HIXL_CHK_STATUS_RET(FabricMemControlClient::Fetch(remote, timeout_in_millis, share_handles, keepalive_fd),
                      "[FabricMemEngine] Failed to fetch share handles from remote:%s.", remote.c_str());
  HIXL_DISMISSABLE_GUARD(close_keepalive, ([keepalive_fd]() {
                           if (keepalive_fd >= 0) {
                             (void)close(keepalive_fd);
                           }
                         }));
  // Note: connection_mutex_ (acquired below) serializes all concurrent Connect/EnsureConnected
  // calls, so the re-check after Fetch is safe against double-install races.
  std::lock_guard<std::mutex> connection_lock(connection_mutex_);
  std::lock_guard<std::mutex> lock(mutex_);
  if (!is_initialized_ || aclrt_context_ == nullptr) {
    HIXL_LOGE(FAILED, "[FabricMemEngine] Engine finalized during connect, remote:%s.", remote.c_str());
    return FAILED;
  }
  if (fabric_mem_remote_mems_.find(remote) != fabric_mem_remote_mems_.end()) {
    return ALREADY_CONNECTED;
  }
  HIXL_CHK_STATUS_RET(CreateAndRegisterRemoteMemory(share_handles, remote),
                      "[FabricMemEngine] Failed to register remote memory, remote:%s.", remote.c_str());
  if (keepalive_fd >= 0) {
    keepalive_fds_[remote] = keepalive_fd;
  }
  HIXL_DISMISS_GUARD(close_keepalive);
  return SUCCESS;
}

Status FabricMemEngine::CreateAndRegisterRemoteMemory(const std::vector<ShareHandleInfo> &share_handles,
                                                      const std::string &remote) {
  auto remote_memory = MakeUnique<FabricMemRemoteMemory>();
  HIXL_CHECK_NOTNULL(remote_memory.get(), "[FabricMemEngine] Failed to create remote memory.");
  HIXL_CHK_STATUS_RET(remote_memory->Import(share_handles, device_id_),
                      "[FabricMemEngine] Failed to import remote memory, remote:%s.", remote.c_str());
  auto connection = std::make_shared<RemoteConnection>();
  connection->remote_memory = std::move(remote_memory);
  fabric_mem_statistic_.RegisterChannel(FabricMemStatistic::GetClientStatisticChannelId(remote));
  fabric_mem_remote_mems_[remote] = std::move(connection);
  HIXL_EVENT("[FabricMemEngine] Connected, local_engine:%s, remote_engine:%s.", local_engine_.c_str(), remote.c_str());
  return SUCCESS;
}

// Internal auto-connect helper. Precondition: the caller (TransferSync/TransferAsync) already
// holds an OperationGuard and has set the ACL context, so this does not re-acquire either.
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
  HIXL_CHK_STATUS_RET(ret, "[FabricMemEngine] Failed to fetch share handles from remote:%s.", remote.c_str());
  HIXL_DISMISSABLE_GUARD(close_keepalive, ([keepalive_fd]() {
                           if (keepalive_fd >= 0) {
                             (void)close(keepalive_fd);
                           }
                         }));
  std::lock_guard<std::mutex> connection_lock(connection_mutex_);
  std::lock_guard<std::mutex> lock(mutex_);
  if (!is_initialized_ || aclrt_context_ == nullptr) {
    HIXL_LOGE(FAILED, "[FabricMemEngine] Engine finalized during auto-connect, remote:%s.", remote.c_str());
    return FAILED;
  }
  if (fabric_mem_remote_mems_.find(remote) != fabric_mem_remote_mems_.end()) {
    return SUCCESS;
  }
  if (fabric_mem_transfer_service_ == nullptr) {
    HIXL_LOGE(FAILED, "[FabricMemEngine] Service finalized during auto-connect, remote:%s.", remote.c_str());
    return FAILED;
  }
  HIXL_CHK_STATUS_RET(CreateAndRegisterRemoteMemory(share_handles, remote),
                      "[FabricMemEngine] Failed to register remote memory, remote:%s.", remote.c_str());
  if (keepalive_fd >= 0) {
    keepalive_fds_[remote] = keepalive_fd;
  }
  HIXL_DISMISS_GUARD(close_keepalive);
  return SUCCESS;
}

Status FabricMemEngine::AcquireTransferLease(const std::string &remote_engine,
                                             std::shared_ptr<RemoteConnection> &conn) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = fabric_mem_remote_mems_.find(remote_engine);
    HIXL_CHK_BOOL_RET_STATUS(it != fabric_mem_remote_mems_.end(), NOT_CONNECTED,
                            "[FabricMemEngine] remote engine:%s is not connected.", remote_engine.c_str());
    conn = it->second;
  }
  std::lock_guard<std::mutex> conn_lock(conn->state_mutex);
  HIXL_CHK_BOOL_RET_STATUS(!conn->disconnecting, NOT_CONNECTED, "[FabricMemEngine] remote engine:%s is disconnecting.",
                           remote_engine.c_str());
  ++conn->in_flight;
  return SUCCESS;
}

void FabricMemEngine::ReleaseTransferLease(const std::shared_ptr<RemoteConnection> &conn) {
  if (conn == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> conn_lock(conn->state_mutex);
  if (conn->in_flight > 0U) {
    --conn->in_flight;
  }
  conn->cv.notify_all();
}

Status FabricMemEngine::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  OperationGuard op_guard(*this);
  HIXL_CHK_BOOL_RET_STATUS(op_guard.Acquired(), FAILED, "[FabricMemEngine] Engine is finalizing.");
  aclrtContext ctx = op_guard.Context();
  if (ctx == nullptr) {
    return DisconnectRemote(remote_engine, timeout_in_millis, true);
  }
  TemporaryRtContext with_context(ctx);
  return DisconnectRemote(remote_engine, timeout_in_millis, true);
}

void FabricMemEngine::ReleasePendingAsyncLeasesLocked(const std::string &remote) {
  for (auto it = req_map_.begin(); it != req_map_.end();) {
    if (it->second.info.remote_engine.GetString() == remote) {
      if (it->second.release_on_disconnect) {
        ReleaseTransferLease(it->second.conn);
      }
      it = req_map_.erase(it);
    } else {
      ++it;
    }
  }
}

void FabricMemEngine::RemoveConnectionEntryLocked(const std::string &remote) {
  const auto it = fabric_mem_remote_mems_.find(remote);
  if (it == fabric_mem_remote_mems_.end()) {
    return;
  }
  {
    std::lock_guard<std::mutex> req_lock(req_map_mutex_);
    RemoveChannelReqMapLocked(remote);
  }
  fabric_mem_statistic_.RemoveStatisticChannel(FabricMemStatistic::GetClientStatisticChannelId(remote));
  fabric_mem_remote_mems_.erase(it);
  auto legacy_it = keepalive_fds_.find(remote);
  if (legacy_it != keepalive_fds_.end()) {
    if (legacy_it->second >= 0) {
      (void)close(legacy_it->second);
    }
    keepalive_fds_.erase(legacy_it);
  }
}

Status FabricMemEngine::DisconnectRemote(const AscendString &remote_engine, int32_t timeout_in_millis,
                                         bool wait_in_flight) {
  const std::string remote = remote_engine.GetString();
  std::shared_ptr<RemoteConnection> conn;
  std::shared_ptr<FabricMemTransferService> transfer_service;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = fabric_mem_remote_mems_.find(remote);
    if (it == fabric_mem_remote_mems_.end()) {
      return NOT_CONNECTED;
    }
    conn = it->second;
    transfer_service = fabric_mem_transfer_service_;
  }
  {
    std::lock_guard<std::mutex> conn_lock(conn->state_mutex);
    conn->disconnecting = true;
  }
  // Release leases held by pending async requests for this channel,
  // so that in_flight can reach 0 even if GetTransferStatus is never called.
  {
    std::lock_guard<std::mutex> req_lock(req_map_mutex_);
    ReleasePendingAsyncLeasesLocked(remote);
  }
  if (transfer_service != nullptr) {
    transfer_service->RemoveChannel(remote);
  }
  if (wait_in_flight) {
    std::unique_lock<std::mutex> conn_lock(conn->state_mutex);
    const auto in_flight_done = [&conn]() { return conn->in_flight == 0U; };
    if (timeout_in_millis > 0) {
      if (!conn->cv.wait_for(conn_lock, std::chrono::milliseconds(timeout_in_millis),
                             in_flight_done)) {
        HIXL_LOGW("[FabricMemEngine] Timed out waiting for in-flight transfers on disconnect, remote:%s.",
                  remote.c_str());
        conn->disconnecting = false;
        conn->cv.notify_all();
        return TIMEOUT;
      }
    } else if (timeout_in_millis == 0) {
      if (!in_flight_done()) {
        HIXL_LOGW("[FabricMemEngine] In-flight transfers still exist on disconnect, remote:%s.", remote.c_str());
        conn->disconnecting = false;
        conn->cv.notify_all();
        return TIMEOUT;
      }
    } else {
      // Negative timeout means wait indefinitely until all in-flight transfers drain.
      conn->cv.wait(conn_lock, in_flight_done);
    }
  }
  std::lock_guard<std::mutex> lock(mutex_);
  RemoveConnectionEntryLocked(remote);
  return SUCCESS;
}

void FabricMemEngine::Disconnect() {
  OperationGuard op_guard(*this);
  if (!op_guard.Acquired()) {
    return;
  }
  aclrtContext ctx = op_guard.Context();
  std::vector<std::string> remotes;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    remotes.reserve(fabric_mem_remote_mems_.size());
    for (const auto &item : fabric_mem_remote_mems_) {
      remotes.emplace_back(item.first);
    }
  }
  // TemporaryRtContext is a no-op when ctx is null, so a single guard covers both cases.
  TemporaryRtContext with_context(ctx);
  constexpr int32_t kWaitForDrain = -1;
  for (const auto &remote : remotes) {
    const Status ret = DisconnectRemote(AscendString(remote.c_str()), kWaitForDrain);
    if (ret != SUCCESS && ret != NOT_CONNECTED) {
      HIXL_LOGW("[FabricMemEngine] Disconnect() failed to release remote:%s, ret:%d. Resources may still be held.",
                remote.c_str(), static_cast<int32_t>(ret));
    }
  }
}

void FabricMemEngine::RemoveChannelReqMapLocked(const std::string &remote_engine) {
  for (auto it = req_map_.begin(); it != req_map_.end();) {
    if (it->second.info.remote_engine.GetString() == remote_engine) {
      it = req_map_.erase(it);
    } else {
      ++it;
    }
  }
}

Status FabricMemEngine::BuildTransferContext(const RemoteConnection &conn, const std::string &remote_engine,
                                             FabricMemTransferContext &context) {
  HIXL_CHECK_NOTNULL(conn.remote_memory.get(), "[FabricMemEngine] Remote memory is null.");
  context.channel_id = remote_engine;
  context.statistic_channel_id = FabricMemStatistic::GetClientStatisticChannelId(remote_engine);
  context.remote_va_to_old_va = conn.remote_memory->GetNewVaToOldVa();
  context.stat_info = fabric_mem_statistic_.GetOrCreateStatisticInfo(context.statistic_channel_id);
  return SUCCESS;
}

Status FabricMemEngine::DisconnectOnTransferError(const AscendString &remote_engine, int32_t timeout_in_millis) {
  if (!auto_connect_) {
    return SUCCESS;
  }
  return Disconnect(remote_engine, timeout_in_millis);
}

Status FabricMemEngine::TransferSync(const AscendString &remote_engine, TransferOp operation,
                                     const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) {
  OperationGuard op_guard(*this);
  HIXL_CHK_BOOL_RET_STATUS(op_guard.Acquired(), FAILED, "[FabricMemEngine] Engine is finalizing.");
  HIXL_CHK_BOOL_RET_STATUS(!op_descs.empty(), PARAM_INVALID,
                           "[FabricMemEngine] TransferSync failed, op_descs is empty.");
  aclrtContext ctx = op_guard.Context();
  HIXL_CHK_BOOL_RET_STATUS(ctx != nullptr, FAILED, "[FabricMemEngine] ACL runtime context is not available.");
  TemporaryRtContext with_context(ctx);
  HixlProfType type = (operation == READ ? HixlProfType::HixlOpBatchRead : HixlProfType::HixlOpBatchWrite);
  HIXL_API_PROFILING(type);
  std::shared_ptr<RemoteConnection> conn;
  FabricMemTransferContext context;
  std::shared_ptr<FabricMemTransferService> transfer_service;
  Status prepare_ret = PrepareTransfer(remote_engine, timeout_in_millis, false, conn, context, transfer_service);
  if (prepare_ret != SUCCESS) {
    return prepare_ret;
  }

  Status ret = transfer_service->Transfer(context, operation, op_descs, timeout_in_millis);
  // Release lease BEFORE disconnect to avoid self-deadlock: DisconnectRemote
  // waits for in_flight == 0, which requires the lease to be released first.
  ReleaseTransferLease(conn);
  if (!IsInitialized()) {
    return FAILED;
  }
  if (ret != SUCCESS) {
    HIXL_CHK_STATUS_RET(DisconnectOnTransferError(remote_engine, timeout_in_millis),
                        "[FabricMemEngine] Failed to disconnect on transfer error.");
    return ret;
  }
  return SUCCESS;
}

Status FabricMemEngine::BuildTransferServiceContext(const std::shared_ptr<RemoteConnection> &conn,
                                                    const std::string &remote, FabricMemTransferContext &context,
                                                    std::shared_ptr<FabricMemTransferService> &service) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (fabric_mem_transfer_service_ == nullptr) {
    HIXL_LOGE(PARAM_INVALID, "[FabricMemEngine] FabricMem service is null.");
    return PARAM_INVALID;
  }
  HIXL_CHK_STATUS_RET(BuildTransferContext(*conn, remote, context),
                      "[FabricMemEngine] Failed to build transfer context.");
  service = fabric_mem_transfer_service_;
  return SUCCESS;
}

Status FabricMemEngine::PrepareTransfer(const AscendString &remote_engine, int32_t connect_timeout_in_millis,
                                        bool disconnect_on_build_failure, std::shared_ptr<RemoteConnection> &conn,
                                        FabricMemTransferContext &context,
                                        std::shared_ptr<FabricMemTransferService> &service) {
  if (auto_connect_) {
    HIXL_CHK_STATUS_RET(EnsureConnected(remote_engine, connect_timeout_in_millis),
                        "[FabricMemEngine] Auto-connect failed, remote:%s.", remote_engine.GetString());
  }
  const std::string remote = remote_engine.GetString();
  HIXL_CHK_STATUS_RET(AcquireTransferLease(remote, conn),
                      "[FabricMemEngine] Failed to acquire transfer lease, remote:%s.", remote.c_str());
  Status build_ret = BuildTransferServiceContext(conn, remote, context, service);
  if (build_ret != SUCCESS) {
    ReleaseTransferLease(conn);
    if (disconnect_on_build_failure && auto_connect_) {
      (void)Disconnect(remote_engine, 0);
    }
    return build_ret;
  }
  return SUCCESS;
}

Status FabricMemEngine::CleanupAsyncTransferOnFailure(uint64_t id, const std::shared_ptr<RemoteConnection> &conn,
                                                      const std::shared_ptr<FabricMemTransferService> &service,
                                                      TransferReq new_req, Status ret,
                                                      const AscendString &remote_engine) {
  size_t erased = 0;
  {
    std::lock_guard<std::mutex> req_lock(req_map_mutex_);
    erased = req_map_.erase(id);
  }
  (void)erased;
  // This cleanup is only used before the request is published to DisconnectRemote().
  // If a concurrent disconnect erased the pre-registered request, the submitting
  // thread still owns the lease and must release it here.
  ReleaseTransferLease(conn);
  if (ret == SUCCESS) {
    // Service-side async record was registered; cancel it to release
    // the slot (streams / host flags) that would otherwise be leaked.
    // Unlike GetTransferStatus, CancelAsyncTransfer deterministically
    // removes the record and frees the slot regardless of stream state.
    (void)service->CancelAsyncTransfer(new_req);
  }
  if (ret != SUCCESS) {
    HIXL_CHK_STATUS_RET(DisconnectOnTransferError(remote_engine, 0),
                        "[FabricMemEngine] Failed to disconnect on transfer error.");
  }
  // Submit may have succeeded but a concurrent disconnect erased the request: return the real
  // error if any, otherwise NOT_CONNECTED so callers can trigger reconnect (consistent with the
  // MarkAsyncRequestSubmitted failure path in TransferAsync).
  return ret != SUCCESS ? ret : NOT_CONNECTED;
}

Status FabricMemEngine::PreRegisterAndSubmitAsync(const std::shared_ptr<FabricMemTransferService> &transfer_service,
                                                  const FabricMemTransferContext &context, TransferOp operation,
                                                  const std::vector<TransferOpDesc> &op_descs,
                                                  const AscendString &remote_engine,
                                                  const std::shared_ptr<RemoteConnection> &conn, TransferReq new_req,
                                                  uint64_t id) {
  // Pre-register the request before service->TransferAsync. It is not released by
  // DisconnectRemote until MarkAsyncRequestSubmitted publishes it.
  {
    std::lock_guard<std::mutex> req_lock(req_map_mutex_);
    req_map_.emplace(id, FabricMemTransferRequest{TransferInfo{0U, operation, remote_engine, nullptr}, conn});
  }
  Status ret = transfer_service->TransferAsync(context, operation, op_descs, new_req);
  // Check whether the connection was marked disconnecting concurrently (e.g. by
  // DisconnectRemote) while we were registering the service-side async record.
  bool disconnected = false;
  {
    std::lock_guard<std::mutex> conn_lock(conn->state_mutex);
    disconnected = conn->disconnecting;
  }
  if (!IsInitialized() || disconnected || ret != SUCCESS) {
    return CleanupAsyncTransferOnFailure(id, conn, transfer_service, new_req, ret, remote_engine);
  }
  return SUCCESS;
}

bool FabricMemEngine::MarkAsyncRequestSubmitted(uint64_t id, uint64_t start_time) {
  std::lock_guard<std::mutex> req_lock(req_map_mutex_);
  auto it = req_map_.find(id);
  if (it == req_map_.end()) {
    return false;
  }
  it->second.info.start_time = start_time;
  it->second.release_on_disconnect = true;
  return true;
}

Status FabricMemEngine::TransferAsync(const AscendString &remote_engine, TransferOp operation,
                                      const std::vector<TransferOpDesc> &op_descs, const TransferArgs &optional_args,
                                      TransferReq &req) {
  (void)optional_args;
  OperationGuard op_guard(*this);
  HIXL_CHK_BOOL_RET_STATUS(op_guard.Acquired(), FAILED, "[FabricMemEngine] Engine is finalizing.");
  aclrtContext ctx = op_guard.Context();
  HIXL_CHK_BOOL_RET_STATUS(ctx != nullptr, FAILED, "[FabricMemEngine] ACL runtime context is not available.");
  TemporaryRtContext with_context(ctx);
  req = nullptr;
  HIXL_CHK_BOOL_RET_STATUS(!op_descs.empty(), PARAM_INVALID,
                           "[FabricMemEngine] TransferAsync failed, op_descs is empty.");
  constexpr int32_t kAutoConnectTimeoutMs = 3000;
  std::shared_ptr<RemoteConnection> conn;
  FabricMemTransferContext context;
  std::shared_ptr<FabricMemTransferService> transfer_service;
  Status prepare_ret = PrepareTransfer(remote_engine, kAutoConnectTimeoutMs, true, conn, context, transfer_service);
  if (prepare_ret != SUCCESS) {
    return prepare_ret;
  }
  const uint64_t id = next_req_id_.fetch_add(1U, std::memory_order_relaxed);
  TransferReq new_req = reinterpret_cast<void *>(static_cast<uintptr_t>(id));
  Status ret =
      PreRegisterAndSubmitAsync(transfer_service, context, operation, op_descs, remote_engine, conn, new_req, id);
  if (ret != SUCCESS) {
    return ret;
  }
  const uint64_t start_time = HixlProfilingReporter::GetSysCycleTime();
  if (!MarkAsyncRequestSubmitted(id, start_time)) {
    (void)transfer_service->CancelAsyncTransfer(new_req);
    ReleaseTransferLease(conn);
    return NOT_CONNECTED;
  }
  req = new_req;
  return SUCCESS;
}

Status FabricMemEngine::LookupAndBuildTransferContext(const TransferReq &req, FabricMemTransferRequest &transfer_req,
                                                      FabricMemTransferContext &context,
                                                      std::shared_ptr<FabricMemTransferService> &service,
                                                      bool &conn_invalid) {
  const auto id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(req));
  {
    std::lock_guard<std::mutex> req_lock(req_map_mutex_);
    const auto it = req_map_.find(id);
    HIXL_CHK_BOOL_RET_STATUS(it != req_map_.end(), PARAM_INVALID, "[FabricMemEngine] request:%p not found.", req);
    transfer_req = it->second;
  }
  const std::string remote = transfer_req.info.remote_engine.GetString();
  conn_invalid = false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (fabric_mem_transfer_service_ == nullptr || transfer_req.conn == nullptr ||
      transfer_req.conn->remote_memory == nullptr) {
    conn_invalid = true;
  } else {
    Status build_ret = BuildTransferContext(*transfer_req.conn, remote, context);
    if (build_ret != SUCCESS) {
      conn_invalid = true;
    } else {
      service = fabric_mem_transfer_service_;
    }
  }
  return SUCCESS;
}

bool FabricMemEngine::EraseRequestAndReleaseLease(uint64_t id, const std::shared_ptr<RemoteConnection> &conn) {
  size_t erased = 0U;
  {
    std::lock_guard<std::mutex> req_lock(req_map_mutex_);
    erased = req_map_.erase(id);
  }
  if (erased == 0U) {
    return false;
  }
  ReleaseTransferLease(conn);
  return true;
}

Status FabricMemEngine::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  OperationGuard op_guard(*this);
  HIXL_CHK_BOOL_RET_STATUS(op_guard.Acquired(), FAILED, "[FabricMemEngine] Engine is finalizing.");
  FabricMemTransferRequest transfer_req{};
  FabricMemTransferContext context;
  std::shared_ptr<FabricMemTransferService> transfer_service;
  bool conn_invalid = false;
  HIXL_CHK_STATUS_RET(LookupAndBuildTransferContext(req, transfer_req, context, transfer_service, conn_invalid),
                      "[FabricMemEngine] Failed to lookup request.");
  const auto id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(req));
  if (conn_invalid) {
    (void)EraseRequestAndReleaseLease(id, transfer_req.conn);
    if (auto_connect_) {
      (void)Disconnect(transfer_req.info.remote_engine, 0);
    }
    return NOT_CONNECTED;
  }

  Status ret = transfer_service->GetTransferStatus(context, req, status);
  if (ret != SUCCESS) {
    (void)EraseRequestAndReleaseLease(id, transfer_req.conn);
    HIXL_CHK_STATUS_RET(DisconnectOnTransferError(transfer_req.info.remote_engine, 0),
                        "[FabricMemEngine] Failed to disconnect on transfer error.");
    return ret;
  }
  if (status != TransferStatus::WAITING) {
    if (status == TransferStatus::COMPLETED) {
      const auto prof_type =
          (transfer_req.info.op_type == READ ? HixlProfType::HixlOpBatchRead : HixlProfType::HixlOpBatchWrite);
      HIXL_API_PROFILING_WITH_TIME(prof_type, transfer_req.info.start_time);
    }
    (void)EraseRequestAndReleaseLease(id, transfer_req.conn);
    if (status == TransferStatus::FAILED) {
      HIXL_CHK_STATUS_RET(DisconnectOnTransferError(transfer_req.info.remote_engine, 0),
                          "[FabricMemEngine] Failed to disconnect on async transfer failure.");
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
  for (auto &item : fabric_mem_remote_mems_) {
    if (item.second != nullptr) {
      std::lock_guard<std::mutex> conn_lock(item.second->state_mutex);
      item.second->disconnecting = true;
    }
  }
  {
    TemporaryRtContext with_context(aclrt_context_);
    if (fabric_mem_control_server_ != nullptr) {
      fabric_mem_control_server_->Stop();
      fabric_mem_control_server_.reset();
    }
    if (fabric_mem_transfer_service_ != nullptr) {
      fabric_mem_transfer_service_->Finalize();
      fabric_mem_transfer_service_.reset();
    }
    fabric_mem_statistic_.StopPeriodicDump();
    fabric_mem_remote_mems_.clear();
    for (auto &item : keepalive_fds_) {
      if (item.second >= 0) {
        (void)close(item.second);
      }
    }
    keepalive_fds_.clear();
    mem_map_.clear();
    {
      std::lock_guard<std::mutex> req_lock(req_map_mutex_);
      req_map_.clear();
    }
    ReleaseVirtualMemoryManager();
  }
  if (aclrt_context_ != nullptr) {
    aclrt_context_holder_.reset();
    aclrt_context_ = nullptr;
  }
}

void FabricMemEngine::Finalize() {
  HIXL_LOGI("[FabricMemEngine] Finalization started");
  std::unique_lock<std::mutex> lock(mutex_);
  finalizing_ = true;
  lifecycle_cv_.wait(lock, [this]() { return active_operations_ == 0U; });
  CleanupFabricMemLocked();
  finalizing_ = false;
  HIXL_LOGI("[FabricMemEngine] Finalization succeeded");
}

Status FabricMemEngine::SendNotify(const AscendString &remote_engine, const NotifyDesc &notify,
                                   int32_t timeout_in_millis) {
  OperationGuard op_guard(*this);
  HIXL_CHK_BOOL_RET_STATUS(op_guard.Acquired(), FAILED, "[FabricMemEngine] Engine is finalizing.");
  aclrtContext ctx = op_guard.Context();
  HIXL_CHK_BOOL_RET_STATUS(ctx != nullptr, FAILED, "[FabricMemEngine] ACL runtime context is not available.");
  TemporaryRtContext with_context(ctx);
  HIXL_LOGI("[FabricMemEngine] Sending notify to remote:%s, name:%s", remote_engine.GetString(),
            notify.name.GetString());
  HIXL_CHK_STATUS_RET(FabricMemControlClient::SendNotify(remote_engine.GetString(), notify, timeout_in_millis),
                      "[FabricMemEngine] Failed to send notify to remote:%s.", remote_engine.GetString());
  HIXL_EVENT("[FabricMemEngine] Notify sent, remote:%s, name:%s", remote_engine.GetString(), notify.name.GetString());
  return SUCCESS;
}

Status FabricMemEngine::GetNotifies(std::vector<NotifyDesc> &notifies) {
  OperationGuard op_guard(*this);
  HIXL_CHK_BOOL_RET_STATUS(op_guard.Acquired(), FAILED, "[FabricMemEngine] Engine is finalizing.");
  aclrtContext ctx = op_guard.Context();
  HIXL_CHK_BOOL_RET_STATUS(ctx != nullptr, FAILED, "[FabricMemEngine] ACL runtime context is not available.");
  TemporaryRtContext with_context(ctx);
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
