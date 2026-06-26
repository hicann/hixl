/**
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_H_

#include <mutex>
#include <atomic>
#include <utility>
#include <chrono>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include "nlohmann/json.hpp"
#include "acl/acl.h"
#include "adxl/adxl_types.h"
#include "hccl/hccl_adapter.h"
#include "control_msg_handler.h"
#include "adxl/transfer_slot_pool.h"

namespace adxl {
enum class ChannelType {
  kClient = 0,
  kServer = 1,
};

struct ChannelInfo {
  ChannelType channel_type;
  std::string channel_id;
  uint32_t peer_rank_id;
  uint32_t local_rank_id;
  HcclCommConfig comm_config;
  std::string rank_table;
  std::map<MemHandle, void *> registered_mems;
  HcclComm comm;
  int32_t timeout_sec;
};

class BufferedTransfer {
 public:
  explicit BufferedTransfer(std::function<Status(HcclOneSideOpDesc *descs, uint32_t desc_num)> trans_func);
  Status Put(const std::vector<TransferOpDesc> &op_descs);

 private:
  Status Flush();

  std::vector<HcclOneSideOpDesc> op_descs_;
  std::function<Status(HcclOneSideOpDesc *descs, uint32_t desc_num)> trans_func_;
};

struct AsyncRecord {
  // The channel-shared slot this request was issued on; keeps the slot alive (ref-count) until the request is reaped.
  std::shared_ptr<SlotHandle> slot;
  // Per-request host-pinned flag; set to 1 by a device-to-host copy of dev_const_one on the slot stream on completion.
  void *host_flag = nullptr;
  std::chrono::steady_clock::time_point transfer_start;
  uint64_t transfer_bytes = 0UL;
  uint64_t op_desc_count = 0UL;
};

enum class RecvState { WAITING_FOR_HEADER, WAITING_FOR_BODY };

class CommChannel {
 public:
  explicit CommChannel(ChannelInfo info) : channel_info_(std::move(info)) {};
  Status Initialize();
  Status Finalize();
  std::string GetChannelId() const;
  Status TransferSync(TransferOp operation, const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis);
  Status TransferAsync(TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                       const TransferArgs &optional_args, TransferReq &req);
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status);
  Status SetSocketNonBlocking(int32_t fd);
  void StopHeartbeat();
  Status SendControlMsg(const std::function<Status(int32_t fd)> &func);
  Status CommWithFd(const std::function<Status(int32_t)> &func);
  Status SendHeartBeat(const std::function<Status(int32_t)> &func);
  static void SetHeartbeatTimeout(int64_t timeout_in_millis);
  int32_t GetFd() const {
    return fd_;
  }
  void UpdateHeartbeatTime();
  bool IsHeartbeatTimeout() const;
  void SetSlotPool(TransferSlotPool *slot_pool);

  // Runs issue_fn on the channel-shared slot stream and then synchronizes it with a timeout. Acquires/releases the
  // shared slot; on failure it flags unfinished work, aborts the slot, and may mark the link unavailable.
  // Shared by the direct synchronous transfer path and the buffer transfer service.
  Status RunSyncOnSlot(const std::function<Status(aclrtStream stream)> &issue_fn, int32_t timeout_in_millis);

  void GetNotifyMessages(std::vector<NotifyDesc> &notifies);

  int32_t GetTransferCount() const {
    return transfer_count_.load(std::memory_order_acquire);
  }
  bool IsDisconnecting() const {
    return disconnect_flag_.load(std::memory_order_acquire);
  }
  bool IsFinalized() const {
    return finalized_.load(std::memory_order_acquire);
  }
  // unavailable_ is set on any fatal transport error (drives disconnect sleep guard). fail_fast_ gates whether
  // CheckAvailableLocked rejects new transfers; channel pool leaves fail_fast_ off (eviction handles those links).
  void SetFailFastEnabled(bool enabled) {
    fail_fast_enabled_ = enabled;
  }
  bool IsUnavailable() const {
    return unavailable_.load(std::memory_order_acquire);
  }
  bool GetHasTransferred() const {
    return has_transfered_.load(std::memory_order_acquire);
  }
  void SetHasTransferred(bool value) {
    has_transfered_.store(value, std::memory_order_release);
  }
  void IncrementTransferCount() {
    transfer_count_++;
  }
  void DecrementTransferCount() {
    transfer_count_--;
  }
  void SetDisconnecting(bool value) {
    disconnect_flag_.store(value, std::memory_order_release);
  }
  Status TransferAsyncWithTimeout(TransferOp operation, const std::vector<TransferOpDesc> &op_descs, aclrtStream stream,
                                  uint64_t timeout);
  std::string GetStatisticChannelId() const;

 private:
  Status InitializeHcclComm();
  Status BindRegisteredMemory(std::vector<void *> &bind_handles);
  Status PrepareHcclComm(const std::chrono::steady_clock::time_point &hccl_start);
  Status ClearResources();
  bool HasInFlightWorkForDisconnect();
  void AbortActiveSlotStreamForDisconnect();
  Status TeardownHcclComm();
  void ResetAsyncStateOnDisconnect();
  void ClearNotifyMessages();
  // Enqueue HcclBatchGet/Put on the given stream (batched via BufferedTransfer).
  Status IssueHcclBatch(TransferOp operation, const std::vector<TransferOpDesc> &op_descs, aclrtStream stream);

  // --- Shared slot lifecycle: one slot per channel, ref-counted across its in-flight batch ---
  // Borrow (or reuse) the single shared slot bound to this channel; increments the shared_ptr ref-count.
  Status AcquireSharedSlot(std::shared_ptr<SlotHandle> &slot_out);
  // Drop one reference (success path); when the channel holds the last reference, return the slot to the pool.
  void ReleaseSharedSlotRef(std::shared_ptr<SlotHandle> &slot_ref);
  // Tear the shared slot down (failure/disconnect path): aclrtStreamAbort + destroy/recreate ctx+stream in the
  // pool, then unbind it from the channel.
  void AbortSharedSlot();
  // Return a per-request host-pinned completion flag to its slot free-list (or free it if the pool is gone).
  void ReleaseHostFlag(const std::shared_ptr<SlotHandle> &slot, void *host_flag);

  // --- Async record recycling: req_2_async_record_ owns one host_flag + one slot reference per request ---
  // Remove one record and recycle its host_flag + slot reference (completion or per-request drop).
  void ReleaseAsyncRecord(uint64_t id);
  // Recycle the host_flag of every pending record and drop their slot references. The slot itself is torn down by
  // AbortSharedSlot, which the fatal/disconnect callers run right after.
  void PurgeAllAsyncRecords();
  // Fatal-error response: purge all pending records, abort the shared slot, mark the link unavailable.
  // Caller holds device_launch_mu_.
  void FailChannel(Status ret);
  // Record statistics for a finished request, then reap it.
  void CompleteRequest(uint64_t id, const std::chrono::steady_clock::time_point &transfer_start,
                       uint64_t transfer_bytes, uint64_t op_desc_count);

  Status IssueAsyncBatchWithHostFlag(TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                                     const std::shared_ptr<SlotHandle> &slot, void *host_flag);
  Status LookupPendingAsyncTransfer(uint64_t id, std::shared_ptr<SlotHandle> &slot, void *&host_flag,
                                    std::chrono::steady_clock::time_point &transfer_start, uint64_t &transfer_bytes,
                                    uint64_t &op_desc_count);
  static bool IsHostFlagDone(void *host_flag);
  void MarkUnavailableOnError(Status ret);
  // Fail-fast gate: only rejects when fail_fast_ is on and the link was marked unavailable by a prior fatal error.
  Status CheckAvailableLocked() const;
  ChannelInfo channel_info_;
  // mutex for fd
  std::mutex mutex_;
  std::atomic<bool> with_heartbeat_{false};
  std::chrono::steady_clock::time_point last_heartbeat_time_;
  static int64_t timeout_in_millis_;

  std::atomic<int32_t> transfer_count_{0};
  std::atomic<bool> disconnect_flag_{false};
  std::atomic<bool> finalized_{false};
  std::atomic<bool> has_transfered_{false};
  std::atomic<bool> unavailable_{false};
  bool fail_fast_enabled_{true};

  int32_t fd_ = -1;
  RecvState recv_state_ = RecvState::WAITING_FOR_HEADER;
  std::vector<char> recv_buffer_;
  size_t expected_body_size_ = 0;
  size_t bytes_received_ = 0;

  // lock for push/fetch items from notify_messages_
  std::mutex notify_message_mutex_;
  std::vector<NotifyMsg> notify_messages_;

  friend class ChannelManager;
  // Single link-level lock: serializes submit, sync, async poll, slot abort, and disconnect teardown so
  // concurrent threads never share or abort the one channel stream mid-issue.
  std::mutex device_launch_mu_;
  // Guards active_slot_ (the one shared slot bound to this channel) and its ref-count transitions.
  std::mutex active_slot_mu_;
  std::shared_ptr<SlotHandle> active_slot_;

  std::mutex transfer_reqs_mutex_;
  std::unordered_map<uint64_t, AsyncRecord> req_2_async_record_;
  TransferSlotPool *slot_pool_ = nullptr;
};
using ChannelPtr = std::shared_ptr<CommChannel>;
}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_H_
