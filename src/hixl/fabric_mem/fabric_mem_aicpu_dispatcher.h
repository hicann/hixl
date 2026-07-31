/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_AICPU_DISPATCHER_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_AICPU_DISPATCHER_H_

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "acl/acl_rt.h"
#include "common/hixl_inner_types.h"
#include "fabric_mem/fabric_mem_types.h"

namespace hixl {

// Host-side launcher for the standalone FabricMem AICPU package. It only
// uploads resolved VMM descriptors and enqueues an AICPU operator; the device
// binary owns direct A3 RTSQ/SDMA submission without communication-stack dependencies.
//
// Concurrency: the dispatcher holds no lock across submission. Everything that targets one channel
// (its bound slot, worker RTSQ and TransferContext) is already serialized by
// FabricMemChannel::transfer_mu, and distinct channels own distinct slots, so they dispatch in
// parallel. Only the per-RTSQ task-id counters need a lock, and it is a leaf. Initialize / Finalize
// bracket all transfers and must not overlap with them.
class FabricMemAicpuDispatcher {
 public:
  FabricMemAicpuDispatcher() = default;
  ~FabricMemAicpuDispatcher();
  FabricMemAicpuDispatcher(const FabricMemAicpuDispatcher &) = delete;
  FabricMemAicpuDispatcher &operator=(const FabricMemAicpuDispatcher &) = delete;

  Status Initialize(int32_t device_id);
  void Finalize();
  bool IsInitialized() const;

  // The caller owns resource for the full stream lifetime. On failure the resource stays allocated
  // and must go through FabricMemSlotPool::AbortSlot, never a bare ReleaseRequestResource.
  Status Submit(const AsyncSlot &slot, TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                FabricMemAicpuRequestResource &resource, uint32_t rtsq_timeout_ms = 0U);
  // Reads per-launch AICPU status words after notify-ordered completion.
  static Status CheckRequestStatus(const FabricMemAicpuRequestResource &resource);
  static void ReleaseRequestResource(FabricMemAicpuRequestResource &resource);

  // Register / try-free TransferContext for a bound AICPU slot (aligns with hixl_cs SyncTransferContext).
  // AddTransferContext sets slot.transfer_ctx_key from notifies[0] and launches OP_ADD.
  Status AddTransferContext(AsyncSlot &slot);
  Status DeleteTransferContext(const AsyncSlot &slot);

 private:
  Status InitializeRtsqDevice();
  Status LaunchKernel(aclrtFuncHandle function, aclrtStream stream, const FabricMemAicpuKernelParam &param,
                      void *dev_args) const;
  Status BuildRtsqKernelParam(aclrtStream worker_stream, uint32_t task_count, FabricMemAicpuKernelParam &param);
  // Hands out `task_count` consecutive SQE task ids for one worker RTSQ.
  uint32_t ReserveRtsqTaskIds(uint32_t sq_id, uint32_t task_count);
  Status LaunchOneDescriptorBatch(const AsyncSlot &slot, aclrtFuncHandle function,
                                  FabricMemAicpuTransferDirection direction, void *descriptor_buffer,
                                  void *status_buffer, void *kernel_args_buffer, size_t begin, size_t count,
                                  size_t total_descs, uint32_t rtsq_timeout_ms, size_t &status_idx,
                                  size_t &tasks_since_notify);
  Status LaunchDescriptorBatches(const AsyncSlot &slot, const std::vector<FabricMemAicpuTransferDesc> &descs,
                                 FabricMemAicpuTransferDirection direction, void *descriptor_buffer,
                                 void *status_buffer, void *kernel_args_buffer, uint32_t rtsq_timeout_ms);
  Status SyncTransferContext(ThreadHandle key, uint32_t op, uint32_t expect_state) const;
  Status LaunchSyncContextKernel(const std::vector<HixlTransferContextSyncEntry> &entries,
                                 std::vector<uint32_t> &states) const;
  Status CollectRetrySyncEntries(const std::vector<HixlTransferContextSyncEntry> &entries,
                                 const std::vector<uint32_t> &states, uint32_t op, uint32_t expect_state,
                                 std::vector<HixlTransferContextSyncEntry> &retry_entries) const;
  Status HandleSyncContextTimeout(const std::vector<HixlTransferContextSyncEntry> &pending,
                                  const std::vector<uint32_t> &states, uint32_t op) const;

  // Serializes Initialize / Finalize against each other; never held during submission.
  std::mutex lifecycle_mutex_;
  // Leaf lock over rtsq_next_task_ids_. Two channels never share a worker RTSQ, so it only keeps
  // the map itself safe against concurrent insertion.
  std::mutex task_id_mutex_;
  std::unordered_map<uint32_t, uint32_t> rtsq_next_task_ids_;
  // Written under lifecycle_mutex_ before initialized_ is published; read-only afterwards.
  int32_t device_id_{-1};
  // Physical/host device id passed to AICPU as param.device_id.
  uint32_t rtsq_device_id_{0U};
  aclrtBinHandle binary_handle_{nullptr};
  aclrtFuncHandle batch_read_{nullptr};
  aclrtFuncHandle batch_write_{nullptr};
  aclrtFuncHandle sync_transfer_context_{nullptr};
  std::atomic<bool> initialized_{false};
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_AICPU_DISPATCHER_H_
