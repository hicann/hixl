/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_SEGMENT_H_
#define CANN_HIXL_SRC_HIXL_COMMON_SEGMENT_H_

#include <vector>
#include "hixl/hixl_types.h"

namespace hixl {
class Segment {
 public:
  explicit Segment(MemType type) : mem_type_(type) {};
  Status AddRange(uint64_t start, uint64_t len);
  void RemoveRange(uint64_t start, uint64_t end);
  bool Contains(uint64_t start, uint64_t end) const;
  MemType GetMemType() const;

 private:
  std::vector<std::pair<uint64_t, uint64_t>> ranges_;
  MemType mem_type_;
};

using SegmentPtr = std::shared_ptr<Segment>;
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_SEGMENT_H_