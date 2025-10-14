/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef H8C7822C4_3060_4C00_BD99_9E8DF5C8D2A6
#define H8C7822C4_3060_4C00_BD99_9E8DF5C8D2A6

#include <set>
#include "memory/util/link.h"
#include "memory/span/span_layer_id.h"
#include "memory/span/span_allocator.h"

namespace llm {
class SpanLayer : public LinkNode<SpanLayer> {
 public:
  SpanLayer(SpanLayerId layer_id, size_t span_capacity)
      : layer_id_{layer_id}, span_capacity_{span_capacity} {
  }

  void PushSpan(PageSpan &span) {
    free_link_.push_front(span);
  }

  PageSpan *PopSpan() {
    return free_link_.pop_front();
  }

  void Remove(PageSpan &span) {
    free_link_.remove(span);
  }

  void Release(SpanAllocator &span_allocator);

  bool IsEmpty() const {
    return free_link_.size() == 0U;
  }

  SpanLayerId GetLayerId() const {
    return layer_id_;
  }

  size_t GetSize() const {
    return free_link_.size();
  }

  size_t GetCapacity() const {
    return span_capacity_;
  }

  size_t GetPageSize() const {
    return GetSize() * layer_id_;
  }

 private:
  SpanLayerId layer_id_;
  size_t span_capacity_;

  Link<PageSpan> free_link_;
};
}

#endif
