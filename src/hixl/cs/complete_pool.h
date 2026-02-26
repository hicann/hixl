/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_HIXL_SRC_HIXL_COMMON_COMPLETE_POOL_H_
#define CANN_HIXL_SRC_HIXL_COMMON_COMPLETE_POOL_H_

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

#include "acl/acl.h"
#include "common/hixl_checker.h"
#include "cs/hixl_cs.h"
#include "hcomm_compat.h"
#include "runtime/runtime/rt.h"

#include <deque>

namespace hixl {

class CompletePool {
 public:
  static constexpr uint32_t kMaxSlots = 128U;
  static constexpr uint32_t kNotifyTagSize = 64U;
  struct SlotHandle {
    uint32_t slot_index;
    aclrtContext ctx;
    aclrtStream stream;
    ThreadHandle thread;
    aclrtNotify notify;
    void *host_flag;
    std::array<char, kNotifyTagSize> notify_tag;
    uint64_t notify_addr;
    uint32_t notify_len;
  };

  CompletePool();
  ~CompletePool();
  CompletePool(const CompletePool &) = delete;
  CompletePool &operator=(const CompletePool &) = delete;
  Status AddRefAndInitIfNeeded(int32_t device_id, CommEngine engine, uint32_t thread_num,
                               uint32_t notify_num_per_thread);
  void ReleaseRefAndDeinitIfNeeded();
  Status Acquire(SlotHandle *handle);
  void Release(uint32_t slot_index);
  bool IsComplete(const SlotHandle &handle) const;
  void ResetHostFlag(const SlotHandle &handle) const;
  uint32_t GetInUseCount() const;
  Status GetSlotNotifyInfo(uint32_t slot_index, uint64_t &notify_addr, uint32_t &notify_len,
                           std::array<char, kNotifyTagSize> &tag) const;

 private:
  struct Slot {
    bool in_use;
    aclrtContext ctx;
    aclrtStream stream;
    ThreadHandle thread;
    aclrtNotify notify;
    uint64_t notify_addr;
    uint32_t notify_len;
    void *host_flag;
    std::array<char, kNotifyTagSize> notify_tag;
  };

 private:
  bool IsInitedParamsSame(int32_t device_id, CommEngine engine, uint32_t thread_num,
                          uint32_t notify_num_per_thread) const;
  void SaveInitParams(int32_t device_id, CommEngine engine, uint32_t thread_num, uint32_t notify_num_per_thread);
  void ResetInitParamsLocked();
  void InitFreeListLocked();
  Status InitOneSlotLocked(Slot &slot, uint32_t slot_index, int32_t device_id, CommEngine engine, uint32_t thread_num,
                           uint32_t notify_num_per_thread);
  Status EnsureNotifyRecordLocked(Slot &slot, uint32_t slot_index);
  void ResetNotifyResourcesLocked(Slot &slot);
  Status CreateNotifyLocked(Slot &slot, uint32_t &notify_id);
  Status GetNotifyAddrLocked(uint32_t notify_id, uint64_t &notify_addr, uint32_t &notify_len) const;
  Status BuildNotifyTagLocked(uint32_t slot_index, std::array<char, kNotifyTagSize> &tag) const;
  Status InitAllSlotsLocked(int32_t device_id, CommEngine engine, uint32_t thread_num, uint32_t notify_num_per_thread);
  void DeinitAllSlotsLocked();
  Status EnsureContextLocked(Slot &slot, int32_t device_id);
  Status EnsureStreamLocked(Slot &slot);
  Status EnsureThreadLocked(Slot &slot, CommEngine engine, uint32_t thread_num, uint32_t notify_num_per_thread);
  Status EnsurePinnedHostFlagLocked(Slot &slot);
  void DestroySlotLocked(Slot &slot);

 private:
  mutable std::mutex mu_;
  uint32_t ref_cnt_;
  bool inited_;
  std::deque<uint32_t> free_list_;
  std::array<Slot, kMaxSlots> slots_;
  int32_t init_device_id_;
  CommEngine init_engine_;
  uint32_t init_thread_num_;
  uint32_t init_notify_num_per_thread_;
};

CompletePool &GetCompletePool();

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_COMPLETE_POOL_H_