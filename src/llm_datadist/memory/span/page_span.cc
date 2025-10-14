/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "memory/span/page_span.h"
#include <cmath>
#include "memory/allocator/scalable_allocator.h"

namespace llm {
void PageSpan::SetBuddy(PageSpan &buddy_span) {
  PageSpan *next_buddy = buddy_link_.GetNext();
  if (next_buddy != nullptr) {
    next_buddy->buddy_link_.SetPrev(&buddy_span);
  }
  buddy_link_.SetNext(&buddy_span);
  buddy_span.buddy_link_.SetNext(next_buddy);
  buddy_span.buddy_link_.SetPrev(this);
}

void PageSpan::MergeBuddy(PageSpan &buddy_span) {
  if ((buddy_link_.GetNext() == &buddy_span) && (buddy_span.buddy_link_.GetPrev() == this)) {
    PageSpan *next_of_buddy = buddy_span.buddy_link_.GetNext();
    if (next_of_buddy != nullptr) {
      next_of_buddy->buddy_link_.SetPrev(this);
    }
    buddy_link_.SetNext(next_of_buddy);
    page_len_ += buddy_span.GetPageLen();
    // Restore to original
    if (buddy_link_.IsEmpty()) {
      try_split_page_len_ = 0;
    }
  }
}

size_t PageSpan::GetSize() const {
  return PageLen_GetMemSize(page_len_, scalable_allocator_.GetScalableConfig().page_idem_num) ;
}
}

