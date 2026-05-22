/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "segment_manager.h"

namespace hixl_kv_benchmark {

void SegmentManager::AddSegment(std::uint32_t segment_id, std::uint64_t size) {
  segments_.push_back(Segment{segment_id, size, 0U});
}

std::optional<SegmentAllocation> SegmentManager::AllocateFrom(std::uint32_t start_index, std::uint64_t size) {
  if (segments_.empty()) {
    return std::nullopt;
  }
  const std::uint32_t count = static_cast<std::uint32_t>(segments_.size());
  for (std::uint32_t i = 0; i < count; ++i) {
    Segment &segment = segments_[(start_index + i) % count];
    if (segment.used > segment.size || (segment.size - segment.used) < size) {
      continue;
    }
    const std::uint64_t offset = segment.used;
    segment.used += size;
    return SegmentAllocation{segment.id, offset, size};
  }
  return std::nullopt;
}

}  // namespace hixl_kv_benchmark
