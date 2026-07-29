/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "transfer_context_manager.h"

#include "common/hixl_log.h"
#include "task_exception_handler.h"

namespace hixl {

void TransferContext::WriteErrorFlag() const {
  if (err_flag_dev_va != 0U) {
    volatile uint8_t *err_flag_ptr = reinterpret_cast<volatile uint8_t *>(static_cast<uintptr_t>(err_flag_dev_va));
    *err_flag_ptr = 1U;
    HIXL_LOGI("[TransferContext] Written err_flag=1 to va=0x%lx", err_flag_dev_va);
  }
}

TransferContextManager &TransferContextManager::Instance() {
  static TransferContextManager manager;
  return manager;
}

std::shared_ptr<TransferContext> TransferContextManager::Get(ThreadHandle thread) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = contexts_.find(thread);
  if (it == contexts_.end()) {
    return nullptr;
  }
  return it->second;
}

HixlTransferThreadState TransferContextManager::Add(ThreadHandle thread, uint32_t notify_id, uint64_t err_flag_dev_va) {
  std::unique_lock<std::mutex> lock(mutex_);
  auto &ctx = contexts_[thread];
  if (ctx == nullptr) {
    ctx = std::make_shared<TransferContext>();
  }
  ctx->SetState(TRANSFER_THREAD_STATE_INITIALIZED);
  ctx->notify_id = notify_id;
  ctx->err_flag_dev_va = err_flag_dev_va;

  HIXL_LOGI("[TransferContextManager] add transfer context success. thread=%lu notify_id=%u err_flag_dev_va=0x%lx",
            static_cast<uint64_t>(thread), notify_id, static_cast<uint64_t>(err_flag_dev_va));
  lock.unlock();

  // 防止hcomm接口内回调hixl触发Get导致死锁，所以这里放到锁外面
  TaskExceptionHandler::Instance().EnableExceptionCallback();

  return TRANSFER_THREAD_STATE_INITIALIZED;
}

HixlTransferThreadState TransferContextManager::Delete(ThreadHandle thread) {
  bool need_disable = false;
  std::unique_lock<std::mutex> lock(mutex_);
  auto it = contexts_.find(thread);
  if (it == contexts_.end() || it->second == nullptr) {
    return TRANSFER_THREAD_STATE_DELETED;
  }

  auto ctx = it->second;
  ctx->SetState(TRANSFER_THREAD_STATE_DELETING);
  if (!ctx->try_lock()) {
    return TRANSFER_THREAD_STATE_DELETING;
  }
  ctx->SetState(TRANSFER_THREAD_STATE_DELETED);
  contexts_.erase(it);
  ctx->unlock();

  HIXL_LOGI("[TransferContextManager] delete transfer context success. thread=%lu", static_cast<uint64_t>(thread));
  need_disable = contexts_.empty();
  lock.unlock();

  if (need_disable) {
    // 防止hcomm接口内回调hixl触发Get导致死锁，所以这里放到锁外面
    TaskExceptionHandler::Instance().DisableExceptionCallback();
  }

  return TRANSFER_THREAD_STATE_DELETED;
}

}  // namespace hixl
