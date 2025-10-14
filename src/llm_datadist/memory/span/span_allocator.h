/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef H9092AB01_1339_410D_ADCF_1D1FAE19EF03
#define H9092AB01_1339_410D_ADCF_1D1FAE19EF03

#include "memory/allocator/scalable_config.h"
#include "memory/util/object_allocator.h"
#include "memory/span/page_span.h"
#include "common/llm_log.h"

namespace llm {
class SpanAllocator {
 public:
  SpanAllocator() = default;
  virtual ~SpanAllocator() = default;
  virtual PageSpan *Alloc() = 0;
  virtual void Free(PageSpan &span) = 0;
};

class SpanAllocatorImp : public SpanAllocator {
 public:
  explicit SpanAllocatorImp(size_t capacity = ScalableConfig().span_prepared_count) : span_allocator_(capacity) {}
  ~SpanAllocatorImp() = default;
  PageSpan *Alloc() override {
    auto span = span_allocator_.Alloc();
    return span;
  }

  void Free(PageSpan &span) override {
    span_allocator_.Free(span);
  }
 private:
  ObjectAllocator<PageSpan> span_allocator_;
};
}

#endif
