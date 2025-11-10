/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef AIR_CXX_MEMORY_POOL_H
#define AIR_CXX_MEMORY_POOL_H

#include "ge/ge_allocator.h"
#include "ge_common/ge_api_types.h"
#include "memory/type/mem_size.h"

namespace llm {
class MemoryPool {
 public:
  MemoryPool() = default;
  virtual ~MemoryPool() = default;
  virtual ge::MemBlock *Alloc(ge::Allocator &allocator, const MemSize size) = 0;
  virtual void Free(ge::MemBlock *block) = 0;

  virtual const std::string &GetId() const = 0;

  virtual void PrintDetails(const int32_t level) = 0;
};
}  // namespace llm

#endif  // AIR_CXX_MEMORY_POOL_H
