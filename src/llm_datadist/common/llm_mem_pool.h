/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_COMMON_LLM_MEM_POOL_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_COMMON_LLM_MEM_POOL_H_

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include "memory/allocator/scalable_allocator.h"

namespace llm {
class LlmMemPool {
 public:
  explicit LlmMemPool(const ScalableConfig &config = {});
  ~LlmMemPool();
  ge::Status Initialize(void *base_addr, size_t size);
  void *Alloc(size_t size);
  void *Alloc(size_t size, int32_t timeout_in_ms);
  void Free(void *addr);

  std::shared_ptr<void> AllocShared(size_t size);
  std::shared_ptr<void> AllocShared(size_t size, int32_t timeout_in_ms);
  std::shared_ptr<void> MakeShared(void *addr);
  void LogPoolState();

 private:
  class LlmMemAllocator : public ge::Allocator {
   public:
    ge::MemBlock *Malloc(size_t size) override;
    void Free(ge::MemBlock *block) override;

    void SetScalableAllocator(ScalableAllocator *scalable_allocator);

   private:
    ScalableAllocator *scalable_allocator_;
  };

  std::mutex mu_;
  std::mutex mu_cv_;
  std::condition_variable cv_;
  SpanAllocatorImp span_allocator_;
  LlmMemAllocator allocator_;
  ScalableAllocator scalable_allocator_;
  std::map<void *, ge::MemBlock *> addr_to_mem_block_;
};
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_COMMON_LLM_MEM_POOL_H_
