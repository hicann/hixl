/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */


#include <gtest/gtest.h>
#include "common/aligned_ptr.h"

using namespace std;
using namespace ::testing;

namespace llm {
class AlignedPtrTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  // 在测试类中进行清理工作，如果需要的话
  void TearDown() override {}
};

TEST_F(AlignedPtrTest, NormalAlignment) {
  const size_t buffer_size = 1024;
  const size_t alignment = 64;
  AlignedPtr ptr(buffer_size, alignment);
  EXPECT_NE(ptr.Get(), nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr.Get()) % alignment, 0);
}

TEST_F(AlignedPtrTest, ZeroBufferSize) {
  AlignedPtr ptr(0, 16);
  EXPECT_EQ(ptr.Get(), nullptr);
}

TEST_F(AlignedPtrTest, ZeroAlignmentWithNonZeroSize) {
  const size_t buffer_size = 1024;
  AlignedPtr ptr(buffer_size, 0);
  EXPECT_NE(ptr.Get(), nullptr);
}

TEST_F(AlignedPtrTest, AllocationFailure) {
  const size_t huge_size = std::numeric_limits<size_t>::max();
  AlignedPtr ptr(huge_size, 16);
  EXPECT_EQ(ptr.Get(), nullptr);
}

TEST_F(AlignedPtrTest, AlignmentLargerThanSize) {
  const size_t buffer_size = 16;
  const size_t alignment = 64;
  AlignedPtr ptr(buffer_size, alignment);
  EXPECT_NE(ptr.Get(), nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr.Get()) % alignment, 0);
}
}  // namespace llm
