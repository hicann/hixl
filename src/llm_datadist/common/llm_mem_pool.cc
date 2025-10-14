/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "llm_mem_pool.h"
#include "nlohmann/json.hpp"
#include "common/llm_log.h"
#include "runtime/rt.h"

namespace llm {
ge::MemBlock *LlmMemPool::LlmMemAllocator::Malloc(size_t size) {
  LLMLOGI("Try malloc size:%zu.", size);
  return scalable_allocator_->Alloc(*this, size);
}

void LlmMemPool::LlmMemAllocator::Free(ge::MemBlock *block) {
  scalable_allocator_->Free(block);
}

void LlmMemPool::LlmMemAllocator::SetScalableAllocator(ScalableAllocator *scalable_allocator) {
  scalable_allocator_ = scalable_allocator;
}

LlmMemPool::LlmMemPool(const ScalableConfig &config)
    : scalable_allocator_(span_allocator_, config) {
}

LlmMemPool::~LlmMemPool() {
  LLMLOGI("Destroyed, unfree count = %zu", addr_to_mem_block_.size());
  for (const auto &addr_and_mem_block : addr_to_mem_block_) {
    addr_and_mem_block.second->Free();
  }
}

ge::Status LlmMemPool::Initialize(void *base_addr, size_t size) {
  constexpr size_t kMinPageShift = 10;
  constexpr size_t kMaxPageShift = 30;
  const auto page_shift = scalable_allocator_.GetScalableConfig().page_idem_num;
  LLM_CHK_BOOL_RET_STATUS(((page_shift >= kMinPageShift) && (page_shift <= kMaxPageShift)), ge::LLM_PARAM_INVALID,
                         "page_shift (%zu) out of range: [10, 31)", page_shift);
  LLM_CHK_BOOL_RET_STATUS((1UL << page_shift) <= size, ge::LLM_PARAM_INVALID,
                         "Check page_size <= memory_size failed, page_shift = %zu, page_size = %zu, memory_size = %lu",
                         page_shift, (1UL << page_shift), size);
  allocator_.SetScalableAllocator(&scalable_allocator_);
  LLM_CHK_STATUS_RET(scalable_allocator_.InitFixSizedAllocator(allocator_, base_addr, size));
  return ge::SUCCESS;
}

void *LlmMemPool::Alloc(size_t size) {
  std::lock_guard<std::mutex> lk(mu_);
  auto span = allocator_.Malloc(size);
  void *memory = nullptr;
  if (span != nullptr) {
    memory = span->GetAddr();
    addr_to_mem_block_[memory] = span;
    LLMLOGI("alloc memory success, size = %zu", size);
  }
  return memory;
}

void LlmMemPool::Free(void *addr) {
  std::lock_guard<std::mutex> lk(mu_);
  const auto it = addr_to_mem_block_.find(addr);
  if (it != addr_to_mem_block_.cend()) {
    LLMLOGI("free memory, size = %zu", it->second->GetSize());
    it->second->Free();
    addr_to_mem_block_.erase(it);
    cv_.notify_all();
  }
}

void *LlmMemPool::Alloc(size_t size, int32_t timeout_in_ms) {
  void *addr = Alloc(size);
  if (addr != nullptr) {
    return addr;
  }
  const auto tp_end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_in_ms);
  while (addr == nullptr) {
    std::unique_lock<std::mutex> lk(mu_cv_);
    if (cv_.wait_until(lk, tp_end) == std::cv_status::timeout) {
      LLMLOGW("waiting for idle memory within %d ms timed out", timeout_in_ms);
      break;
    }
    LLMLOGI("wait success, retry");
    addr = Alloc(size);
  }
  return addr;
}

std::shared_ptr<void> LlmMemPool::MakeShared(void *addr) {
  std::shared_ptr<void> tensor(addr, [this](void *address) -> void {
    if (address != nullptr) {
      Free(address);
    }
  });
  return tensor;
}

std::shared_ptr<void> LlmMemPool::AllocShared(size_t size) {
  return MakeShared(Alloc(size));
}

std::shared_ptr<void> LlmMemPool::AllocShared(size_t size, int32_t timeout_in_ms) {
  return MakeShared(Alloc(size, timeout_in_ms));
}

void LlmMemPool::LogPoolState() {
  scalable_allocator_.PrintDetails(DLOG_ERROR);
}
}  // namespace llm