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

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ALIGNED_PTR_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ALIGNED_PTR_H_

#include <memory>
#include <functional>

namespace llm {
class AlignedPtr {
 public:
  using Deleter = std::function<void(uint8_t *)>;
  using Allocator = std::function<void(std::unique_ptr<uint8_t[], Deleter> &base_addr)>;
  explicit AlignedPtr(const size_t buffer_size, const size_t alignment = 16U);
  AlignedPtr() = default;
  ~AlignedPtr() = default;
  AlignedPtr(const AlignedPtr &) = delete;
  AlignedPtr(AlignedPtr &&) = delete;
  AlignedPtr &operator=(const AlignedPtr &) = delete;
  AlignedPtr &operator=(AlignedPtr &&) = delete;

  const uint8_t *Get() const { return aligned_addr_; }
  uint8_t *MutableGet() { return aligned_addr_; }

 private:
  std::unique_ptr<uint8_t[], AlignedPtr::Deleter> base_ = nullptr;
  uint8_t *aligned_addr_ = nullptr;
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ALIGNED_PTR_H_
