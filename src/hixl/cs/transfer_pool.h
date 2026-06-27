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
#include "common/hixl_utils.h"
#include "load_kernel.h"
#include "rt_external.h"

namespace hixl {

class TransferPool {
 public:
  static constexpr uint32_t kMaxPoolSize = 4096U;
  static TransferPool *GetInstance(int32_t device_id);

  struct SlotHandle {
    int32_t device_id;
    uint32_t slot_index;
    aclrtContext ctx;
    aclrtStream stream;
    ThreadHandle thread;
    aclrtNotify notify;
    void *dev_const_one;
    uint32_t notify_id;
  };

  TransferPool(const TransferPool &) = delete;
  TransferPool &operator=(const TransferPool &) = delete;

  Status Initialize(uint32_t pool_size);
  void Finalize();
  Status Acquire(SlotHandle *handle);
  void Release(const SlotHandle &handle);
  void Abort(const SlotHandle &handle);
  Status GetAllSlots(std::vector<SlotHandle> &out) const;
  aclrtContext GetContext() const;
  aclrtFuncHandle GetDeviceKernelFunc(bool is_get) const;

  explicit TransferPool(int32_t device_id);
  ~TransferPool();

 private:
  struct Slot {
    bool in_use;
    aclrtContext ctx;
    aclrtStream stream;
    ThreadHandle thread;
    aclrtNotify notify;
    uint32_t notify_id;
  };

  void InitFreeListLocked();
  Status InitOneSlotLocked(Slot &slot, uint32_t slot_index) const;
  static Status EnsureNotifyLocked(Slot &slot);
  static void ResetNotifyResourcesLocked(Slot &slot);
  static Status CreateNotifyLocked(Slot &slot);
  Status InitAllSlotsLocked();
  void DeinitAllSlotsLocked();
  Status EnsureContextLocked(Slot &slot) const;
  Status EnsureDefaultStreamLocked(Slot &slot) const;
  Status EnsureThreadLocked(Slot &slot) const;
  Status DestroySlotLocked(Slot &slot, bool sync_context = true) const;

  void AbortInUseStreamsLocked() const;
  static void AbortInUseStreamLocked(const Slot &slot);
  void AbortSlotRuntimeLocked(Slot &slot) const;
  static void ResetAbortSlotNotifyLocked(Slot &slot);
  void DeleteSlotThreadContextForAbortLocked(Slot &slot, uint32_t slot_index) const;
  static void DestroySlotContextForAbortLocked(Slot &slot);
  Status ReinitSlotAfterAbortLocked(Slot &slot, uint32_t slot_index);
  void ReturnSlotToFreeListLocked(uint32_t slot_index);
  void AbortSlotByIndexLocked(uint32_t slot_index);

  static void FillHandleFromSlot(int32_t device_id, uint32_t index, const Slot &slot, SlotHandle *handle);
  Status EnsureDevConstOneLocked();
  Status EnsureDeviceKernelsLocked();
  Status SyncContextsLocked(const std::vector<ThreadHandle> &threads, uint32_t op, uint32_t expect_state) const;
  Status RunSyncContextOnceLocked(std::vector<TransferContextSyncEntry> &pending, uint32_t op, uint32_t expect_state,
                                  std::vector<TransferContextSyncEntry> &retry_entries,
                                  std::vector<uint32_t> &retry_states) const;
  Status CollectRetrySyncEntries(const std::vector<TransferContextSyncEntry> &entries,
                                 const std::vector<uint32_t> &states, uint32_t op, uint32_t expect_state,
                                 std::vector<TransferContextSyncEntry> &retry_entries,
                                 std::vector<uint32_t> &retry_states) const;
  Status HandleSyncContextTimeout(const std::vector<TransferContextSyncEntry> &pending,
                                  const std::vector<uint32_t> &states, uint32_t op) const;
  Status AddTransferContextsLocked() const;
  Status DeleteTransferContextsLocked(const std::vector<ThreadHandle> &threads) const;
  Status SyncOneTransferContextLocked(ThreadHandle thread, uint32_t op, uint32_t expect_state) const;
  Status LaunchSyncContextKernelLocked(const std::vector<TransferContextSyncEntry> &entries,
                                       std::vector<uint32_t> &states) const;
  static std::vector<ThreadHandle> CollectLiveThreads(const std::vector<Slot> &slots);

  mutable std::mutex mu_{};
  const int32_t device_id_;
  uint32_t ref_cnt_;
  bool inited_;
  uint32_t pool_size_;
  std::deque<uint32_t> free_list_;
  std::vector<Slot> slots_;
  void *dev_const_one_{nullptr};
  aclrtContext rts_context_{nullptr};
  aclrtBinHandle kernel_bin_handle_{nullptr};
  DeviceFuncHandles device_func_handles_{};
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_TRANSFER_POOL_H_
