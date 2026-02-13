/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "segment.h"
#include <algorithm>
#include "hixl_log.h"

namespace hixl {

Status Segment::AddRange(uint64_t start, uint64_t len) {
  // 检查地址溢出
  if (len > UINT64_MAX - start) {
    HIXL_LOGE(PARAM_INVALID, "Address overflow, addr:%lu, len:%lu", start, len);
    return PARAM_INVALID;
  }
  uint64_t end = start + len;
  auto it = std::upper_bound(ranges_.begin(), ranges_.end(), start,
                             [](uint64_t val, const std::pair<uint64_t, uint64_t> &range) {
                               return val < range.first;
                             });
  // 检查是否已经存在完全相同的范围
  if (it != ranges_.begin()) {
    auto prev_it = std::prev(it);
    if (prev_it->first == start && prev_it->second == end) {
      HIXL_LOGI("Range already exists, start:%lu, end:%lu", start, end);
      return SUCCESS;
    }
  }
  if (it != ranges_.end() && it->first == start && it->second == end) {
    HIXL_LOGI("Range already exists, start:%lu, end:%lu", start, end);
    return SUCCESS;
  }
  ranges_.insert(it, {start, end});
  HIXL_LOGI("Add range success, start:%lu, end:%lu", start, end);
  return SUCCESS;
}

void Segment::RemoveRange(uint64_t start, uint64_t end) {
  auto it = std::lower_bound(ranges_.begin(), ranges_.end(), start,
                             [](const std::pair<uint64_t, uint64_t> &range, uint64_t val) {
                               return range.first < val;
                             });
  if (it == ranges_.end() || it->first > start) {
    HIXL_LOGW("Range not found, start:%lu, end:%lu", start, end);
    return;
  }
  for (; it != ranges_.end() && it->first == start; ++it) {
    if (it->second == end) {
      ranges_.erase(it);
      HIXL_LOGI("Remove range %lu-%lu, left ranges size:%zu", start, end, ranges_.size());
      return;
    }
  }
}

bool Segment::Contains(uint64_t start, uint64_t end) const {
  HIXL_LOGI("Check if segment contains range [%lu, %lu]", start, end);
  if (start > end) {
    HIXL_LOGE(PARAM_INVALID, "Invalid range, start:%lu, end:%lu", start, end);
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
        HIXL_LOGI("Segment contains range [%lu, %lu]", start, end);
        return true;
      }
    }
  }

  if (!covered_start) {
    HIXL_LOGE(PARAM_INVALID, "Range not found, start:%lu, end:%lu", start, end);
    return false;
  }

  for (; it != ranges_.end(); ++it) {
    if (it->first > (max_reached + 1)) {
      break;
    }
    if (it->second > max_reached) {
      max_reached = it->second;
      if (max_reached >= end) {
        HIXL_LOGI("Segment contains range [%lu, %lu]", start, end);
        return true;
      }
    }
  }
  bool result = max_reached >= end;
  HIXL_LOGI("Segment contains range [%lu, %lu]", start, end);
  return result;
}

MemType Segment::GetMemType() const {
  return mem_type_;
}

}  // namespace hixl