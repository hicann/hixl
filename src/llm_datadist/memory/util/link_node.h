/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HEAAB628E_761B_4552_BEFF_EE3AAA1F9A19
#define HEAAB628E_761B_4552_BEFF_EE3AAA1F9A19

namespace llm {
template<typename T>
struct List;

template<typename T>
struct LinkNode {
  LinkNode() {
    link_.prev_ = nullptr;
    link_.next_ = nullptr;
  }

  void remove() {
    // Notice: Just used in scenes careless num of link!!!
    link_.prev_->link_.next_ = link_.next_;
    link_.next_->link_.prev_ = link_.prev_;
  }

  T *next() {
    return link_.next_;
  }

  const T *next() const {
    return link_.next_;
  }

  T *prev() {
    return link_.prev_;
  }

  const T *prev() const {
    return link_.prev_;
  }

  friend struct List<T>;

  struct Chain {
    T *volatile next_;
    T *volatile prev_;
  }; // __cacheline_aligned;

  Chain link_;
};
}

#endif
