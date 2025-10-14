/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */


#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_UTILS_SYNC_FLAG_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_UTILS_SYNC_FLAG_H_

#include <cstdint>
#include <chrono>
#include "common/llm_log.h"

namespace llm {
class SyncFlag {
 public:
  explicit SyncFlag(void *flag);
  ~SyncFlag() = default;

  int32_t Wait(const std::chrono::steady_clock::time_point *end_time_point = nullptr) const;
  ge::Status Check() const;

 private:
  volatile uint8_t *flag_;
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_UTILS_SYNC_FLAG_H_
