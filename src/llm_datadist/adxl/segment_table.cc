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
#include <algorithm>
#include "common/llm_inner_types.h"

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

void SegmentTable::RemoveRange(const std::string &channel_id, uint64_t start, uint64_t end, MemType type) {
  auto channel_it = channel_2_segment_.find(channel_id);
  if (channel_it == channel_2_segment_.end()) {
    return;
  }
  auto &segments = channel_it->second;
  auto it = std::find_if(segments.begin(), segments.end(),
                         [type](const SegmentPtr &seg) { return seg->GetMemType() == type; });
  if (it != segments.end()) {
    (*it)->RemoveRange(start, end);
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
  auto it = std::upper_bound(ranges_.begin(), ranges_.end(), start,
                             [](uint64_t val, const std::pair<uint64_t, uint64_t> &range) {
                               return val < range.first;
                             });
  ranges_.insert(it, {start, end});
}

void Segment::RemoveRange(uint64_t start, uint64_t end) {
  auto it = std::lower_bound(ranges_.begin(), ranges_.end(), start,
                             [](const std::pair<uint64_t, uint64_t> &range, uint64_t val) {
                               return range.first < val;
                             });
  for (; it != ranges_.end() && it->first == start; ++it) {
    if (it->second == end) {
      ranges_.erase(it);
      LLMLOGI("Remove range %lu-%lu, left ranges size:%zu", start, end, ranges_.size());
      return;
    }
  }
}

bool Segment::Contains(uint64_t start, uint64_t end) const {
  if (start > end) {
    return false;
  }
  auto it = std::upper_bound(ranges_.begin(), ranges_.end(), start,
                             [](uint64_t val, const std::pair<uint64_t, uint64_t> &range) {
                               return val < range.first;
                             });

  uint64_t max_reached = start;
  bool covered_start = false;
  // left range make sure range.start <= start
  for (auto r_it = std::make_reverse_iterator(it); r_it != ranges_.rend(); ++r_it) {
    if (r_it->second > start) {
      if (r_it->second > max_reached) {
        max_reached = r_it->second;
      }
      covered_start = true;
      if (max_reached >= end) {
        return true;
      }
    }
  }

  if (!covered_start) {
    return false;
  }

  for (; it != ranges_.end(); ++it) {
    if (it->first > (max_reached + 1)) {
      break;
    }
    if (it->second > max_reached) {
      max_reached = it->second;
      if (max_reached >= end) {
        return true;
      }
    }
  }
  return max_reached >= end;
}

MemType Segment::GetMemType() const {
  return mem_type_;
}

}  // namespace adxl
