/**
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_LLMDATADIST_ADXL_TRANSFER_SLOT_POOL_H
#define HIXL_SRC_LLMDATADIST_ADXL_TRANSFER_SLOT_POOL_H

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>
#include "acl/acl.h"
#include "adxl/adxl_types.h"

namespace adxl {
// A context-centric slot. Each slot owns its own aclrtContext and that context's default stream, configured with
// ACL_STOP_ON_FAILURE ("stop on failure"). The pool-level dev_const_one is shared by all slots as the device source
// for the host_flag device-to-host completion copy used by the asynchronous transfer path.
struct SlotHandle {
  int32_t device_id = -1;
  uint32_t slot_index = 0U;
  aclrtContext ctx = nullptr;
  aclrtStream stream = nullptr;
  void *dev_const_one = nullptr;
};

// Replaces StreamPool. Hands out context-centric slots that each carry a "stop on failure" default stream, so an
// error on one transfer halts the stream instead of corrupting subsequent ops. A channel borrows a single slot and
// shares it across its in-flight batch; the slot returns to the pool when the batch finishes (Release) or is torn
// down on failure (Abort).
class TransferSlotPool {
 public:
  TransferSlotPool(int32_t device_id, size_t max_slot_num) : device_id_(device_id), max_slot_num_(max_slot_num) {}
  ~TransferSlotPool();

  TransferSlotPool(const TransferSlotPool &) = delete;
  TransferSlotPool &operator=(const TransferSlotPool &) = delete;

  Status Initialize();
  void Finalize();
  // Borrow a slot. Lazily creates a context + its default stream the first time a slot index is used, bounded by
  // max_slot_num_.
  Status Acquire(SlotHandle *handle);
  // Return a slot to the free list for reuse (success path).
  void Release(const SlotHandle &handle);
  // Abort the slot stream (signal in-flight aicpu task expansion to stop), tear down its context+stream so a reused
  // slot starts clean, then return it to the free list (failure path).
  void Abort(const SlotHandle &handle);
  // Borrow a per-request host-pinned completion flag tied to the slot index (recycled via ReleaseHostFlag).
  Status AcquireHostFlag(const SlotHandle &handle, void *&host_flag);
  void ReleaseHostFlag(const SlotHandle &handle, void *host_flag);

 private:
  struct Slot {
    bool in_use = false;
    aclrtContext ctx = nullptr;
    aclrtStream stream = nullptr;
    std::vector<void *> host_flag_free_list;
  };
  Status InitSlotLocked(Slot &slot) const;
  void DestroySlotLocked(Slot &slot);
  Status EnsureDevConstOneLocked();
  void FillHandleLocked(uint32_t index, const Slot &slot, SlotHandle *handle) const;
  void DestroySlotHostFlagsLocked(Slot &slot);

  std::mutex mu_;
  int32_t device_id_;
  size_t max_slot_num_;
  bool inited_ = false;
  std::vector<Slot> slots_;
  std::deque<uint32_t> free_list_;
  void *dev_const_one_ = nullptr;
};
}  // namespace adxl

#endif  // HIXL_SRC_LLMDATADIST_ADXL_TRANSFER_SLOT_POOL_H
