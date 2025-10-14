/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef H17C3CFE2_A674_4D31_8851_4024DEECEB04
#define H17C3CFE2_A674_4D31_8851_4024DEECEB04

#include "memory/type/mem_size.h"
#include "memory/span/span_layer_id.h"

namespace llm {
// specification level
constexpr size_t MEMORY_SPECIFICATION_LEVEL_MAX = 2U;

// Page size 64_KB default
constexpr size_t PAGE_SIZE_IDEM_DEFAULT = 16U;

// Huge page memory size default 2MB
constexpr MemSize HUGE_PAGE_MEMORY_DEFAULT = 2_MB;

// Recycle memory when total cached size exceeded
constexpr MemSize PAGE_MEM_SIZE_THRESHOLD_DEFAULT[MEMORY_SPECIFICATION_LEVEL_MAX] = {30_GB, 60_GB};

// Recycle memory when layer cached size exceeded
constexpr MemSize PAGE_MEM_SIZE_IN_LAYER_THRESHOLD_DEFAULT = 8_GB;

// Recycle memory when span count in layer exceeded
constexpr size_t SPAN_COUNT_IN_LAYER_THRESHOLD_DEFAULT = 10240U;

// Span memory prepared count
constexpr size_t SPAN_PREPARED_COUNT_DEFAULT = 10240U;

// Span layer memory prepared count
constexpr size_t SPAN_LAYER_PREPARED_COUNT_DEFAULT = 4096U;

// Unspliting span size threshold
constexpr MemSize SPAN_UNSPLITABLE_MEM_SIZE_DEFAULT = 16_GB;

// Uncaching span size threshold
constexpr MemSize SPAN_UNCACHEABLE_MEM_SIZE_DEFAULT[MEMORY_SPECIFICATION_LEVEL_MAX] = {16_GB, 32_GB};

// Find fitable span layer by set or by sequential order
constexpr bool SPAN_LAYER_QUICK_MODE_ENABLE_DEFAULT = true; // locate fitable span by set default

// Max traveling layer count when finding fitable span;
constexpr size_t SPAN_LAYER_LIFT_LEVEL_DEFAULT = PAGE_LENGTH_INVALID; // not limit default
}

#endif
