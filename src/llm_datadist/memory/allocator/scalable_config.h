/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef H940B2F9C_80E4_4BEB_A91A_798A20DDE4F5
#define H940B2F9C_80E4_4BEB_A91A_798A20DDE4F5

#include "memory/config/default_config.h"
#include "graph/types.h"

namespace llm {
constexpr ge::float64_t kMaxMemorySizeRatio = 0.95;
struct ScalableConfig {
  ScalableConfig();
  size_t page_idem_num{PAGE_SIZE_IDEM_DEFAULT};
  MemSize huge_page_mem_size{HUGE_PAGE_MEMORY_DEFAULT};
  MemSize page_mem_size_total_threshold{PAGE_MEM_SIZE_THRESHOLD_DEFAULT[0U]};
  MemSize page_mem_size_in_layer_threshold{PAGE_MEM_SIZE_IN_LAYER_THRESHOLD_DEFAULT};
  size_t span_count_in_layer_threshold{SPAN_COUNT_IN_LAYER_THRESHOLD_DEFAULT};
  size_t span_layer_lift_max{SPAN_LAYER_LIFT_LEVEL_DEFAULT};
  size_t span_prepared_count{SPAN_PREPARED_COUNT_DEFAULT};
  size_t span_layer_prepared_count{SPAN_LAYER_PREPARED_COUNT_DEFAULT};
  MemSize unsplitable_size_threshold{SPAN_UNSPLITABLE_MEM_SIZE_DEFAULT};
  MemSize uncacheable_size_threshold{SPAN_UNCACHEABLE_MEM_SIZE_DEFAULT[0U]};
};
}

#endif
