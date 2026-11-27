/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "engine/transfer_request_manager.h"
#include <limits>
#include <new>
#include "common/hixl_checker.h"

namespace hixl {
std::atomic<uintptr_t> TransferRequestManager::next_id_{1U};

Status TransferRequestManager::AllocateId(TransferReq &req) {
  auto id = next_id_.load(std::memory_order_relaxed);
  // Reserve the maximum value as a permanent exhaustion marker to prevent wraparound.
  while (id != std::numeric_limits<uintptr_t>::max()) {
    if (next_id_.compare_exchange_weak(id, id + 1U, std::memory_order_relaxed)) {
      // TransferReq is an opaque token. This value must never be dereferenced.
      req = reinterpret_cast<TransferReq>(id);
      return SUCCESS;
    }
  }
  HIXL_LOGE(RESOURCE_EXHAUSTED, "Transfer request ID space is exhausted");
  return RESOURCE_EXHAUSTED;
}

Status TransferRequestManager::Add(const TransferRequest &request, TransferReq &req) {
  req = nullptr;
  TransferReq id = nullptr;
  HIXL_CHK_STATUS_RET(AllocateId(id), "Failed to allocate transfer request ID");
  try {
    requests_.emplace(id, request);
    req = id;
  } catch (const std::bad_alloc &) {
    HIXL_LOGE(RESOURCE_EXHAUSTED, "Failed to allocate transfer request record");
    return RESOURCE_EXHAUSTED;
  }
  return SUCCESS;
}

const TransferRequest *TransferRequestManager::Get(TransferReq req) const {
  auto it = requests_.find(req);
  return it == requests_.end() ? nullptr : &it->second;
}

void TransferRequestManager::Remove(TransferReq req) {
  requests_.erase(req);
}

bool TransferRequestManager::Empty() const {
  return requests_.empty();
}

void TransferRequestManager::Clear() {
  requests_.clear();
}
}  // namespace hixl
