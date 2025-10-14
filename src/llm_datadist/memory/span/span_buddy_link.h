/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef H12EBE7B2_04A4_4EA1_9B67_A3A878EEECD0
#define H12EBE7B2_04A4_4EA1_9B67_A3A878EEECD0
namespace llm {
class PageSpan;

class SpanBuddyLink {
 public:
  SpanBuddyLink() = default;

  virtual ~SpanBuddyLink() {
    Clear();
  }

  PageSpan *GetPrev() {
    return prev_;
  }

  PageSpan *GetNext() {
    return next_;
  }

  void SetPrev(PageSpan *span) {
    prev_ = span;
  }

  void SetNext(PageSpan *span) {
    next_ = span;
  }

  void Clear() {
    prev_ = nullptr;
    next_ = nullptr;
  }

  bool IsEmpty() const {
    return (prev_ == nullptr) && (next_ == nullptr);
  }

 private:
   PageSpan *volatile prev_{nullptr};
   PageSpan *volatile next_{nullptr};
};
}

#endif
