/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <vector>
#include <gtest/gtest.h>

#include "adxl/statistic_manager.h"

namespace adxl {
namespace {
constexpr char kChannelId[] = "test";
constexpr uint64_t kCost = 100;
}  // namespace
class StatisticManagerUTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(StatisticManagerUTest, TestDump) {
  StatisticManager::GetInstance().UpdateBufferTransferCost(kChannelId, kCost);
  StatisticManager::GetInstance().Dump();
}

}  // namespace adxl
