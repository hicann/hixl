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

namespace hixl {

TransferContextManager &TransferContextManager::Instance() {
  static TransferContextManager manager;
  return manager;
}

std::shared_ptr<TransferContext> TransferContextManager::Get(ThreadHandle thread) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = contexts_.find(thread);
  if (it == contexts_.end()) {
    return nullptr;
  }
  return it->second;
}

HixlTransferThreadState TransferContextManager::Add(ThreadHandle thread, uint32_t user_stream_id, uint32_t notify_id,
                                                    uint64_t err_flag_dev_va) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto &ctx = contexts_[thread];
  if (ctx == nullptr) {
    ctx = std::make_shared<TransferContext>();
  }
  ctx->SetState(TRANSFER_THREAD_STATE_INITIALIZED);
  ctx->user_stream_id = user_stream_id;
  ctx->notify_id = notify_id;
  ctx->err_flag_dev_va = err_flag_dev_va;

  return TRANSFER_THREAD_STATE_INITIALIZED;
}

HixlTransferThreadState TransferContextManager::Delete(ThreadHandle thread) {
  std::lock_guard<std::mutex> lock(mutex_);
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

  return TRANSFER_THREAD_STATE_DELETED;
}

}  // namespace hixl
