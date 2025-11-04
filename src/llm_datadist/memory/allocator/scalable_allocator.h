/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef H5CF96432_BE55_46BE_B9E1_8F7A5C662D50
#define H5CF96432_BE55_46BE_B9E1_8F7A5C662D50

#include <memory>
#include "memory/allocator/scalable_config.h"
#include "memory/span/span_layer_allocator.h"
#include "memory/span/span_allocator.h"
#include "memory/span/span_layer_lut.h"
#include "memory_pool.h"

#define DLOG_EVENT 0x10

#define LOG_BY_TYPE(type, fmt, ...)                           \
  do {                                                        \
    if (type == DLOG_INFO) {                                  \
      dlog_info(LLM_MODULE_NAME, "%lu %s %s:" fmt, LlmLog::GetTid(), GetId().c_str(), __FUNCTION__, ##__VA_ARGS__);  \
    } else if (type == DLOG_ERROR) {                          \
      LLMLOGE(ge::FAILED, fmt, ##__VA_ARGS__);                 \
    } else if (type == DLOG_EVENT){                           \
      dlog_info(static_cast<int32_t>(static_cast<uint32_t>(RUN_LOG_MASK) | static_cast<uint32_t>(LLM_MODULE_NAME)), \
                "%lu %s %s:" fmt, LlmLog::GetTid(), GetId().c_str(), __FUNCTION__, ##__VA_ARGS__); \
    } else {}                                                 \
  } while (false)

namespace llm {
class ScalableAllocator : public MemoryPool {
 public:
  explicit ScalableAllocator(SpanAllocator &span_allocator, const ScalableConfig &cfg = ScalableConfig());
  ~ScalableAllocator() override;

  PageSpan *Alloc(ge::Allocator &allocator, const MemSize size) override;
  void Free(ge::MemBlock *block) override;
  void PrintDetails(const int32_t level = DLOG_ERROR) override;
  ge::Status InitFixSizedAllocator(ge::Allocator &allocator, void *base_addr, size_t size);

  const ScalableConfig &GetScalableConfig() const { return config_; }
  const std::string &GetId() const override;
  float GetReachTheoryRate() const;

 protected:
  ge::Status Finalize() override;
  virtual PageSpan *BlockAlloc(ge::Allocator &allocator, const BlockAddr block_addr, const MemAddr addr,
                               const size_t size);
  virtual PageSpan *SplitSpan(ge::Allocator &allocator, const SpanLayerId fix_layer_id,
                              const SpanLayerId fit_layer_id, PageSpan *const span, const MemSize size);
 private:
  PageSpan *FetchLayerSpan(const SpanLayerId layer_id);
  PageSpan *FetchSplitedSpan(ge::Allocator &allocator, const SpanLayerId fix_layer_id, const SpanLayerId fit_layer_id,
                             const MemSize size);
  PageSpan *PickOutBuddy(PageSpan *const buddy_span);
  PageSpan *TryMergePrev(PageSpan &span);
  PageSpan *TryMergeNext(PageSpan &span);

  PageSpan *PopSpanFromLayer(SpanLayer &layer);
  void PushSpanToLayer(SpanLayer &layer, PageSpan &span);
  void RemoveFromLayer(SpanLayer &layer, PageSpan &span);

  void OccupySpan(PageSpan &span, PageLen page_len);
  void FreeSpan(PageSpan &span);
  PageSpan *AllocImp(ge::Allocator &allocator, const MemSize size);
  SpanLayer *FetchSpanLayer(const SpanLayerId layer_id);
  MemSize GetAllocSize(const MemSize size) const;
  PageLen GetlayerSpanCapacity(const SpanLayerId layer_id) const;

 protected:
  size_t allocator_id_;
  std::string allocator_id_with_type_;
  const ScalableConfig config_;

 private:
  MemSize page_mem_size_;
  SpanLayerId span_layer_capacity_;
  SpanLayerId uncacheable_layer_start_;
  PageLen span_layer_page_capacity_;
  size_t alloc_succ_count_{0U};
  size_t free_succ_count_{0U};
  Link<PageSpan> occupied_spans_;
  std::vector<SpanLayer *> span_layers_;
  std::unique_ptr<SpanLayerLut> span_layer_lut_;
  SpanAllocator &span_allocator_;
  SpanLayerAllocator layer_allocator_;
  static std::atomic_size_t global_allocator_id_;
  MemSize max_occupied_size_{0U};
  void *base_addr_{nullptr};
  size_t theory_size_{0U};
  size_t theory_min_size_{0U};
  size_t real_theory_size_{0U};
  size_t real_theory_min_size_{0U};
  bool is_fix_sized_{false};
};
}

#endif
