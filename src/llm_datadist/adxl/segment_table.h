/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_ADXL_SEGMENT_TABLE_H_
#define CANN_GRAPH_ENGINE_RUNTIME_ADXL_SEGMENT_TABLE_H_

#include <vector>
#include <unordered_map>
#include "adxl/adxl_types.h"

namespace adxl {
class Segment {
 public:
  explicit Segment(MemType type) : mem_type_(type) {};
  void AddRange(uint64_t start, uint64_t end);
  void RemoveRange(uint64_t start, uint64_t end);
  bool Contains(uint64_t start, uint64_t end) const;
  MemType GetMemType() const;

 private:
  std::vector<std::pair<uint64_t, uint64_t>> ranges_;
  MemType mem_type_;
};

using SegmentPtr = std::shared_ptr<Segment>;
class SegmentTable {
 public:
  SegmentTable() = default;

  void AddRange(const std::string &channel_id, uint64_t start, uint64_t end, MemType type);
  void RemoveRange(const std::string &channel_id, uint64_t start, uint64_t end, MemType type);

  SegmentPtr FindSegment(const std::string &channel_id, uint64_t start, uint64_t end);

 private:
  std::unordered_map<std::string, std::vector<SegmentPtr>> channel_2_segment_;
};
}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_RUNTIME_ADXL_SEGMENT_TABLE_H_
