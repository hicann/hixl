/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <vector>
#include <gtest/gtest.h>

#include "adxl/segment_table.h"

namespace adxl {
namespace {
constexpr char kChannelId[] = "test";
constexpr uint32_t kSegmentStart1 = 100;
constexpr uint32_t kSegmentMiddle = 150;
constexpr uint32_t kSegmentStart2 = 200;
constexpr uint32_t kSegmentEnd2 = 205;
constexpr uint32_t kSegmentStart3 = 206;
constexpr uint32_t kSegmentEnd3 = 300;
constexpr uint32_t kSegmentQueryStart2 = 201;
constexpr uint32_t kSegmentQueryEnd = 310;
}  // namespace
class SegmentTableUTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(SegmentTableUTest, TestContains) {
  SegmentTable table;
  table.AddRange(kChannelId, kSegmentStart3, kSegmentEnd3, MemType::MEM_DEVICE);
  table.AddRange(kChannelId, kSegmentStart2, kSegmentEnd2, MemType::MEM_DEVICE);
  table.AddRange(kChannelId, kSegmentStart1, kSegmentStart2, MemType::MEM_DEVICE);
  auto channel = table.FindSegment(kChannelId, kSegmentMiddle, kSegmentEnd3);
  ASSERT_NE(channel, nullptr);
  channel = table.FindSegment(kChannelId, kSegmentQueryStart2, kSegmentEnd3);
  ASSERT_NE(channel, nullptr);
  channel = table.FindSegment(kChannelId, kSegmentStart3, kSegmentEnd3);
  ASSERT_NE(channel, nullptr);
}

TEST_F(SegmentTableUTest, TestRemoveContains) {
  SegmentTable table;
  table.AddRange(kChannelId, kSegmentStart1, kSegmentStart2, MemType::MEM_DEVICE);
  table.AddRange(kChannelId, kSegmentStart2, kSegmentEnd3, MemType::MEM_DEVICE);
  table.AddRange(kChannelId, kSegmentStart1, kSegmentStart2, MemType::MEM_DEVICE);
  table.RemoveRange(kChannelId, kSegmentStart1, kSegmentStart2, MemType::MEM_DEVICE);
  auto channel = table.FindSegment(kChannelId, kSegmentMiddle, kSegmentEnd3);
  ASSERT_NE(channel, nullptr);
}

TEST_F(SegmentTableUTest, TestRemoveNotContains) {
  SegmentTable table;
  table.AddRange(kChannelId, kSegmentStart1, kSegmentStart2, MemType::MEM_DEVICE);
  table.AddRange(kChannelId, kSegmentStart2, kSegmentEnd3, MemType::MEM_DEVICE);
  table.RemoveRange(kChannelId, kSegmentStart1, kSegmentStart2, MemType::MEM_DEVICE);
  auto channel = table.FindSegment(kChannelId, kSegmentMiddle, kSegmentEnd3);
  ASSERT_EQ(channel, nullptr);
}

TEST_F(SegmentTableUTest, TestNotContains) {
  SegmentTable table;
  table.AddRange(kChannelId, kSegmentStart1, kSegmentStart2, MemType::MEM_DEVICE);
  table.AddRange(kChannelId, kSegmentStart2, kSegmentEnd3, MemType::MEM_DEVICE);
  auto channel = table.FindSegment(kChannelId, kSegmentMiddle, kSegmentQueryEnd);
  ASSERT_EQ(channel, nullptr);
}
}  // namespace adxl
