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
  // Must be 1 when enable_aicpu_unfold is true.
  size_t task_stream_num{0U};
  std::string local_engine;
  bool auto_connect{false};
  FabricMemStatistic *statistic{nullptr};
  FabricMemLocalMemory *local_memory{nullptr};
  FabricMemControlServer *control_server{nullptr};
  aclrtContext aclrt_context{nullptr};
  // Selects FabricMemAicpuTransferService when true; FabricMemHostTransferService otherwise.
  bool enable_aicpu_unfold{false};
};

// Base for fabric_mem transfer services. Owns the channel manager and slot pool; concrete
// Host/AICPU subclasses implement distinct concurrency models and copy submission paths.
class FabricMemTransferService {
 public:
  FabricMemTransferService() = default;
  virtual ~FabricMemTransferService();
  FabricMemTransferService(const FabricMemTransferService &) = delete;
  FabricMemTransferService &operator=(const FabricMemTransferService &) = delete;
  FabricMemTransferService(FabricMemTransferService &&) = delete;
  FabricMemTransferService &operator=(FabricMemTransferService &&) = delete;

  virtual Status Initialize(const FabricMemTransferServiceInitParam &param) = 0;
  virtual void Finalize();

  Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis);
  Status EnsureConnected(const AscendString &remote_engine, int32_t timeout_in_millis);
  Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis);
  void DisconnectAll();
  bool HasChannels() const;
  bool IsConnected(const std::string &remote_engine) const;
  Status StartKeepaliveMonitor();
  void StopKeepaliveMonitor();

  virtual Status TransferSync(const std::string &remote_engine, TransferOp operation,
                              const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) = 0;
  virtual Status TransferAsync(const std::string &remote_engine, TransferOp operation,
                               const std::vector<TransferOpDesc> &op_descs, TransferReq &req) = 0;
  virtual Status GetTransferStatus(const TransferReq &req, TransferStatus &status,
                                   AsyncTransferPollInfo *info = nullptr) = 0;
  virtual void CleanupAsyncTransfer(const TransferReq &req) = 0;

  static void SetKeepaliveCheckIntervalMs(int64_t interval_ms);
  static Status MallocMem(MemType type, size_t size, void **ptr);
  static Status FreeMem(void *ptr);
  static Status ExportToShareableHandle(void *addr, aclrtMemFabricHandle &share_handle);

 protected:
  enum class AsyncStreamQueryResult { kWaiting, kFailed, kComplete };

  struct TransferInvocation {
    TransferOp operation{READ};
    uint64_t req_id{0U};
    uint64_t prof_start_time{0U};
    uint32_t rtsq_timeout_ms{0U};
    uint64_t transfer_bytes{0U};
    uint64_t op_desc_count{0U};
    std::chrono::steady_clock::time_point transfer_start;
    std::chrono::steady_clock::time_point real_copy_start;
  };

  Status InitCommon(const FabricMemTransferServiceInitParam &param, bool enable_aicpu_unfold);
  Status InitDevConstOne();
  void FreeDevConstOne();

  Status PrepareChannelTransfer(const std::string &remote_engine, std::shared_ptr<FabricMemChannel> &channel,
                                FabricMemTransferContext &context) const;
  Status WaitControlStreamsWithTimeout(const AsyncSlot &slot, const std::chrono::steady_clock::time_point &start,
                                       uint64_t timeout_us) const;
  Status AppendHostFlagCopies(const AsyncSlot &slot) const;
  static bool AllHostFlagsDone(const AsyncSlot &slot);
  static AsyncStreamQueryResult QueryAsyncSlotStreams(const AsyncSlot &slot);
  static void FillPollInfo(const AsyncRecord &record, AsyncTransferPollInfo *info);

  Status ResolveTransferAddrs(std::vector<TransferOpDesc> &op_descs, const FabricMemTransferContext &context) const;
  static Status TransOpAddr(uintptr_t old_addr, size_t len,
                            const std::unordered_map<uintptr_t, VaInfo> &new_va_to_old_va, uintptr_t &new_addr);
  Status NeedTransLocalAddr(const std::vector<TransferOpDesc> &op_descs, bool &need_trans_local_addr) const;
  void UpdateStats(const std::string &channel_id, const std::string &statistic_channel_id,
                   const std::shared_ptr<FabricMemTransferStatisticInfo> &stat_info, uint64_t transfer_cost,
                   uint64_t real_copy_cost, uint64_t transfer_bytes, uint64_t op_desc_count) const;
  static uint64_t GetTransferBytes(const std::vector<TransferOpDesc> &op_descs);
  static uint64_t GetDurationUs(const std::chrono::steady_clock::time_point &start,
                                const std::chrono::steady_clock::time_point &end);

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
