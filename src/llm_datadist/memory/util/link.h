/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef H29B1191B_457B_4E80_8056_1233626F0A9A
#define H29B1191B_457B_4E80_8056_1233626F0A9A

#include <cstddef>
#include <cstdint>
#include <new>
#include "memory/util/link_node.h"

namespace llm {
template<typename T>
struct Link {
  struct Iterator {
    Iterator() noexcept
        : elem_(nullptr), next_(nullptr) {
    }

    explicit Iterator(T *const elem) noexcept
        : elem_(elem), next_(next_of(elem)) {
    }

    Iterator(const Iterator &rhs) noexcept
        : elem_(rhs.elem_), next_(rhs.next_) {
    }

    Iterator &operator=(const Iterator &other) noexcept {
      if (this != &other) {
        elem_ = other.elem_;
        next_ = other.next_;
      }
      return *this;
    }

    bool operator==(const Iterator &rhs) const noexcept {
      return elem_ == rhs.elem_;
    }

    bool operator!=(const Iterator &rhs) const noexcept {
      return !(*this == rhs);
    }

    void reset() noexcept {
      elem_ = nullptr;
      next_ = nullptr;
    }

    T *operator->() noexcept {
      return elem_;
    }

    T &operator*() noexcept {
      return *elem_;
    }

    T *value() const noexcept {
      return elem_;
    }

    Iterator operator++(int32_t) noexcept {
      Iterator i = *this;
      elem_ = next_;
      next_ = next_of(elem_);
      return i;
    }

    Iterator &operator++() noexcept {
      elem_ = next_;
      next_ = next_of(elem_);
      return *this;
    }

   private:
    static T *next_of(const T *const elem) noexcept {
      return elem == nullptr ? nullptr : Link<T>::next_of(elem);
    }

    static T *prev_of(const T *const elem) noexcept {
      return elem == nullptr ? nullptr : Link<T>::prev_of(elem);
    }
   private:
    T *elem_;
    T *next_;
  };

  struct ReverseIterator {
    ReverseIterator() noexcept
        : elem_(nullptr), next_(nullptr) {
    }

    explicit ReverseIterator(T *elem) noexcept
        : elem_(elem), next_(next_of(elem)) {
    }

    ReverseIterator(const ReverseIterator &rhs) noexcept
        : elem_(rhs.elem_), next_(rhs.next_) {
    }

    ReverseIterator &operator=(const ReverseIterator &other) noexcept {
      if (this != &other) {
        elem_ = other.elem_;
        next_ = other.next_;
      }
      return *this;
    }

    bool operator==(const ReverseIterator &rhs) const noexcept {
      return elem_ == rhs.elem_;
    }

    bool operator!=(const ReverseIterator &rhs) const noexcept {
      return !(*this == rhs);
    }

    void reset() noexcept {
      elem_ = nullptr;
      next_ = nullptr;
    }

    T *operator->() noexcept {
      return elem_;
    }

    T &operator*() noexcept {
      return *elem_;
    }

    T *value() const noexcept {
      return elem_;
    }

    ReverseIterator operator++(int32_t) noexcept {
      ReverseIterator i = *this;
      elem_ = next_;
      next_ = next_of(elem_);
      return i;
    }

    ReverseIterator &operator++() noexcept {
      elem_ = next_;
      next_ = next_of(elem_);
      return *this;
    }

   private:
    static T *next_of(const T *const elem) noexcept {
      return elem == nullptr ? nullptr : Link<T>::prev_of(elem);
    }

   private:
    T *elem_;
    T *next_;
  };

  Link() : num_(0U) {
    head_.next_ = sentinel();
    head_.prev_ = sentinel();
  }

  Link(Link &&other) noexcept {
    *this = std::move(other);
  }

  Link &operator=(Link &&other) noexcept {
    if (this != &other) {
      this->reset();
      this->concat(other);
    }
    return *this;
  }

  bool empty() const {
    return head_.next_ == sentinel();
  }

  size_t size() const {
    return num_;
  }

  Iterator begin() const {
    return Iterator(head_.next_);
  }

  Iterator end() const {
    return Iterator(const_cast<T *>(sentinel()));
  }

  ReverseIterator rbegin() const {
    return ReverseIterator(head_.prev_);
  }

  ReverseIterator rend() const {
    return ReverseIterator(const_cast<T *>(sentinel()));
  }

  bool is_back(const T *const elem) const {
    return elem == head_.prev_;
  }

  bool is_front(const T *const elem) const {
    return elem == head_.next_;
  }

  T *front() const {
    return empty() ? nullptr : head_.next_;
  }

  T *back() const {
    return empty() ? nullptr : head_.prev_;
  }

  void push_back(T &elem) {
    elem.link_.next_ = sentinel();
    elem.link_.prev_ = sentinel()->link_.prev_;
    sentinel()->link_.prev_->link_.next_ = &elem;
    sentinel()->link_.prev_ = &elem;
    num_++;
  }

  void push_front(T &elem) {
    elem.link_.prev_ = sentinel();
    elem.link_.next_ = sentinel()->link_.next_;
    sentinel()->link_.next_->link_.prev_ = &elem;
    sentinel()->link_.next_ = &elem;
    num_++;
  }

  void push_back(Iterator &elem) {
    push_back(*elem);
  }

  void push_front(Iterator &elem) {
    push_front(*elem);
  }

  T *pop_front() {
    Iterator i = begin();
    if (i == end()) {
      return nullptr;
    }

    erase(i);
    return &(*i);
  }

  void concat(Link<T> &other) {
    if (other.empty()) {
      return;
    }

    other.head_.prev_->link_.next_ = sentinel();
    other.head_.next_->link_.prev_ = sentinel();
    sentinel()->link_.prev_->link_.next_ = other.head_.next_;
    sentinel()->link_.prev_ = other.head_.prev_;

    num_ += other.size();
    other.reset();
  }

  void remove(T &elem) {
    elem.remove();
    num_--;
  }

  void erase(Iterator elem) {
    if (!elem.value()) {
      return;
    }

    remove(*elem);
  }

  void clear() {
    while (pop_front() != nullptr) {};
  }

  Iterator next_of(Iterator &i) const {
    return (i == end()) ? end() : ++i;
  }

 private:
  static T *next_of(const T *const elem) {
    return elem->link_.next_;
  }

  static T *prev_of(const T *const elem) {
    return elem->link_.prev_;
  }

  void reset() {
    head_.next_ = sentinel();
    head_.prev_ = sentinel();
    num_ = 0U;
  }

  T *sentinel() {
    return reinterpret_cast<T *>(reinterpret_cast<uint8_t volatile *>(&(head_.next_))
                                 - reinterpret_cast<uint8_t volatile *>(&(static_cast<T *>(nullptr)->link_)));
  }

  const T *sentinel() const {
    return reinterpret_cast<T *>(reinterpret_cast<uint8_t const volatile *>(&(head_.next_))
                                 - reinterpret_cast<uint8_t volatile *>(&(static_cast<T *>(nullptr)->link_)));
  }

 private:
  typename LinkNode<T>::Chain head_;
  size_t num_;
};
}

#endif
