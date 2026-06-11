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
  void Release(AsyncSlot &slot, bool destroy_slot);
  void AbortAndDestroyAll();
  // Aborts the slot's streams in place without touching the pool. Used to interrupt an in-flight
  // transfer (e.g. on disconnect) when the owning thread still holds and will release the slot.
  static void AbortSlotStreams(const AsyncSlot &slot);

 private:
  static Status CreateSlotStream(aclrtStream &stream);
  static void DestroySlotStreams(std::vector<aclrtStream> &streams, bool abort_streams);
  static void DestroySlotHostFlags(std::vector<void *> &host_flags);
  static void ResetSlotHostFlags(const std::vector<void *> &host_flags);
  static void DestroyCreatedSlotEntry(AsyncSlot &entry);
  Status PopulateSlotStreams(AsyncSlot &entry) const;
  Status PopulateSlotHostFlags(AsyncSlot &entry) const;
  Status CreateSlotEntryLocked(AsyncSlot &entry);
  void DestroySlotEntryLocked(AsyncSlot &entry, bool abort_streams);
  Status TryAcquireSlotLocked(AsyncSlot &slot);
  void RebuildFreeSlotIndicesLocked();
  bool ReleaseSlotEntryLocked(AsyncSlot &slot, bool destroy_slot);
  static void ClearReleasedSlot(AsyncSlot &slot);

  std::mutex pool_mutex_;
  std::condition_variable pool_cv_;
  std::vector<AsyncSlot> slot_pool_;
  std::queue<size_t> free_slot_indices_;

  int32_t device_id_{-1};
  size_t max_async_slot_num_{0};
  size_t task_stream_num_{0};
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_SLOT_POOL_H_
