/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef H46CE8BDB_A361_403B_AB8B_D4CFAE40604E
#define H46CE8BDB_A361_403B_AB8B_D4CFAE40604E

#include <vector>
#include <set>
#include "memory/span/span_layer.h"
#include "memory/span/span_allocator.h"

namespace llm {
class SpanLayerLut {
  using SpanLayerIdIterator = std::set<SpanLayerId>::iterator;
 public:
  explicit SpanLayerLut(const std::vector<SpanLayer *> &span_layers)
      : span_layers_{span_layers} {
  }

 public:
  virtual void OnLayerCreated(const SpanLayer &) = 0;
  virtual void OnLayerAddSpan(const SpanLayer &) = 0;
  virtual void OnLayerRemoveSpan(const SpanLayer &) = 0;
  virtual SpanLayerId FindFitLayerId(PageLen, size_t maxLiftLevel) const = 0;
  virtual void Release(SpanAllocator &) = 0;
  virtual ~SpanLayerLut() = default;

 public:
  SpanLayerIdIterator begin() {
    return span_layer_ids_.begin();
  }

  SpanLayerIdIterator end() {
    return span_layer_ids_.end();
  }

  SpanLayerIdIterator begin() const {
    return span_layer_ids_.begin();
  }

  SpanLayerIdIterator end() const {
    return span_layer_ids_.end();
  }

  size_t size() {
    return span_layer_ids_.size();
  }

 protected:
  std::set<SpanLayerId> span_layer_ids_;
  const std::vector<SpanLayer *> &span_layers_;
};

class SpanLayerQuickLut : public SpanLayerLut {
 public:
  using SpanLayerLut::SpanLayerLut;

 private:
  void OnLayerCreated(const SpanLayer &) override {
  }
  void OnLayerAddSpan(const SpanLayer &layer) override {
    if (layer.GetSize() == 1) {
      span_layer_ids_.emplace(layer.GetLayerId());
    }
  }
  void OnLayerRemoveSpan(const SpanLayer &layer) override {
    if (layer.IsEmpty()) {
      span_layer_ids_.erase(layer.GetLayerId());
    }
  }
  SpanLayerId FindFitLayerId(PageLen page_len, size_t max_lift_level) const override {
    if (max_lift_level == 0U) {
      return SPAN_LAYER_ID_INVALID;
    }
    auto layer_idIter = span_layer_ids_.lower_bound(page_len);
    return (layer_idIter == span_layer_ids_.end()) ? SPAN_LAYER_ID_INVALID : *layer_idIter;
  }
  void Release(SpanAllocator &span_allocator) override {
    for (auto &layer : span_layers_) {
      if (layer != nullptr) {
        layer->Release(span_allocator);
      }
    }
    span_layer_ids_.clear();
  }
};
}

#endif
