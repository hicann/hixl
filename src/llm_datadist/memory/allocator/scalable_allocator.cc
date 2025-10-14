/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "memory/allocator/scalable_allocator.h"
#include <limits>
#include <map>
#include <iomanip>
#include "common/llm_checker.h"
#include "common/llm_scope_guard.h"

namespace llm {
namespace {
constexpr size_t kRatioBase = 100U;
}

std::atomic_size_t ScalableAllocator::global_allocator_id_(0U);

ScalableAllocator::ScalableAllocator(SpanAllocator &span_allocator, const ScalableConfig &cfg)
    : allocator_id_(++global_allocator_id_),
      allocator_id_with_type_("[allocator_" + std::to_string(allocator_id_) + "]"),
      config_{cfg},
      page_mem_size_{PageLen_GetMemSize(1, cfg.page_idem_num)},
      span_layer_capacity_{1 + SpanLayerId_GetIdFromSize(cfg.page_mem_size_total_threshold, cfg.page_idem_num)},
      uncacheable_layer_start_{SpanLayerId_GetIdFromSize(cfg.uncacheable_size_threshold, cfg.page_idem_num)},
      span_layer_page_capacity_{PageLen_GetLenFromSize(cfg.page_mem_size_total_threshold, cfg.page_idem_num)},
      span_allocator_{span_allocator},
      layer_allocator_{cfg.span_layer_prepared_count} {
  span_layers_.resize(span_layer_capacity_);
  // 不加nothrow，理由：由于构造函数无法返回失败，且这是关键资源申请，如果申请失败允许进程退出。
  auto layer_lut = static_cast<SpanLayerLut *>(new SpanLayerQuickLut{span_layers_});
  if (layer_lut != nullptr) {
    span_layer_lut_.reset(layer_lut);
  }
}

ScalableAllocator::~ScalableAllocator() {
  try {
    (void) Finalize();
    span_layers_.clear();
  } catch (const std::exception &) {
    // do nothing
  }
}

ge::Status ScalableAllocator::Finalize() {
  if (!span_layers_.empty() && span_layer_lut_ != nullptr && occupied_spans_.empty()) {
    span_layer_lut_->Release(span_allocator_);
    for (auto layer : span_layers_) {
      if (layer != nullptr) {
        if (layer->GetLayerId() < span_layers_.size()) {
          span_layers_[layer->GetLayerId()] = nullptr;
        }
        delete layer;
      }
    }
  }
  return ge::SUCCESS;
}

MemSize ScalableAllocator::GetAllocSize(const MemSize size) const {
  return (size < page_mem_size_) ? page_mem_size_ : MemSize_GetAlignedOf(size, page_mem_size_);
}

PageLen ScalableAllocator::GetlayerSpanCapacity(const SpanLayerId layer_id) const {
  if ((layer_id >= uncacheable_layer_start_) || (layer_id == 0U)) {
    return 0U;
  }
  return std::min(span_layer_page_capacity_ / layer_id, static_cast<uint32_t>(config_.span_count_in_layer_threshold));
}

PageSpan *ScalableAllocator::FetchLayerSpan(const SpanLayerId layer_id) {
  if (span_layers_.empty() || (span_layers_[layer_id] == nullptr)) {
    return nullptr;
  }

  auto span = PopSpanFromLayer(*span_layers_[layer_id]);
  if (span != nullptr) {
    OccupySpan(*span, layer_id);
  }
  return span;
}

PageSpan *ScalableAllocator::BlockAlloc(ge::Allocator &allocator, const BlockAddr block_addr, const MemAddr addr,
                                        const size_t size) {
  auto span = new(span_allocator_.Alloc()) PageSpan{allocator, *this, block_addr, addr, size};
  return span;
}

SpanLayer *ScalableAllocator::FetchSpanLayer(const SpanLayerId layer_id) {
  if (span_layers_.empty() || (layer_id >= span_layer_capacity_)) {
    return nullptr;
  }

  if (span_layers_[layer_id] != nullptr) {
    return span_layers_[layer_id];
  }

  auto newLayer = new(layer_allocator_.Alloc()) SpanLayer(layer_id, GetlayerSpanCapacity(layer_id));
  if (newLayer != nullptr) {
    span_layers_[layer_id] = newLayer;
    span_layer_lut_->OnLayerCreated(*newLayer);
  }
  return newLayer;
}

PageSpan *ScalableAllocator::FetchSplitedSpan(ge::Allocator &allocator, const SpanLayerId fix_layer_id,
                                              const SpanLayerId fit_layer_id, const MemSize size) {
  if (span_layers_.empty() || (fit_layer_id <= fix_layer_id)) {
    return nullptr;
  }
  if (span_layers_[fit_layer_id] == nullptr) {
    return nullptr;
  }

  auto span = PopSpanFromLayer(*span_layers_[fit_layer_id]);
  if (span == nullptr) {
    return nullptr;
  }
  return SplitSpan(allocator, fix_layer_id, fit_layer_id, span, size);
}

PageSpan *ScalableAllocator::SplitSpan(ge::Allocator &allocator, const SpanLayerId fix_layer_id,
                                       const SpanLayerId fit_layer_id, PageSpan *const span, const MemSize size) {
  LLM_ASSERT_NOTNULL(span);
  PageLen left_page_len = fit_layer_id - fix_layer_id;
  const auto buddy_addr = PageLen_ForwardAddr(left_page_len, config_.page_idem_num,
                                              reinterpret_cast<MemAddr>(span->GetAddr()));
  const auto buddy_span = BlockAlloc(allocator, nullptr, buddy_addr, static_cast<size_t>(size));
  if (buddy_span == nullptr) {
    return nullptr;
  }

  span->SetBuddy(*buddy_span);
  span->SetPageLen(left_page_len);

  const auto layer = FetchSpanLayer(left_page_len);
  if (layer == nullptr) {
    span_allocator_.Free(*span);
    return nullptr;
  }
  PushSpanToLayer(*layer, *span);
  OccupySpan(*buddy_span, fix_layer_id);
  return buddy_span;
}

PageSpan *ScalableAllocator::AllocImp(ge::Allocator &allocator, const MemSize size) {
  MemSize alloc_size = GetAllocSize(size);
  SpanLayerId fix_layer_id = SpanLayerId_GetIdFromSize(alloc_size, config_.page_idem_num);
  if ((fix_layer_id >= span_layer_capacity_) || (span_layer_lut_ == nullptr)) {
    return nullptr;
  }
  auto span = FetchLayerSpan(fix_layer_id);
  SpanLayerId fit_layer_id = 0U;
  if (span == nullptr) {
    fit_layer_id = span_layer_lut_->FindFitLayerId(fix_layer_id + 1, config_.span_layer_lift_max);
    if (fit_layer_id >= span_layer_capacity_) {
      span = nullptr;
    } else {
      span = FetchSplitedSpan(allocator, fix_layer_id, fit_layer_id, alloc_size);
    }
  }

  if (span != nullptr) {
    LOG_BY_TYPE(DLOG_INFO, "size:%zu block_size:%zu", size, span->GetSize());
    span->SetRealSize(size);
  }
  return span;
}

PageSpan *ScalableAllocator::Alloc(ge::Allocator &allocator, const MemSize size) {
  if (size > config_.page_mem_size_total_threshold) {
    LLMLOGE(ge::FAILED, "%s [ScalableAllocator]: size:%lu > mem_size_total_thresold:%lu", GetId().c_str(), size,
           config_.page_mem_size_total_threshold);
    return nullptr;
  }

  PageSpan *span = nullptr;
  span = AllocImp(allocator, size);
  if (span != nullptr) {
    theory_size_ += span->GetSize();
    real_theory_size_ += span->GetRealSize();
    if (real_theory_size_ > real_theory_min_size_) {
      real_theory_min_size_ = real_theory_size_;
    }

    if (theory_size_ > theory_min_size_) {
      theory_min_size_ = theory_size_;
    }
    LOG_BY_TYPE(DLOG_INFO, "Malloc block size:%llu allocate_size:%zu mem_addr:%p. span addr %p",
                size, span->GetSize(), span->GetAddr(), span);
  }
  return span;
}

PageSpan *ScalableAllocator::PickOutBuddy(PageSpan *const buddy_span) {
  if (span_layers_.empty() || (buddy_span == nullptr)) {
    return nullptr;
  }
  if (buddy_span->GetCount() != 0U) {
    return nullptr;
  }

  PageLen page_len = buddy_span->GetPageLen();
  if (page_len >= span_layer_capacity_ || span_layers_[page_len] == nullptr) {
    return nullptr;
  }
  RemoveFromLayer(*span_layers_[page_len], *buddy_span);
  return buddy_span;
}

PageSpan *ScalableAllocator::TryMergePrev(PageSpan &span) {
  auto prev_buddy = PickOutBuddy(span.GetPrevBuddy());
  if (prev_buddy == nullptr) {
    return &span;
  }

  prev_buddy->MergeBuddy(span);
  span_allocator_.Free(span);
  return prev_buddy;
}

PageSpan *ScalableAllocator::TryMergeNext(PageSpan &span) {
  auto next_buddy = PickOutBuddy(span.GetNextBuddy());
  if (next_buddy == nullptr) {
    return &span;
  }

  span.MergeBuddy(*next_buddy);
  span_allocator_.Free(*next_buddy);
  return &span;
}

void ScalableAllocator::Free(ge::MemBlock *block) {
  auto span = reinterpret_cast<PageSpan *>(block);
  if ((span == nullptr) || (span_layer_lut_ == nullptr)) {
    return;
  }

  if (theory_size_ >= span->GetRealSize()) {
    real_theory_size_ -= span->GetRealSize();
    theory_size_ -= span->GetSize();
  }
  LOG_BY_TYPE(DLOG_INFO, "Free block theory_size_:%zu theory_min_size_:%zu allock_size:%zu mem_addr:%p.",
              theory_size_, theory_min_size_, span->GetSize(), span->GetAddr());

  occupied_spans_.remove(*span);
  span = TryMergeNext(*span);
  span = TryMergePrev(*span);
  FreeSpan(*span);
}

PageSpan *ScalableAllocator::PopSpanFromLayer(SpanLayer &layer) {
  PageSpan *span = layer.PopSpan();
  span_layer_lut_->OnLayerRemoveSpan(layer);
  return span;
}

void ScalableAllocator::PushSpanToLayer(SpanLayer &layer, PageSpan &span) {
  layer.PushSpan(span);
  span_layer_lut_->OnLayerAddSpan(layer);
}

void ScalableAllocator::RemoveFromLayer(SpanLayer &layer, PageSpan &span) {
  layer.Remove(span);
  span_layer_lut_->OnLayerRemoveSpan(layer);
}

void ScalableAllocator::OccupySpan(PageSpan &span, PageLen page_len) {
  span.Alloc(page_len);
  occupied_spans_.push_front(span);
  alloc_succ_count_++;
}

void ScalableAllocator::FreeSpan(PageSpan &span) {
  SpanLayer *layer = nullptr;
  layer = FetchSpanLayer(span.GetPageLen());
  if (layer != nullptr) {
    PushSpanToLayer(*layer, span);
    free_succ_count_++;
  }
}

void ScalableAllocator::PrintDetails(const int32_t level) {
  if (((level != DLOG_EVENT) && (!LlmIsLogEnable(LLM_MODULE_NAME, level)))) {
    return;
  }

  LOG_BY_TYPE(level, "Allocator memory: [alloc count:%zu free count:%zu]", alloc_succ_count_,
              free_succ_count_);

  std::map<size_t, size_t> occupied_span_stat;
  size_t total_page_count = 0U;
  for (const auto &span : occupied_spans_) {
    total_page_count += span.GetPageLen();
    occupied_span_stat[span.GetPageLen()]++;
  }

  LOG_BY_TYPE(level, "Using: [span count:%zu page count:%zu total size:%llu]",
              occupied_spans_.size(), total_page_count, PageLen_GetMemSize(total_page_count, config_.page_idem_num));
  for (const auto &stat : occupied_span_stat) {
    LOG_BY_TYPE(level, "    |-span: [size:%-11llu count:%-5zu]",
                PageLen_GetMemSize(stat.first, config_.page_idem_num), stat.second);
  }

  size_t total_span_count = 0U;
  MemSize total_mem_size = 0U;
  total_page_count = 0U;
  LOG_BY_TYPE(level, "Freed span layers: [level:%u, count:%zu]", span_layer_capacity_, span_layer_lut_->size());
  for (const auto &layer_id : *span_layer_lut_) {
    if ((!span_layers_.empty()) && (layer_id < span_layer_capacity_) && (span_layers_[layer_id] != nullptr)) {
      total_span_count += span_layers_[layer_id]->GetSize();
      total_page_count += span_layers_[layer_id]->GetPageSize();
      total_mem_size += PageLen_GetMemSize(span_layers_[layer_id]->GetPageSize(), config_.page_idem_num);
    }
  }

  LOG_BY_TYPE(level, "Freed: [span count:%zu page count:%zu total size:%llu]", total_span_count, total_page_count,
              total_mem_size);
  for (const auto &layer_id : *span_layer_lut_) {
    if ((!span_layers_.empty()) && (layer_id < span_layer_capacity_) && (span_layers_[layer_id] != nullptr)) {
      LOG_BY_TYPE(level, "    |-layer: [id:%-5u capacity:%-5zu size:%-11llu count:%-5zu]",
                  span_layers_[layer_id]->GetLayerId(), span_layers_[layer_id]->GetCapacity(),
                  PageLen_GetMemSize(span_layers_[layer_id]->GetLayerId(), config_.page_idem_num),
                  span_layers_[layer_id]->GetSize());
    }
  }
}

const std::string &ScalableAllocator::GetId() const {
  return allocator_id_with_type_;
}

ge::Status ScalableAllocator::InitFixSizedAllocator(ge::Allocator &allocator, void *base_addr, size_t size) {
  is_fix_sized_ = true;
  const auto span = BlockAlloc(allocator, nullptr, static_cast<uint8_t *>(base_addr), size);
  LLM_CHK_BOOL_RET_STATUS(span != nullptr, ge::FAILED, "Failed to alloc block.");

  LLM_DISMISSABLE_GUARD(fail_guard, ([this, span]() {
      span_allocator_.Free(*span);
  }));

  base_addr_ = span->GetAddr();
  LOG_BY_TYPE(DLOG_INFO, "base_addr:%p size:%zu", base_addr_, span->GetSize());
  // span is idle, so the usage count needs to be 0
  while (span->GetCount() > 0U) {
    span->SubCount();
  }

  const SpanLayerId fit_layer_id =
      SpanLayerId_GetIdFromSize(size, ScalableAllocator::config_.page_idem_num);
  const auto layer = FetchSpanLayer(fit_layer_id);
  LLM_CHK_BOOL_RET_STATUS(layer != nullptr, ge::FAILED, "Failed to fetch span layer.");
  PushSpanToLayer(*layer, *span);
  PrintDetails(DLOG_EVENT);
  LLM_DISMISS_GUARD(fail_guard);
  return ge::SUCCESS;
}
}
