/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_CHANNEL_MANAGER_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_CHANNEL_MANAGER_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "acl/acl_rt.h"
#include "common/hixl_utils.h"
#include "fabric_mem/fabric_mem_control.h"
#include "fabric_mem/fabric_mem_memory.h"
#include "fabric_mem/fabric_mem_slot_pool.h"
#include "fabric_mem/fabric_mem_statistic.h"
#include "fabric_mem/fabric_mem_types.h"
#include "hixl/hixl_types.h"

namespace hixl {

struct FabricMemChannel {
  std::unique_ptr<FabricMemRemoteMemory> remote_memory;
  int32_t keepalive_fd{-1};

  // Host path: submit_gate gates copy submission against disconnect. A transfer holds it SHARED while
  // submitting copies to its own (private) slot streams, so concurrent transfers on the same channel
  // run in parallel. Disconnect holds it EXCLUSIVE for abort-before-unmap.
  std::shared_mutex submit_gate{};
  // records_mutex protects the bookkeeping containers below (brief mutations only). disconnecting is
  // atomic so the transfer fast path can reject a disconnecting channel before doing any work.
  std::mutex records_mutex{};
  std::atomic<bool> disconnecting{false};
  std::unordered_map<uint64_t, AsyncRecord> async_records;
  std::vector<AsyncSlot> active_sync_slots;

  // AICPU path (aligned with hixl_cs): one bound_slot per channel, shared by pipelined submits.
  // transfer_mu serializes Acquire/Issue/Complete/Disconnect; TransferAsync returns after submit
  // so the next TransferAsync can reuse bound_slot without waiting for GetTransferStatus. It is the
  // only serialization the AICPU dispatch path needs: one channel at a time, channels in parallel.
  std::mutex transfer_mu{};
  AsyncSlot bound_slot{};
  uint32_t bound_slot_refs{0U};
};

struct FabricMemChannelManagerInitParam {
  std::string local_engine;
  int32_t device_id{-1};
  bool auto_connect{false};
  FabricMemStatistic *statistic{nullptr};
  FabricMemSlotPool *slot_pool{nullptr};
  FabricMemControlServer *control_server{nullptr};
  aclrtContext aclrt_context{nullptr};
  // When true, AbortAndClearChannelRecords uses transfer_mu + bound_slot instead of submit_gate.
  bool enable_aicpu_unfold{false};
};

class FabricMemChannelManager {
 public:
  FabricMemChannelManager() = default;
  ~FabricMemChannelManager();
  FabricMemChannelManager(const FabricMemChannelManager &) = delete;
  FabricMemChannelManager &operator=(const FabricMemChannelManager &) = delete;
  FabricMemChannelManager(FabricMemChannelManager &&) = delete;
  FabricMemChannelManager &operator=(FabricMemChannelManager &&) = delete;

  Status Initialize(const FabricMemChannelManagerInitParam &param);
  void Finalize() noexcept;
  Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis);
  Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis);
  void DisconnectAll();
  Status EnsureConnected(const AscendString &remote_engine, int32_t timeout_in_millis);
  Status GetChannel(const std::string &remote_engine, std::shared_ptr<FabricMemChannel> &channel) const;
  Status BuildTransferContext(const std::string &remote_engine, FabricMemStatistic *statistic,
                              FabricMemTransferContext &context) const;
  bool HasChannels() const;
  bool IsConnected(const std::string &remote_engine) const;
  Status StartKeepaliveMonitor();
  void StopKeepaliveMonitor();
  static void SetKeepaliveCheckIntervalMs(int64_t interval_ms);
  // AICPU path only: the abort shared by disconnect and by every transfer failure. Drains the
  // channel's in-flight requests and runs the slot pool's abort flow on its bound slot; a transfer
  // that failed before registering an AsyncRecord passes its own slot / resource as `pending_*`.
  // Caller must hold channel->transfer_mu.
  void AbortAicpuChannelLocked(const std::shared_ptr<FabricMemChannel> &channel, AsyncSlot *pending_slot,
                               FabricMemAicpuRequestResource *pending_resource);

  // Async request routing: maps a request id to the channel that owns its AsyncRecord.
  void AddReqRoute(uint64_t req_id, const std::shared_ptr<FabricMemChannel> &channel);
  Status FindChannelByReq(uint64_t req_id, std::shared_ptr<FabricMemChannel> &channel) const;
  void RemoveReqRoute(uint64_t req_id);

 private:
  Status FetchAndInstallRemote(const std::string &remote, int32_t timeout_in_millis, Status if_already_connected);
  Status CreateAndRegisterRemoteMemory(const std::vector<ShareHandleInfo> &share_handles, const std::string &remote);
  Status DisconnectRemote(const AscendString &remote_engine, int32_t timeout_in_millis, bool from_auto_cleanup = false);
  void AbortAndClearChannelRecords(const std::shared_ptr<FabricMemChannel> &channel);
  void AbortAndClearHostChannelRecords(const std::shared_ptr<FabricMemChannel> &channel);
  void AbortAndClearAicpuChannelRecords(const std::shared_ptr<FabricMemChannel> &channel);
  void RemoveChannelEntryLocked(const std::string &remote);
  void CleanupChannelsLocked();
  void CheckKeepaliveFds();
  void SendOutboundHeartbeats();
  void DisconnectDeadRemote(const std::string &remote);

  // connect_mutex_: serializes Fetch + remote install across Connect/EnsureConnected.
  // Never hold channels_mutex_ during network I/O.
  std::mutex connect_mutex_;

  // channels_mutex_: protects channels_ and initialized_.
  mutable std::mutex channels_mutex_;
  std::unordered_map<std::string, std::shared_ptr<FabricMemChannel>> channels_;

  // req_route_mutex_: protects req_2_channel_. It is a leaf lock.
  // Host path lock order: submit_gate -> req_route_mutex_ / records_mutex.
  // AICPU path lock order: transfer_mu -> records_mutex / req_route_mutex_ / pool_mutex_.
  // Never take submit_gate and transfer_mu together; reverse of any listed order is forbidden.
  mutable std::mutex req_route_mutex_;
  std::unordered_map<uint64_t, std::shared_ptr<FabricMemChannel>> req_2_channel_;

  std::string local_engine_;
  int32_t device_id_{-1};
  bool auto_connect_{false};
  bool initialized_{false};
  bool enable_aicpu_unfold_{false};
  FabricMemStatistic *statistic_{nullptr};
  FabricMemSlotPool *slot_pool_{nullptr};
  FabricMemControlServer *control_server_{nullptr};
  aclrtContext aclrt_context_{nullptr};

  std::thread keepalive_monitor_;
  std::atomic<bool> keepalive_stop_{false};
  std::mutex keepalive_cv_mutex_;
  std::condition_variable keepalive_cv_;
  static int64_t keepalive_check_interval_ms_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_CHANNEL_MANAGER_H_
