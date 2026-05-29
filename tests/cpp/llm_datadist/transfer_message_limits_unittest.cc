/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>

#include "common/transfer_message_limits.h"

namespace llm {
namespace transfer_message_limits {
namespace {
TEST(TransferMessageLimitsTest, CalcResponseSizeMatchesHeaderAndAddrs) {
  constexpr uint32_t kDstAddrCount = 4U;
  const uint64_t expected = kResponseInfoHeaderSize + static_cast<uint64_t>(kDstAddrCount) * sizeof(uint64_t);
  EXPECT_EQ(CalcResponseSize(kDstAddrCount), expected);
}

TEST(TransferMessageLimitsTest, CalcMinRequestSizeForD2hLayout) {
  constexpr uint32_t kDstAddrCount = 2U;
  constexpr uint32_t kPromptBlocks = 0U;
  const uint64_t expected =
      sizeof(TransferCacheReq) + sizeof(TransferInfo) * (static_cast<uint64_t>(kDstAddrCount) + kPromptBlocks);
  EXPECT_EQ(CalcMinRequestSize(kDstAddrCount, kPromptBlocks, kBufferInfoMultiplierD2h), expected);
}

TEST(TransferMessageLimitsTest, MaxD2hPromptBlocksRespectsTransferInfoLimit) {
  constexpr uint64_t kDstAddrCount = 2U;
  const uint64_t max_prompt_blocks = MaxD2hPromptBlocksForDstCount(kDstAddrCount);
  EXPECT_EQ(max_prompt_blocks, kMaxTransferInfoCount - kDstAddrCount);
  EXPECT_EQ(MaxD2hPromptBlocksForDstCount(kMaxTransferInfoCount), 0U);
}

TEST(TransferMessageLimitsTest, RequestSizeWithinDefaultBuffer) {
  constexpr uint32_t kDstAddrCount = 2U;
  const uint64_t request_size = CalcMinRequestSize(kDstAddrCount, 0U, kBufferInfoMultiplierD2h);
  EXPECT_LE(request_size, kMaxRequestPayloadSize);
}

TEST(TransferMessageLimitsTest, ZeroBufferInfoCountIsValidForD2hLayout) {
  constexpr uint32_t kDstAddrCount = 2U;
  const uint64_t transfer_info_count = static_cast<uint64_t>(kDstAddrCount);
  EXPECT_LE(transfer_info_count, kMaxTransferInfoCount);
  EXPECT_EQ(CalcMinRequestSize(kDstAddrCount, 0U, kBufferInfoMultiplierD2h),
            sizeof(TransferCacheReq) + sizeof(TransferInfo) * kDstAddrCount);
}
}  // namespace
}  // namespace transfer_message_limits
}  // namespace llm
