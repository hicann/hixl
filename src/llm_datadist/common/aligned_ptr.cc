/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.

 * The code snippet comes from Huawei's open-source Ascend project.
 * Copyright 2020 Huawei Technologies Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#include "aligned_ptr.h"
#include "mem_utils.h"
#include "def_types.h"
#include "common/llm_log.h"

namespace llm {
AlignedPtr::AlignedPtr(const size_t buffer_size, const size_t alignment) {
  size_t alloc_size = buffer_size;
  if (alignment > 0U) {
    alloc_size = buffer_size + alignment - 1U;
  }
  if ((buffer_size == 0U) || (alloc_size < buffer_size)) {
    LLMLOGW("[Allocate][Buffer] Allocate empty buffer or overflow, size=%zu, alloc_size=%zu", buffer_size, alloc_size);
    return;
  }

  base_ =
    std::unique_ptr<uint8_t[], AlignedPtr::Deleter>(new (std::nothrow) uint8_t[alloc_size], [](const uint8_t *ptr) {
    delete[] ptr;
    ptr = nullptr;
  });
  if (base_ == nullptr) {
    LLMLOGW("[Allocate][Buffer] Allocate buffer failed, size=%zu", alloc_size);
    return;
  }

  if (alignment == 0U) {
    aligned_addr_ = base_.get();
  } else {
    const size_t offset = alignment - 1U;
    aligned_addr_ =
        PtrToPtr<void, uint8_t>(ValueToPtr((PtrToValue(PtrToPtr<uint8_t, void>(base_.get())) + offset) & ~offset));
  }
}
}  // namespace llm
