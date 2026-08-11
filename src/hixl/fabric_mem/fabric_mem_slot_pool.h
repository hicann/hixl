/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_SLOT_POOL_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_SLOT_POOL_H_

#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>

#include "acl/acl.h"
#include "fabric_mem/fabric_mem_types.h"
#include "hixl/hixl_types.h"

namespace hixl {

class FabricMemAicpuDispatcher;

// Bounded pool of reusable transfer slots (each slot owns a context, task streams and host completion
// flags). Acquired slots are views into pooled entries; release returns them for reuse or destroys
// them (aborting their streams). Shared by the transfer service (acquire/release during a transfer)
// and the channel manager (abort/release during disconnect).
class FabricMemSlotPool {
 public:
  FabricMemSlotPool() = default;
  ~FabricMemSlotPool();
  FabricMemSlotPool(const FabricMemSlotPool &) = delete;
  FabricMemSlotPool &operator=(const FabricMemSlotPool &) = delete;
  FabricMemSlotPool(FabricMemSlotPool &&) = delete;
  FabricMemSlotPool &operator=(FabricMemSlotPool &&) = delete;

  Status Initialize(int32_t device_id, size_t max_async_slot_num, size_t task_stream_num);
  Status AcquireWithTimeout(AsyncSlot &slot, uint64_t timeout_us);
  Status AcquireAsync(AsyncSlot &slot);
  // Lazily creates one device-only A3 RTSQ worker stream per control stream.
  // Keeping these absent from the default path preserves its existing resource footprint.
  Status EnsureAicpuRtsqStreams(AsyncSlot &slot);
  void Release(AsyncSlot &slot, bool destroy_slot);
  void AbortAndDestroyAll();
  // AICPU unfold only: lets AbortSlot force a running kernel to exit before the slot goes away.
  void SetAicpuDispatcher(FabricMemAicpuDispatcher *dispatcher);
  // The single abort path for a slot that may still carry in-flight AICPU work. Both the transfer
  // failure paths and disconnect go through it, in this fixed order:
  //   1. abort the control streams and stop the device-only RTSQ worker streams,
  //   2. delete the AICPU TransferContext so a kernel that is mid-submit exits,
  //   3. destroy the slot's streams and notifies instead of returning them to the pool,
  //   4. free the descriptor / status buffers and per-transfer host flags of `requests`,
  //   5. destroy the slot context.
  // Steps 1-2 must precede step 4: the kernel reads the descriptor buffer, and step 4 must precede
  // step 5 because the buffers were allocated under this context.
  void AbortSlot(AsyncSlot &slot, const std::vector<FabricMemAicpuPendingRequest> &requests);
  // Frees per-request descriptor / status buffers and per-transfer host flags. Only safe on its own
  // once the AICPU kernel can no longer reach them (otherwise go through AbortSlot).
  static void ReleaseRequestResources(const std::vector<FabricMemAicpuPendingRequest> &requests);
  // Stops in-flight work on a slot without returning it to the pool: aborts control
  // streams and stops device-only RTSQ worker streams (cannot aclrtStreamAbort those).
  static Status AbortSlotStreams(const AsyncSlot &slot);
  // Frees per-transfer host flags when slot.owns_host_flags is true.
  static void DestroyOwnedHostFlags(AsyncSlot &slot);

 private:
  static Status CreateSlotStream(aclrtStream &stream);
  static Status CreateAicpuRtsqStream(aclrtStream &stream);
  static void DestroySlotStreams(std::vector<aclrtStream> &streams, bool abort_streams, bool device_only = false);
  static void DestroySlotNotifies(std::vector<aclrtNotify> &notifies, std::vector<uint32_t> &notify_ids);
  static void DestroySlotHostFlags(std::vector<void *> &host_flags);
  static void ResetSlotHostFlags(const std::vector<void *> &host_flags);
  static void DestroyCreatedSlotEntry(AsyncSlot &entry);
  Status PopulateSlotStreams(AsyncSlot &entry) const;
  Status PopulateAicpuRtsqStreams(AsyncSlot &entry) const;
  Status PopulateAicpuNotifies(AsyncSlot &entry) const;
  Status PopulateSlotHostFlags(AsyncSlot &entry) const;
  Status CreateSlotEntryLocked(AsyncSlot &entry) const;
  void DestroySlotEntryLocked(AsyncSlot &entry, bool abort_streams) const;
  Status TryAcquireSlotLocked(AsyncSlot &slot);
  void RebuildFreeSlotIndicesLocked();
  bool ReleaseSlotEntryLocked(AsyncSlot &slot, bool destroy_slot);
  static void ClearReleasedSlot(AsyncSlot &slot);
  // Removes the pooled entry backing `slot` so nobody can acquire it while it is being aborted.
  bool DetachSlotEntry(const AsyncSlot &slot, AsyncSlot &entry);
  FabricMemAicpuDispatcher *GetAicpuDispatcher() const;
  // Destroys the entry's streams / notifies / pooled host flags but keeps its context alive.
  static void DestroySlotEntryResources(AsyncSlot &entry);

  mutable std::mutex pool_mutex_;
  std::condition_variable pool_cv_;
  std::vector<AsyncSlot> slot_pool_;
  std::queue<size_t> free_slot_indices_;

  int32_t device_id_{-1};
  size_t max_async_slot_num_{0};
  size_t task_stream_num_{0};
  // Set once at service initialization, before any channel exists; read by AbortSlot.
  FabricMemAicpuDispatcher *aicpu_dispatcher_{nullptr};
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_SLOT_POOL_H_
