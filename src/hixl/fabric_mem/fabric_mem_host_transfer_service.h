/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_HOST_TRANSFER_SERVICE_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_HOST_TRANSFER_SERVICE_H_

#include "fabric_mem/fabric_mem_transfer_service.h"

namespace hixl {

// Host unfold path: multi-slot concurrent transfers per channel via submit_gate (shared_mutex)
// and aclrtMemcpyAsync on control streams. Logic mirrors upstream FabricMemTransferService.
class FabricMemHostTransferService : public FabricMemTransferService {
 public:
  FabricMemHostTransferService() = default;
  ~FabricMemHostTransferService() override = default;

  Status Initialize(const FabricMemTransferServiceInitParam &param) override;

  Status TransferSync(const std::string &remote_engine, TransferOp operation,
                      const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) override;
  Status TransferAsync(const std::string &remote_engine, TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs, TransferReq &req) override;
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status,
                           AsyncTransferPollInfo *info = nullptr) override;
  void CleanupAsyncTransfer(const TransferReq &req) override;

  static Status ProcessCopyWithAsync(const AsyncSlot &slot, TransferOp operation,
                                     const std::vector<TransferOpDesc> &op_descs);

 private:
  Status IssueSyncCopy(const std::shared_ptr<FabricMemChannel> &channel, const AsyncSlot &slot,
                       const FabricMemTransferContext &context, std::vector<TransferOpDesc> &op_descs,
                       TransferInvocation &invocation) const;
  Status IssueAsyncCopyAndRegister(const std::shared_ptr<FabricMemChannel> &channel, AsyncSlot &slot,
                                   const FabricMemTransferContext &context, std::vector<TransferOpDesc> &op_descs,
                                   TransferInvocation &invocation);
  static void UnregisterSyncSlot(const std::shared_ptr<FabricMemChannel> &channel, const AsyncSlot &slot);
  static Status SynchronizeAsyncSlotStreams(const AsyncSlot &slot);
  Status HandleAsyncStreamQueryFailure(uint64_t req_id, AsyncRecord &async_record, TransferStatus &status);
  Status CompleteAsyncTransferAndUpdateStats(uint64_t req_id, AsyncRecord &async_record, TransferStatus &status);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_HOST_TRANSFER_SERVICE_H_
