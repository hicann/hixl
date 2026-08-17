/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstring>

#include <gtest/gtest.h>

#include "adxl/adxl_engine.h"
#include "fabric_mem/virtual_memory_manager.h"

namespace adxl {
namespace {
constexpr size_t kAllocSize = 4096U;

class AdxlMallocMemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    hixl::VirtualMemoryManager::GetInstance().Finalize();
    ASSERT_EQ(hixl::VirtualMemoryManager::GetInstance().Initialize(), hixl::SUCCESS);
  }

  void TearDown() override {
    hixl::VirtualMemoryManager::GetInstance().Finalize();
  }
};

bool IsAllZero(const ShareableHandle &handle) {
  for (const auto byte : handle.data) {
    if (byte != 0U) {
      return false;
    }
  }
  return true;
}
}  // namespace

TEST_F(AdxlMallocMemTest, ExportToShareableHandleAfterMallocMem) {
  void *ptr = nullptr;
  ASSERT_EQ(AdxlEngine::MallocMem(MEM_HOST, kAllocSize, &ptr), SUCCESS);
  ASSERT_NE(ptr, nullptr);

  ShareableHandle handle{};
  ASSERT_EQ(AdxlEngine::ExportToShareableHandle(ptr, handle), SUCCESS);
  EXPECT_FALSE(IsAllZero(handle));

  EXPECT_EQ(AdxlEngine::FreeMem(ptr), SUCCESS);
}

TEST_F(AdxlMallocMemTest, ExportToShareableHandleRejectsForeignAddress) {
  ShareableHandle handle{};
  EXPECT_EQ(AdxlEngine::ExportToShareableHandle(nullptr, handle), PARAM_INVALID);

  uint8_t stack_buffer[kAllocSize] = {};
  EXPECT_EQ(AdxlEngine::ExportToShareableHandle(stack_buffer, handle), PARAM_INVALID);
}

TEST_F(AdxlMallocMemTest, ExportToShareableHandleFailsAfterFree) {
  void *ptr = nullptr;
  ASSERT_EQ(AdxlEngine::MallocMem(MEM_HOST, kAllocSize, &ptr), SUCCESS);
  ASSERT_EQ(AdxlEngine::FreeMem(ptr), SUCCESS);

  ShareableHandle handle{};
  EXPECT_EQ(AdxlEngine::ExportToShareableHandle(ptr, handle), PARAM_INVALID);
}
}  // namespace adxl
