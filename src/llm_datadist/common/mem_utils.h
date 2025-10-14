/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_MEM_UTILS_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_MEM_UTILS_H_

#include <memory>
#include <utility>

namespace llm {
template <typename T, typename... Args>
inline std::shared_ptr<T> MakeShared(Args &&... args) {
  using T_nc = typename std::remove_const<T>::type;
  const std::shared_ptr<T> ret = std::make_shared<T_nc>(std::forward<Args>(args)...);
  return ret;
}

template <typename T>
struct MakeUniq {
  using unique_object = std::unique_ptr<T>;
};

template <typename T>
struct MakeUniq<T[]> {
  using unique_array = std::unique_ptr<T[]>;
};

template <typename T, size_t B>
struct MakeUniq<T[B]> {
  struct invalid_type { };
};

template <typename T, typename... Args>
inline typename MakeUniq<T>::unique_object MakeUnique(Args &&... args) {
  using T_nc = typename std::remove_const<T>::type;
  return std::unique_ptr<T>(new (std::nothrow) T_nc(std::forward<Args>(args)...));
}

template <typename T>
inline typename MakeUniq<T>::unique_array MakeUnique(const size_t num) {
  return std::unique_ptr<T>(new (std::nothrow) typename std::remove_extent<T>::type[num]());
}

template <typename T, typename... Args>
inline typename MakeUniq<T>::invalid_type MakeUnique(Args &&...) = delete;
}

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_MEM_UTILS_H_
