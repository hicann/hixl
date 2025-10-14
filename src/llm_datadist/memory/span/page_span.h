/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef H3A8A51B9_C45F_4713_89E4_03F02FA1F99F
#define H3A8A51B9_C45F_4713_89E4_03F02FA1F99F

#include "memory/type/mem_addr.h"
#include "memory/type/ref_count.h"
#include "memory/util/link_node.h"
#include "memory/span/span_layer_id.h"
#include "memory/span/span_buddy_link.h"
#include "graph/types.h"

namespace llm {
class ScalableAllocator;
constexpr ge::float64_t kDelaySplitRatio = 0.01;

class PageSpan : public ge::MemBlock, public LinkNode<PageSpan> {
 public:
  PageSpan(ge::Allocator &allocator, ScalableAllocator &scalable_allocator, BlockAddr block_addr, MemAddr addr,
           size_t mem_size)
      : ge::MemBlock(allocator, addr, mem_size), block_addr_{block_addr}, scalable_allocator_{scalable_allocator} {
  }

  ~PageSpan() override {
    page_len_ = 0;
    link_.next_ = nullptr;
    link_.prev_ = nullptr;
    try_split_page_len_ = 0;
    real_size_ = 0U;
    pa_list_.clear();
  }

  void Alloc(PageLen page_len) {
    this->page_len_ = page_len;
    SetSize(GetSize());
    if (GetCount() == 0U) {
      AddCount();
    }
  }

  BlockAddr GetBlockAddr() const {
    return block_addr_;
  }

  void SetAddr(BlockAddr addr) {
    this->block_addr_ = addr;
  }

  void SetPageLen(PageLen page_len) {
    this->page_len_ = page_len;
    SetSize(GetSize());
  }

  PageLen GetPageLen() const {
    return page_len_;
  }

  PageSpan *GetPrevBuddy() {
    return buddy_link_.GetPrev();
  }

  PageSpan *GetNextBuddy() {
    return buddy_link_.GetNext();
  }

  bool HasSplited() const {
    return !buddy_link_.IsEmpty();
  }

  void SetBuddy(PageSpan &buddy_span);
  void MergeBuddy(PageSpan &buddy_span);

  size_t GetSize() const;

  void SetRealSize(size_t real_size) {
    real_size_ = real_size;
  }

  size_t GetRealSize() const {
    return real_size_;
  }

  std::vector<size_t> &GetPaList() {
    return pa_list_;
  }

 private:
  BlockAddr block_addr_{nullptr};
  PageLen page_len_{0};
  PageLen try_split_page_len_{0};

  SpanBuddyLink buddy_link_;
  ScalableAllocator &scalable_allocator_;
  std::vector<size_t> pa_list_;
  size_t real_size_{0U};
  bool splitable_{false};
};
}

#endif
