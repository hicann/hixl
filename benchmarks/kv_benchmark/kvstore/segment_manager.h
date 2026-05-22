/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_KV_BENCHMARK_SEGMENT_MANAGER_H
#define HIXL_KV_BENCHMARK_SEGMENT_MANAGER_H

#include <cstdint>
#include <optional>
#include <vector>

namespace hixl_kv_benchmark {

struct Segment {
  std::uint32_t id = 0U;
  std::uint64_t size = 0U;
  std::uint64_t used = 0U;
};

struct SegmentAllocation {
  std::uint32_t segment_id = 0U;
  std::uint64_t offset = 0U;
  std::uint64_t size = 0U;
};

class SegmentManager {
 public:
  void AddSegment(std::uint32_t segment_id, std::uint64_t size);
  std::optional<SegmentAllocation> AllocateFrom(std::uint32_t start_index, std::uint64_t size);
  const std::vector<Segment> &Segments() const { return segments_; }

 private:
  std::vector<Segment> segments_;
};

}  // namespace hixl_kv_benchmark

#endif  // HIXL_KV_BENCHMARK_SEGMENT_MANAGER_H
