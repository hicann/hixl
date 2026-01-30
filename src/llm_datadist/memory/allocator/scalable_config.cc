/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cmath>
#include "common/llm_inner_types.h"
#include "memory/allocator/scalable_allocator.h"
#include "acl/acl.h"
#include "common/llm_log.h"

namespace llm {
namespace {
constexpr MemSize kDeviceTotalMemorySizeLevel[MEMORY_SPECIFICATION_LEVEL_MAX] = {32_GB, 64_GB};

ge::Status GetDeviceTotalMemorySize(size_t &total_mem_size) {
  size_t free_mem;
  LLM_CHK_RT_RET(aclrtGetMemInfo(ACL_HBM_MEM, &free_mem, &total_mem_size));
  if (total_mem_size == 0U) {
    LLM_CHK_RT_RET(aclrtGetMemInfo(ACL_DDR_MEM, &free_mem, &total_mem_size));
  }
  (void)free_mem;
  return ge::SUCCESS;
}
}

ScalableConfig::ScalableConfig() {
  size_t total_mem_size = 0U;
  (void) GetDeviceTotalMemorySize(total_mem_size);

  for (size_t i = 0U; i < MEMORY_SPECIFICATION_LEVEL_MAX - 1U; ++i) {
    if (total_mem_size > kDeviceTotalMemorySizeLevel[i]) {
      page_mem_size_total_threshold = PAGE_MEM_SIZE_THRESHOLD_DEFAULT[i + 1U];
      uncacheable_size_threshold = SPAN_UNCACHEABLE_MEM_SIZE_DEFAULT[i + 1U];
    }
  }
  if (total_mem_size > 0U) {
    page_mem_size_total_threshold =
        static_cast<size_t>(floor(static_cast<float64_t>(total_mem_size) * kMaxMemorySizeRatio));
  }

  static bool printed = false;
  if (!printed) {
    printed = true;
    LLMEVENT("device total max size: %zu, page_mem_size_total_threshold: %lu, uncacheable_size_threshold: %lu",
            total_mem_size, page_mem_size_total_threshold, uncacheable_size_threshold);
  }
}
}
