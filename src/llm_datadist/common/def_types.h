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

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_DEF_TYPES_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_DEF_TYPES_H_

#include <atomic>
#include <memory>
#include <vector>
namespace llm {
inline uint64_t PtrToValue(const void *const ptr) {
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr));
}

inline void *ValueToPtr(const uint64_t value) {
  return reinterpret_cast<void *>(static_cast<uintptr_t>(value));
}

inline std::vector<uint64_t> VPtrToValue(const std::vector<void *> v_ptr) {
  std::vector<uint64_t> v_value;
  for (const auto &ptr : v_ptr) {
    v_value.emplace_back(PtrToValue(ptr));
  }
  return v_value;
}

template<typename TI, typename TO>
inline TO *PtrToPtr(TI *const ptr) {
  return reinterpret_cast<TO *>(ptr);
}

template<typename TI, typename TO>
inline const TO *PtrToPtr(const TI *const ptr) {
  return reinterpret_cast<const TO *>(ptr);
}

template<typename T>
inline T *PtrAdd(T *const ptr, const size_t max_buf_len, const size_t idx) {
  if ((ptr != nullptr) && (idx < max_buf_len)) {
    return reinterpret_cast<T *>(ptr + idx);
  }
  return nullptr;
}
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_DEF_TYPES_H_
