/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ge/ge_allocator.h"
#include "memory/allocator/scalable_config.h"
#include "memory/span/page_span.h"
#include "memory/span/span_allocator.h"

#define private public
#include "memory/allocator/scalable_allocator.h"
#undef private

namespace llm {
namespace {
constexpr size_t kPageShift = 12U;
constexpr PageLen kPoolPageCount = 8U;
constexpr size_t kPageSize = 1U << kPageShift;
constexpr size_t kPoolSize = kPoolPageCount * kPageSize;

class NullAllocator final : public ge::Allocator {
 public:
  ge::MemBlock *Malloc(size_t) override {
    return nullptr;
  }

  void Free(ge::MemBlock *) override {}
};

class TestSpanAllocator final : public SpanAllocator {
 public:
  explicit TestSpanAllocator(size_t capacity) : span_allocator_(capacity) {}

  PageSpan *Alloc() override {
    if (fail_next_alloc_) {
      fail_next_alloc_ = false;
      return nullptr;
    }
    return span_allocator_.Alloc();
  }

  void Free(PageSpan &span) override {
    ++free_count_;
    span_allocator_.Free(span);
  }

  void FailNextAlloc() {
    fail_next_alloc_ = true;
  }

  size_t GetAvailableSize() const {
    return span_allocator_.GetAvailableSize();
  }

  size_t GetFreeCount() const {
    return free_count_;
  }

 private:
  ObjectAllocator<PageSpan> span_allocator_;
  bool fail_next_alloc_{false};
  size_t free_count_{0U};
};

class TestScalableAllocator final : public ScalableAllocator {
 public:
  using ScalableAllocator::ScalableAllocator;

  void FailNextLayerCreation() {
    fail_next_layer_creation_ = true;
  }

  void RestoreLayerCapacity() {
    span_layer_capacity_ = kPoolPageCount + 1U;
  }

 protected:
  PageSpan *SplitSpan(ge::Allocator &allocator, const SpanLayerId fix_layer_id, const SpanLayerId fit_layer_id,
                      PageSpan *const span, const MemSize size) override {
    if (fail_next_layer_creation_) {
      fail_next_layer_creation_ = false;
      span_layer_capacity_ = fit_layer_id - fix_layer_id;
    }
    return ScalableAllocator::SplitSpan(allocator, fix_layer_id, fit_layer_id, span, size);
  }

 private:
  bool fail_next_layer_creation_{false};
};

ScalableConfig MakeTestConfig() {
  ScalableConfig config{};
  config.page_idem_num = kPageShift;
  config.page_mem_size_total_threshold = kPoolSize;
  config.page_mem_size_in_layer_threshold = kPoolSize;
  config.span_count_in_layer_threshold = kPoolPageCount;
  config.span_prepared_count = 2U;
  config.span_layer_prepared_count = 2U;
  return config;
}

class ScalableAllocatorTest : public ::testing::Test {
 protected:
  ScalableAllocatorTest() : config_(MakeTestConfig()), allocator_(span_allocator_, config_) {}

  void SetUp() override {
    ASSERT_EQ(allocator_.InitFixSizedAllocator(memory_allocator_, storage_.data(), kPoolSize), ge::SUCCESS);
  }

  TestSpanAllocator span_allocator_{2U};
  ScalableConfig config_;
  TestScalableAllocator allocator_;
  NullAllocator memory_allocator_;
  std::array<uint8_t, kPoolSize> storage_{};
};

TEST_F(ScalableAllocatorTest, RestoresSourceWhenBuddyAllocationFails) {
  span_allocator_.FailNextAlloc();

  EXPECT_EQ(allocator_.Alloc(memory_allocator_, kPageSize), nullptr);
  EXPECT_EQ(allocator_.span_layers_[kPoolPageCount]->GetSize(), 1U);
  EXPECT_EQ(span_allocator_.GetFreeCount(), 0U);

  auto *restored_span = allocator_.Alloc(memory_allocator_, kPoolSize);
  ASSERT_NE(restored_span, nullptr);
  allocator_.Free(restored_span);
}

TEST_F(ScalableAllocatorTest, RestoresSourceAndBuddyMetadataWhenLayerCreationFails) {
  const auto available_before_split = span_allocator_.GetAvailableSize();
  allocator_.FailNextLayerCreation();

  EXPECT_EQ(allocator_.Alloc(memory_allocator_, kPageSize), nullptr);
  EXPECT_EQ(allocator_.span_layers_[kPoolPageCount]->GetSize(), 1U);
  EXPECT_EQ(span_allocator_.GetFreeCount(), 1U);
  EXPECT_EQ(span_allocator_.GetAvailableSize(), available_before_split);

  allocator_.RestoreLayerCapacity();
  auto *restored_span = allocator_.Alloc(memory_allocator_, kPoolSize);
  ASSERT_NE(restored_span, nullptr);
  allocator_.Free(restored_span);
}

TEST_F(ScalableAllocatorTest, SplitsSourceAndMergesBuddyOnSuccess) {
  auto *allocated_span = allocator_.Alloc(memory_allocator_, kPageSize);
  ASSERT_NE(allocated_span, nullptr);
  EXPECT_EQ(allocator_.span_layers_[kPoolPageCount - 1U]->GetSize(), 1U);
  EXPECT_EQ(allocated_span->GetPageLen(), 1U);

  allocator_.Free(allocated_span);
  EXPECT_EQ(allocator_.span_layers_[kPoolPageCount]->GetSize(), 1U);
  EXPECT_EQ(span_allocator_.GetFreeCount(), 1U);
}
}  // namespace
}  // namespace llm
