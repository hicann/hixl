/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_AICPU_TRANSFER_SERVICE_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_AICPU_TRANSFER_SERVICE_H_

#include <mutex>

#include "fabric_mem/fabric_mem_aicpu_dispatcher.h"
#include "fabric_mem/fabric_mem_transfer_service.h"

namespace hixl {

// AICPU unfold path (aligned with hixl_cs): one bound slot per channel is reused across pipelined
// submits. transfer_mu serializes Acquire/Issue/Complete; TransferAsync returns after submit so the
// next async can enqueue on the same stream without waiting for GetTransferStatus. Sync holds the
// mutex for the whole transfer including stream wait. Disconnect sets disconnecting then waits for
// the mutex before aborting (no abort-during-wait).
// Every failure that reached the device - submit error, stream query failure, kernel status
// failure, cancel, disconnect - runs the same FabricMemChannelManager::AbortAicpuChannelLocked,
// which drains the channel and lets the slot pool abort the streams, exit the kernel, destroy the
// slot and only then free the buffers the kernel was reading.
class FabricMemAicpuTransferService : public FabricMemTransferService {
 public:
  FabricMemAicpuTransferService() = default;
  ~FabricMemAicpuTransferService() override;

  Status Initialize(const FabricMemTransferServiceInitParam &param) override;
  void Finalize() override;

  Status TransferSync(const std::string &remote_engine, TransferOp operation,
                      const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) override;
  Status TransferAsync(const std::string &remote_engine, TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs, TransferReq &req) override;
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status,
                           AsyncTransferPollInfo *info = nullptr) override;
  void CleanupAsyncTransfer(const TransferReq &req) override;

  Status ProcessCopyWithAsync(AsyncSlot &slot, TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                              FabricMemAicpuRequestResource &aicpu_resource, uint32_t rtsq_timeout_ms = 0U);

 private:
  // Caller must hold channel->transfer_mu. When bound_slot_refs > 0, reuses channel->bound_slot and
  // ignores timeout_us / async_acquire (only the first acquire waits on the pool).
  Status AcquireBoundSlotLocked(const std::shared_ptr<FabricMemChannel> &channel, AsyncSlot &slot, uint64_t timeout_us,
                                bool async_acquire);
  // Success-path release: drops one reference and returns the slot to the pool once it is idle.
  void ReleaseBoundSlotLocked(const std::shared_ptr<FabricMemChannel> &channel, AsyncSlot &slot);
  // The single failure entry point: aborts the channel once the request reached the device,
  // otherwise just gives the bound slot back. Caller must hold channel->transfer_mu.
  void CleanupFailedTransferLocked(const std::shared_ptr<FabricMemChannel> &channel, AsyncSlot &slot,
                                   FabricMemAicpuRequestResource &aicpu_resource);
  static Status AllocateTransferHostFlags(AsyncSlot &slot);
  Status IssueCopyLocked(const std::shared_ptr<FabricMemChannel> &channel, AsyncSlot &slot,
                         const FabricMemTransferContext &context, std::vector<TransferOpDesc> &op_descs,
                         TransferInvocation &invocation, FabricMemAicpuRequestResource &aicpu_resource);
  void RegisterAsyncTransferRecord(uint64_t req_id, const std::shared_ptr<FabricMemChannel> &channel,
                                   const FabricMemTransferContext &context, const TransferInvocation &invocation,
                                   AsyncSlot &slot, FabricMemAicpuRequestResource &aicpu_resource);
  Status CompleteSyncTransferLocked(const std::shared_ptr<FabricMemChannel> &channel, AsyncSlot &slot,
                                    FabricMemAicpuRequestResource &aicpu_resource,
                                    const FabricMemTransferContext &context, const TransferInvocation &invocation);
  Status HandleAsyncStreamQueryFailure(uint64_t req_id, const std::shared_ptr<FabricMemChannel> &channel,
                                       AsyncRecord &async_record, TransferStatus &status);
  Status CompleteAsyncTransferAndUpdateStats(uint64_t req_id, const std::shared_ptr<FabricMemChannel> &channel,
                                             AsyncRecord &async_record, TransferStatus &status);

  FabricMemAicpuDispatcher aicpu_dispatcher_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_AICPU_TRANSFER_SERVICE_H_
