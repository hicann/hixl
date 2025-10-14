/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "segment_table.h"

namespace adxl {

void SegmentTable::AddRange(const std::string &channel_id, uint64_t start, uint64_t end, MemType type) {
  auto &segments = channel_2_segment_[channel_id];
  auto it = std::find_if(segments.begin(), segments.end(),
                         [type](const SegmentPtr &seg) { return seg->GetMemType() == type; });
  if (it != segments.end()) {
    (*it)->AddRange(start, end);
  } else {
    auto new_segment = std::make_shared<Segment>(type);
    new_segment->AddRange(start, end);
    segments.push_back(new_segment);
  }
}

SegmentPtr SegmentTable::FindSegment(const std::string &channel_id, uint64_t start, uint64_t end) {
  auto channel_it = channel_2_segment_.find(channel_id);
  if (channel_it == channel_2_segment_.end()) {
    return nullptr;
  }
  // only two segments: MEM_DEVICE and MEM_HOST
  for (const auto &segment : channel_it->second) {
    if (segment->Contains(start, end)) {
      return segment;
    }
  }
  return nullptr;
}

void Segment::AddRange(uint64_t start, uint64_t end) {
  ranges_.emplace_back(start, end);
  MergeRanges();
}

bool Segment::Contains(uint64_t start, uint64_t end) const {
  if (start > end) {
    return false;
  }
  auto it = std::lower_bound(ranges_.begin(), ranges_.end(), start,
                             [](const auto &range, uint64_t addr) { return range.second < addr; });
  return it != ranges_.end() && it->first <= start && it->second >= end;
}

MemType Segment::GetMemType() const {
  return mem_type_;
}

void Segment::MergeRanges() {
  std::sort(ranges_.begin(), ranges_.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

  std::vector<std::pair<uint64_t, uint64_t>> merged;
  merged.push_back(ranges_[0]);
  for (size_t i = 1; i < ranges_.size(); ++i) {
    auto &last = merged.back();
    auto &current = ranges_[i];
    if (current.first <= last.second + 1) {
      last.second = std::max(last.second, current.second);
    } else {
      merged.push_back(current);
    }
  }
  ranges_ = std::move(merged);
}

}  // namespace adxl
