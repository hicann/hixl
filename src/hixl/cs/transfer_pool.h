/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_HIXL_SRC_HIXL_CS_TRANSFER_POOL_H_
#define CANN_HIXL_SRC_HIXL_CS_TRANSFER_POOL_H_

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>
#include "acl/acl.h"
#include "common/hixl_checker.h"
#include "cs/hixl_cs.h"
#include "hcomm/hcomm_res_defs.h"
#include "runtime/runtime/rt.h"

namespace hixl {

class TransferPool {
 public:
  static constexpr uint32_t kMaxPoolSize = 4096U;
  static TransferPool &GetInstance(int32_t device_id);

  struct SlotHandle {
    int32_t device_id;
    uint32_t slot_index;
    aclrtContext ctx;
    aclrtStream stream;
    ThreadHandle thread;
    aclrtNotify notify;
    void *host_flag;
  };

  TransferPool(const TransferPool &) = delete;
  TransferPool &operator=(const TransferPool &) = delete;

  Status Initialize(uint32_t pool_size);
  void Finalize();
  Status Acquire(SlotHandle *handle);
  void Release(const SlotHandle &handle);
  void Abort(const SlotHandle &handle);
  Status GetAllSlots(std::vector<SlotHandle> &out) const;

  ~TransferPool();

 private:
  explicit TransferPool(int32_t device_id);

  struct Slot {
    bool in_use;
    aclrtContext ctx;
    aclrtStream stream;
    ThreadHandle thread;
    aclrtNotify notify;
    void *host_flag;
  };

  void InitFreeListLocked();
  Status InitOneSlotLocked(Slot &slot, uint32_t slot_index);
  static Status EnsureNotifyLocked(Slot &slot);
  static void ResetNotifyResourcesLocked(Slot &slot);
  static Status CreateNotifyLocked(Slot &slot, uint32_t &notify_id);
  Status InitAllSlotsLocked();
  void RollbackInitLocked(uint32_t failed_index);
  void DeinitAllSlotsLocked();
  Status EnsureContextLocked(Slot &slot);
  Status EnsureDefaultStreamLocked(Slot &slot);
  Status EnsureThreadLocked(Slot &slot);
  Status EnsurePinnedHostFlagLocked(Slot &slot);
  void DestroySlotLocked(Slot &slot);
  void AbortSlotByIndexLocked(uint32_t slot_index);
  static void FillHandleFromSlot(int32_t device_id, uint32_t index, const Slot &slot, SlotHandle *handle);

  mutable std::mutex mu_{};
  const int32_t device_id_;
  uint32_t ref_cnt_;
  bool inited_;
  uint32_t pool_size_;
  std::deque<uint32_t> free_list_;
  std::vector<Slot> slots_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_TRANSFER_POOL_H_
