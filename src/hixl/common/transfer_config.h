/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_TRANSFER_CONFIG_H_
#define CANN_HIXL_SRC_HIXL_COMMON_TRANSFER_CONFIG_H_

#include <algorithm>
#include <cstdint>
#include <limits>

namespace hixl {

constexpr uint32_t kDefaultMaxTransferCountPerBatch = 1920U;
constexpr uint32_t kMaxTransferCountPerBatch = 32766U;
constexpr uint32_t kMaxFixedQueueTransferCountPerBatch = 1920U;
constexpr uint32_t kMinTransportQueueDepth = 64U;

static_assert(kMaxTransferCountPerBatch <= std::numeric_limits<uint32_t>::max() - 2U,
              "max_transfer_count_per_batch + 2 must not overflow uint32_t.");
static_assert(kMaxTransferCountPerBatch + 2U <= (1U << 31U),
              "The required transport queue depth must fit in uint32_t after rounding up to a power of two.");

inline uint32_t CalculateTransportQueueDepth(uint32_t max_transfer_count_per_batch) {
  uint32_t depth = 1U;
  const uint32_t required = max_transfer_count_per_batch + 2U;
  while (depth < required) {
    depth <<= 1U;
  }
  return std::max(kMinTransportQueueDepth, depth);
}

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_TRANSFER_CONFIG_H_
