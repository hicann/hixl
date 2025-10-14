/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HBE3027D7_9FBA_4069_867F_47C2F2822015
#define HBE3027D7_9FBA_4069_867F_47C2F2822015

#include <new>
#include "memory/util/link.h"
#include "common/llm_checker.h"

namespace llm {
template<typename T>
class ObjectAllocator {
 public:
  explicit ObjectAllocator(size_t capacity) {
    for (size_t i = 0; i < capacity; i++) {
      auto elem = new(std::nothrow) Element();
      if (elem != nullptr) {
        elems.push_back(elem->node);
      }
    }
  }

  virtual ~ObjectAllocator() {
    while (!elems.empty()) {
      auto elem = elems.pop_front();
      if (elem != nullptr) {
        delete reinterpret_cast<Element *>(elem);
      }
    }
  }

  // Alloc memory but do not construct !!!
  T *AllocMem() {
    Element *elem = reinterpret_cast<Element *>(elems.pop_front());
    if (!elem) {
      elem = new (std::nothrow) Element();
      LLM_ASSERT_NOTNULL(elem);
    }
    return reinterpret_cast<T *>(elem->buff);
  }

  // Free memory but do not destruct !!!
  void FreeMem(T &elem) {
    elems.push_front(*(reinterpret_cast<ElemNode *>(&elem)));
  }

  // Alloc memory and construct with args!!!
  template <class... Args>
  T *New(Args &&... args) {
    return new (AllocMem()) T(std::forward<Args>(args)...);
  }

  // Alloc memory and construct without args!!!
  T *Alloc() {
    auto elem = elems.pop_front();
    if (elem != nullptr) {
      return reinterpret_cast<T *>(elem);
    }
    return reinterpret_cast<T *>(new(std::nothrow) Element());
  }

  // Free memory and destruct !!!
  void Free(T &elem) {
    elem.~T();
    FreeMem(elem);
  }

  size_t GetAvailableSize() const {
    return elems.size();
  }

 private:
  struct ElemNode : LinkNode<ElemNode> {
  };

  union Element {
    Element() {}
    ElemNode node;
    uint8_t buff[sizeof(T)];
  };

 private:
  Link<ElemNode> elems;
};
}

#endif
