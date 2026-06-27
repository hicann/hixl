/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_HIXL_SRC_OPS_HIXL_KERNEL_TRANSFER_CONTEXT_MANAGER_H_
#define CANN_HIXL_SRC_OPS_HIXL_KERNEL_TRANSFER_CONTEXT_MANAGER_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "common/hixl_inner_types.h"

namespace hixl {

struct TransferContext {
  TransferContext() = default;
  TransferContext(const TransferContext &) = delete;
  TransferContext &operator=(const TransferContext &) = delete;

  TransferThreadState GetState() const {
    return state.load(std::memory_order_acquire);
  }

  void SetState(TransferThreadState new_state) {
    state.store(new_state, std::memory_order_release);
  }

  void lock() {
    while (spin_lock.test_and_set(std::memory_order_acquire)) {
    }
  }

  bool try_lock() {
    return !spin_lock.test_and_set(std::memory_order_acquire);
  }

  void unlock() {
    spin_lock.clear(std::memory_order_release);
  }

  std::atomic_flag spin_lock = ATOMIC_FLAG_INIT;
  std::atomic<TransferThreadState> state{TRANSFER_THREAD_STATE_INITIALIZED};
};

class TransferContextManager {
 public:
  static TransferContextManager &Instance();

  std::shared_ptr<TransferContext> Get(ThreadHandle thread);
  TransferThreadState Add(ThreadHandle thread);
  TransferThreadState Delete(ThreadHandle thread);

 private:
  TransferContextManager() = default;

  std::mutex mutex_;
  std::unordered_map<ThreadHandle, std::shared_ptr<TransferContext>> contexts_;
};

uint32_t DoSyncTransferContext(TransferContextSyncParam *param);

}  // namespace hixl

#endif  // CANN_HIXL_SRC_OPS_HIXL_KERNEL_TRANSFER_CONTEXT_MANAGER_H_
