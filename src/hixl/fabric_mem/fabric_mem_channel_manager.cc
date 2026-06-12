/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem/fabric_mem_channel_manager.h"

#include <unistd.h>
#include <cerrno>
#include <utility>
#include <vector>

#include "acl/acl_rt.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/scope_guard.h"

namespace hixl {

int64_t FabricMemChannelManager::keepalive_check_interval_ms_ = 10000;

void FabricMemChannelManager::SetKeepaliveCheckIntervalMs(int64_t interval_ms) {
  // Lock: none (static interval).
  keepalive_check_interval_ms_ = interval_ms > 0 ? interval_ms : 10000;
}

FabricMemChannelManager::~FabricMemChannelManager() {
  Finalize();
}

Status FabricMemChannelManager::Initialize(const FabricMemChannelManagerInitParam &param) {
  // Lock: channels_mutex_.
  std::lock_guard<std::mutex> lock(channels_mutex_);
  HIXL_CHK_BOOL_RET_STATUS(!initialized_, FAILED, "[FabricMemChannelManager] Already initialized.");
  HIXL_CHK_BOOL_RET_STATUS(param.statistic != nullptr && param.slot_pool != nullptr, PARAM_INVALID,
                           "[FabricMemChannelManager] Invalid initialization parameters.");
  local_engine_ = param.local_engine;
  device_id_ = param.device_id;
  auto_connect_ = param.auto_connect;
  statistic_ = param.statistic;
  slot_pool_ = param.slot_pool;
  control_server_ = param.control_server;
  aclrt_context_ = param.aclrt_context;
  initialized_ = true;
  return SUCCESS;
}

void FabricMemChannelManager::Finalize() noexcept {
  // Cleanup must never throw: this runs from this class's destructor and from
  // FabricMemTransferService::Finalize()/destructor. The teardown path (vector reserve/emplace in
  // AbortAndClearChannelRecords) may throw bad_alloc/length_error, so swallow everything here.
  try {
    // Lock: none (StopKeepaliveMonitor joins thread; CleanupChannelsLocked takes channels_mutex_).
    StopKeepaliveMonitor();
    std::lock_guard<std::mutex> lock(channels_mutex_);
    CleanupChannelsLocked();
    initialized_ = false;
  } catch (...) {
    HIXL_LOGW("[FabricMemChannelManager] Exception caught during finalize, ignored.");
  }
}

Status FabricMemChannelManager::CreateAndRegisterRemoteMemory(const std::vector<ShareHandleInfo> &share_handles,
                                                              const std::string &remote) {
  auto remote_memory = MakeUnique<FabricMemRemoteMemory>();
  HIXL_CHK_STATUS_RET(remote_memory->Import(share_handles, device_id_),
                      "[FabricMemChannelManager] Failed to import remote memory, remote:%s.", remote.c_str());
  auto channel = std::make_shared<FabricMemChannel>();
  channel->remote_memory = std::move(remote_memory);
  statistic_->RegisterChannel(FabricMemStatistic::GetClientStatisticChannelId(remote));
  channels_[remote] = std::move(channel);
  HIXL_EVENT("[FabricMemChannelManager] Connected, local_engine:%s, remote_engine:%s.", local_engine_.c_str(),
             remote.c_str());
  return SUCCESS;
}

Status FabricMemChannelManager::FetchAndInstallRemote(const std::string &remote, int32_t timeout_in_millis,
                                                      Status if_already_connected) {
  std::lock_guard<std::mutex> connect_lock(connect_mutex_);
  std::vector<ShareHandleInfo> share_handles;
  int32_t keepalive_fd = -1;
  HIXL_CHK_STATUS_RET(
      FabricMemControlClient::Fetch(remote, local_engine_, timeout_in_millis, share_handles, keepalive_fd),
      "[FabricMemChannelManager] Failed to fetch share handles from remote:%s.", remote.c_str());
  HIXL_DISMISSABLE_GUARD(close_keepalive, ([keepalive_fd]() {
                           if (keepalive_fd >= 0) {
                             (void)close(keepalive_fd);
                           }
                         }));
  {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    if (channels_.find(remote) != channels_.end()) {
      return if_already_connected;
    }
    HIXL_CHK_STATUS_RET(CreateAndRegisterRemoteMemory(share_handles, remote),
                        "[FabricMemChannelManager] Failed to register remote memory, remote:%s.", remote.c_str());
    if (keepalive_fd >= 0) {
      channels_[remote]->keepalive_fd = keepalive_fd;
    }
  }
  HIXL_DISMISS_GUARD(close_keepalive);
  return SUCCESS;
}

Status FabricMemChannelManager::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  // Lock: channels_mutex_ (brief check); connect_mutex_ during Fetch; none during network I/O with channels_mutex_.
  TemporaryRtContext with_context(aclrt_context_);
  const std::string remote = remote_engine.GetString();
  {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    HIXL_CHK_BOOL_RET_STATUS(initialized_, FAILED, "[FabricMemChannelManager] Not initialized.");
    if (channels_.find(remote) != channels_.end()) {
      HIXL_LOGW("[FabricMemChannelManager] remote engine:%s is already connected.", remote.c_str());
      return ALREADY_CONNECTED;
    }
  }
  return FetchAndInstallRemote(remote, timeout_in_millis, ALREADY_CONNECTED);
}

Status FabricMemChannelManager::EnsureConnected(const AscendString &remote_engine, int32_t timeout_in_millis) {
  // Lock: channels_mutex_ (brief check); connect_mutex_ during Fetch; none during network I/O with channels_mutex_.
  const std::string remote = remote_engine.GetString();
  {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    HIXL_CHK_BOOL_RET_STATUS(initialized_, FAILED, "[FabricMemChannelManager] Not initialized.");
    if (channels_.find(remote) != channels_.end()) {
      return SUCCESS;
    }
  }
  return FetchAndInstallRemote(remote, timeout_in_millis, SUCCESS);
}

Status FabricMemChannelManager::GetChannel(const std::string &remote_engine,
                                           std::shared_ptr<FabricMemChannel> &channel) const {
  // Lock: channels_mutex_.
  std::lock_guard<std::mutex> lock(channels_mutex_);
  const auto it = channels_.find(remote_engine);
  HIXL_CHK_BOOL_RET_STATUS(it != channels_.end(), NOT_CONNECTED,
                           "[FabricMemChannelManager] remote engine:%s is not connected.", remote_engine.c_str());
  channel = it->second;
  return SUCCESS;
}

void FabricMemChannelManager::AddReqRoute(uint64_t req_id, const std::shared_ptr<FabricMemChannel> &channel) {
  // Lock: req_route_mutex_.
  std::lock_guard<std::mutex> lock(req_route_mutex_);
  req_2_channel_[req_id] = channel;
}

Status FabricMemChannelManager::FindChannelByReq(uint64_t req_id, std::shared_ptr<FabricMemChannel> &channel) const {
  // Lock: req_route_mutex_.
  std::lock_guard<std::mutex> lock(req_route_mutex_);
  const auto it = req_2_channel_.find(req_id);
  HIXL_CHK_BOOL_RET_STATUS(it != req_2_channel_.end(), FAILED, "Fabric mem request:%lu not found.", req_id);
  channel = it->second;
  return SUCCESS;
}

void FabricMemChannelManager::RemoveReqRoute(uint64_t req_id) {
  // Lock: req_route_mutex_.
  std::lock_guard<std::mutex> lock(req_route_mutex_);
  (void)req_2_channel_.erase(req_id);
}

void FabricMemChannelManager::AbortAndClearChannelRecords(const std::shared_ptr<FabricMemChannel> &channel) {
  // Abort all in-flight transfers on this channel without waiting. Async record slots are aborted and
  // destroyed here; sync slots are only aborted so the owning transfer thread can release them.
  std::vector<AsyncSlot> async_slots;
  std::vector<uint64_t> req_ids;
  // Mark disconnecting first so new transfers fail fast, then take submit_gate EXCLUSIVE to drain all
  // in-flight submits: once acquired, no transfer is mid-submission and every transfer that did submit
  // has already registered its slot/record below, so the abort here covers all outstanding streams
  // before the caller unmaps the channel memory (abort-before-unmap).
  channel->disconnecting.store(true, std::memory_order_release);
  {
    std::unique_lock<std::shared_mutex> drain(channel->submit_gate);
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    req_ids.reserve(channel->async_records.size());
    async_slots.reserve(channel->async_records.size());
    for (auto &record : channel->async_records) {
      req_ids.emplace_back(record.first);
      async_slots.emplace_back(std::move(record.second.slot));
    }
    channel->async_records.clear();
    for (const auto &slot : channel->active_sync_slots) {
      FabricMemSlotPool::AbortSlotStreams(slot);
    }
    channel->active_sync_slots.clear();
  }
  for (auto &slot : async_slots) {
    slot_pool_->Release(slot, true);
  }
  for (const auto req_id : req_ids) {
    RemoveReqRoute(req_id);
  }
}

void FabricMemChannelManager::RemoveChannelEntryLocked(const std::string &remote) {
  const auto it = channels_.find(remote);
  if (it == channels_.end()) {
    return;
  }
  if (it->second != nullptr && it->second->remote_memory != nullptr) {
    it->second->remote_memory->Finalize();
  }
  statistic_->RemoveStatisticChannel(FabricMemStatistic::GetClientStatisticChannelId(remote));
  if (it->second != nullptr && it->second->keepalive_fd >= 0) {
    (void)close(it->second->keepalive_fd);
    it->second->keepalive_fd = -1;
  }
  channels_.erase(it);
}

Status FabricMemChannelManager::DisconnectRemote(const AscendString &remote_engine, int32_t timeout_in_millis,
                                                 bool from_auto_cleanup) {
  const std::string remote = remote_engine.GetString();
  std::shared_ptr<FabricMemChannel> channel;
  {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    HIXL_CHK_BOOL_RET_STATUS(initialized_, FAILED, "[FabricMemChannelManager] Not initialized.");
    const auto it = channels_.find(remote);
    if (it == channels_.end()) {
      HIXL_LOGW("[FabricMemChannelManager] remote engine:%s is not connected, skip disconnect.", remote.c_str());
      return NOT_CONNECTED;
    }
    channel = it->second;
  }
  AbortAndClearChannelRecords(channel);

  int32_t keepalive_fd = -1;
  std::string local_engine;
  {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    if (channel->keepalive_fd >= 0) {
      keepalive_fd = channel->keepalive_fd;
      channel->keepalive_fd = -1;
    }
    local_engine = local_engine_;
  }
  if (keepalive_fd >= 0) {
    (void)FabricMemControlClient::Disconnect(remote, local_engine, timeout_in_millis, from_auto_cleanup);
    (void)close(keepalive_fd);
  }

  TemporaryRtContext with_context(aclrt_context_);
  std::lock_guard<std::mutex> lock(channels_mutex_);
  RemoveChannelEntryLocked(remote);
  return SUCCESS;
}

Status FabricMemChannelManager::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  // Lock: channels_mutex_ (lookup); channel->records_mutex during abort.
  TemporaryRtContext with_context(aclrt_context_);
  return DisconnectRemote(remote_engine, timeout_in_millis);
}

void FabricMemChannelManager::DisconnectAll() {
  // Lock: channels_mutex_ (snapshot, also guards initialized_); per-channel disconnect off-lock during abort.
  std::vector<std::string> remotes;
  {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    if (!initialized_) {
      return;
    }
    remotes.reserve(channels_.size());
    for (const auto &item : channels_) {
      remotes.emplace_back(item.first);
    }
  }
  TemporaryRtContext with_context(aclrt_context_);
  for (const auto &remote : remotes) {
    const Status ret = DisconnectRemote(AscendString(remote.c_str()), 0);
    if (ret != SUCCESS && ret != NOT_CONNECTED) {
      HIXL_LOGW(
          "[FabricMemChannelManager] DisconnectAll() failed to release remote:%s, ret:%d. Resources may still be held.",
          remote.c_str(), static_cast<int32_t>(ret));
    }
  }
}

Status FabricMemChannelManager::BuildTransferContext(const std::string &remote_engine, FabricMemStatistic *statistic,
                                                     FabricMemTransferContext &context) const {
  // Lock: channels_mutex_.
  std::lock_guard<std::mutex> lock(channels_mutex_);
  HIXL_CHK_BOOL_RET_STATUS(initialized_, FAILED, "[FabricMemChannelManager] Not initialized.");
  const auto it = channels_.find(remote_engine);
  HIXL_CHK_BOOL_RET_STATUS(it != channels_.end(), NOT_CONNECTED,
                           "[FabricMemChannelManager] remote engine:%s is not connected.", remote_engine.c_str());
  context.channel_id = remote_engine;
  context.statistic_channel_id = FabricMemStatistic::GetClientStatisticChannelId(remote_engine);
  context.remote_va_to_old_va = it->second->remote_memory->GetNewVaToOldVa();
  FabricMemStatistic *stat = statistic != nullptr ? statistic : statistic_;
  HIXL_CHK_BOOL_RET_STATUS(stat != nullptr, FAILED, "[FabricMemChannelManager] Statistic is not available.");
  context.stat_info = stat->GetOrCreateStatisticInfo(context.statistic_channel_id);
  return SUCCESS;
}

bool FabricMemChannelManager::HasChannels() const {
  // Lock: channels_mutex_.
  std::lock_guard<std::mutex> lock(channels_mutex_);
  return !channels_.empty();
}

bool FabricMemChannelManager::IsConnected(const std::string &remote_engine) const {
  // Lock: channels_mutex_.
  std::lock_guard<std::mutex> lock(channels_mutex_);
  return channels_.find(remote_engine) != channels_.end();
}

void FabricMemChannelManager::CleanupChannelsLocked() {
  for (auto &item : channels_) {
    if (item.second == nullptr) {
      continue;
    }
    AbortAndClearChannelRecords(item.second);
    if (item.second->remote_memory != nullptr) {
      item.second->remote_memory->Finalize();
    }
    if (item.second->keepalive_fd >= 0) {
      (void)close(item.second->keepalive_fd);
      item.second->keepalive_fd = -1;
    }
  }
  channels_.clear();
}

Status FabricMemChannelManager::StartKeepaliveMonitor() {
  // Lock: none.
  keepalive_stop_.store(false);
  keepalive_monitor_ = std::thread([this]() {
    std::unique_lock<std::mutex> lock(keepalive_cv_mutex_);
    while (!keepalive_stop_.load()) {
      CheckKeepaliveFds();
      keepalive_cv_.wait_for(lock, std::chrono::milliseconds(keepalive_check_interval_ms_),
                             [this] { return keepalive_stop_.load(); });
    }
  });
  return SUCCESS;
}

void FabricMemChannelManager::StopKeepaliveMonitor() {
  // Lock: none (signals keepalive_cv_; joins thread).
  keepalive_stop_.store(true);
  keepalive_cv_.notify_all();
  if (keepalive_monitor_.joinable()) {
    keepalive_monitor_.join();
  }
}

void FabricMemChannelManager::SendOutboundHeartbeats() {
  // Snapshot a private dup() of each keepalive fd under channels_mutex_. dup() keeps the underlying
  // socket alive (its open file description is reference counted), so even if a concurrent Disconnect
  // closes the original fd, the off-lock SendHeartBeat below can never write to a closed (or recycled)
  // fd. Sends stay off-lock so a slow peer cannot stall Disconnect or the keepalive monitor.
  std::vector<std::pair<std::string, int32_t>> keepalive_copy;
  {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    if (!initialized_) {
      return;
    }
    keepalive_copy.reserve(channels_.size());
    for (const auto &item : channels_) {
      if (item.second == nullptr || item.second->keepalive_fd < 0) {
        continue;
      }
      const int32_t dup_fd = ::dup(item.second->keepalive_fd);
      if (dup_fd < 0) {
        HIXL_LOGW("[FabricMemChannelManager] Failed to dup keepalive fd for remote:%s, errno:%d.", item.first.c_str(),
                  errno);
        continue;
      }
      keepalive_copy.emplace_back(item.first, dup_fd);
    }
  }
  for (const auto &item : keepalive_copy) {
    const Status ret = FabricMemControlClient::SendHeartBeat(item.second);
    (void)close(item.second);
    if (ret == SUCCESS) {
      continue;
    }
    HIXL_LOGW("[FabricMemChannelManager] Outbound heartbeat failed for remote:%s, ret:%u.", item.first.c_str(),
              static_cast<uint32_t>(ret));
    if (auto_connect_) {
      DisconnectDeadRemote(item.first);
    }
  }
}

void FabricMemChannelManager::CheckKeepaliveFds() {
  {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    if (!initialized_) {
      return;
    }
  }
  SendOutboundHeartbeats();
  if (auto_connect_ && control_server_ != nullptr) {
    control_server_->CheckClientHeartbeatTimeouts();
  }
}

void FabricMemChannelManager::DisconnectDeadRemote(const std::string &remote) {
  {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    if (!initialized_) {
      return;
    }
    if (channels_.find(remote) == channels_.end()) {
      return;
    }
  }
  TemporaryRtContext with_context(aclrt_context_);
  const Status ret = DisconnectRemote(AscendString(remote.c_str()), 0, true);
  if (ret == SUCCESS) {
    HIXL_EVENT("[FabricMemChannelManager] Auto-disconnected dead remote:%s due to keepalive failure.", remote.c_str());
  } else if (ret != NOT_CONNECTED) {
    HIXL_EVENT("[FabricMemChannelManager] Failed to auto-disconnect dead remote:%s, ret:%u.", remote.c_str(),
               static_cast<uint32_t>(ret));
  }
}

}  // namespace hixl
