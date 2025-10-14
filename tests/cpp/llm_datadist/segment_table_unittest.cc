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
class SegmentTableUTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(SegmentTableUTest, TestContains) {
  SegmentTable table;
  table.AddRange("127.0.0.1:10000", 100, 200, MemType::MEM_DEVICE);
  table.AddRange("127.0.0.1:10000", 200, 300, MemType::MEM_DEVICE);
  auto channel = table.FindSegment("127.0.0.1:10000", 150, 300);
  ASSERT_NE(channel, nullptr);
}

TEST_F(SegmentTableUTest, TestNotContains) {
  SegmentTable table;
  table.AddRange("127.0.0.1:10000", 100, 200, MemType::MEM_DEVICE);
  table.AddRange("127.0.0.1:10000", 200, 300, MemType::MEM_DEVICE);
  auto channel = table.FindSegment("127.0.0.1:10000", 150, 310);
  ASSERT_EQ(channel, nullptr);
}
}  // namespace adxl
