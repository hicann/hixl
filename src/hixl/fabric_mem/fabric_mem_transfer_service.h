/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_TRANSFER_SERVICE_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_TRANSFER_SERVICE_H_

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "acl/acl_rt.h"
#include "fabric_mem/fabric_mem_channel_manager.h"
#include "fabric_mem/fabric_mem_control.h"
#include "fabric_mem/fabric_mem_memory.h"
#include "fabric_mem/fabric_mem_slot_pool.h"
#include "fabric_mem/fabric_mem_statistic.h"
#include "fabric_mem/fabric_mem_types.h"
#include "hixl/hixl_types.h"

namespace hixl {

struct FabricMemTransferServiceInitParam {
  int32_t device_id{-1};
  size_t max_stream_num{0U};
  size_t task_stream_num{0U};
  std::string local_engine;
  bool auto_connect{false};
  FabricMemStatistic *statistic{nullptr};
  FabricMemLocalMemory *local_memory{nullptr};
  FabricMemControlServer *control_server{nullptr};
  aclrtContext aclrt_context{nullptr};
};

// Facade for the fabric_mem subsystem. Owns the channel manager (connection lifecycle, keepalive,
// disconnect) and the slot pool (reusable transfer streams), and orchestrates sync/async transfers.
// Local memory registration/export is handled by FabricMemLocalMemory (owned by the engine) and is
// referenced here only to translate local host addresses during a transfer.
class FabricMemTransferService {
 public:
  FabricMemTransferService() = default;
  ~FabricMemTransferService();
  FabricMemTransferService(const FabricMemTransferService &) = delete;
  FabricMemTransferService &operator=(const FabricMemTransferService &) = delete;
  FabricMemTransferService(FabricMemTransferService &&) = delete;
  FabricMemTransferService &operator=(FabricMemTransferService &&) = delete;

  Status Initialize(const FabricMemTransferServiceInitParam &param);
  void Finalize();

  Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis);
  Status EnsureConnected(const AscendString &remote_engine, int32_t timeout_in_millis);
  Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis);
  void DisconnectAll();
  bool HasChannels() const;
  bool IsConnected(const std::string &remote_engine) const;
  Status StartKeepaliveMonitor();
  void StopKeepaliveMonitor();

  Status TransferSync(const std::string &remote_engine, TransferOp operation,
                      const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis);
  Status TransferAsync(const std::string &remote_engine, TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs, TransferReq &req);
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status, AsyncTransferPollInfo *info = nullptr);
  void CleanupAsyncTransfer(TransferReq req);

  static void SetKeepaliveCheckIntervalMs(int64_t interval_ms);
  static Status MallocMem(MemType type, size_t size, void **ptr);
  static Status FreeMem(void *ptr);

 private:
  enum class AsyncStreamQueryResult { kWaiting, kFailed, kComplete };

  // Per-transfer invocation metadata shared by the sync/async copy issue paths. real_copy_start is an
  // out field updated when the copy is actually issued (after address translation); the async path also
  // uses req_id/prof_start_time/transfer_start to build the AsyncRecord.
  struct TransferInvocation {
    TransferOp operation{READ};
    uint64_t req_id{0U};
    uint64_t prof_start_time{0U};
    std::chrono::steady_clock::time_point transfer_start;
    std::chrono::steady_clock::time_point real_copy_start;
  };

  Status InitDevConstOne();
  void FreeDevConstOne();

  Status PrepareChannelTransfer(const std::string &remote_engine, std::shared_ptr<FabricMemChannel> &channel,
                                FabricMemTransferContext &context);
  Status IssueSyncCopy(const std::shared_ptr<FabricMemChannel> &channel, const AsyncSlot &slot,
                       const FabricMemTransferContext &context, std::vector<TransferOpDesc> &op_descs,
                       TransferInvocation &invocation);
  Status WaitSyncStreams(const AsyncSlot &slot, const std::chrono::steady_clock::time_point &start,
                         uint64_t timeout_us) const;
  static void UnregisterSyncSlot(const std::shared_ptr<FabricMemChannel> &channel, const AsyncSlot &slot);
  Status IssueAsyncCopyAndRegister(const std::shared_ptr<FabricMemChannel> &channel, AsyncSlot &slot,
                                   const FabricMemTransferContext &context, std::vector<TransferOpDesc> &op_descs,
                                   TransferInvocation &invocation);

  Status AppendHostFlagCopies(const AsyncSlot &slot) const;
  static bool AllHostFlagsDone(const AsyncSlot &slot);
  static AsyncStreamQueryResult QueryAsyncSlotStreams(const AsyncSlot &slot);
  static Status SynchronizeAsyncSlotStreams(const AsyncSlot &slot);
  Status HandleAsyncStreamQueryFailure(uint64_t req_id, AsyncRecord &record, TransferStatus &status);
  Status CompleteAsyncTransferAndUpdateStats(uint64_t req_id, AsyncRecord &record, TransferStatus &status);
  static void FillPollInfo(const AsyncRecord &record, AsyncTransferPollInfo *info);

  Status ResolveTransferAddrs(std::vector<TransferOpDesc> &op_descs, const FabricMemTransferContext &context);
  static Status TransOpAddr(uintptr_t old_addr, size_t len,
                            const std::unordered_map<uintptr_t, VaInfo> &new_va_to_old_va, uintptr_t &new_addr);
  static Status ProcessCopyWithAsync(const AsyncSlot &slot, TransferOp operation,
                                     const std::vector<TransferOpDesc> &op_descs);
  Status NeedTransLocalAddr(const std::vector<TransferOpDesc> &op_descs, bool &need_trans_local_addr) const;
  void UpdateStats(const FabricMemTransferContext &context, uint64_t transfer_cost, uint64_t real_copy_cost,
                   uint64_t transfer_bytes, uint64_t op_desc_count) const;
  void UpdateStats(const AsyncRecord &record, uint64_t transfer_cost, uint64_t real_copy_cost, uint64_t transfer_bytes,
                   uint64_t op_desc_count) const;

  int32_t device_id_{-1};
  size_t max_stream_num_{0};
  size_t task_stream_num_{0};
  FabricMemStatistic *statistic_{nullptr};
  FabricMemLocalMemory *local_memory_{nullptr};
  void *dev_const_one_{nullptr};
  std::atomic<uint64_t> next_req_id_{1U};

  // slot_pool_ is declared before channel_manager_ so that it outlives the manager during
  // destruction (the manager's keepalive/disconnect path releases slots back to the pool).
  FabricMemSlotPool slot_pool_;
  FabricMemChannelManager channel_manager_;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_TRANSFER_SERVICE_H_
