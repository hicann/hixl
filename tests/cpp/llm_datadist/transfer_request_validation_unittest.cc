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
#include "common/transfer_request_validation.h"

namespace llm {
namespace transfer_request_validation {
namespace {
TEST(TransferRequestValidationTest, SrcTensorRangeAcceptsWholeCacheAndLayerRange) {
  // 整段 cache：start == 0，num == cache num。
  EXPECT_EQ(CheckSrcTensorRange(0U, 64U, 64U), ge::SUCCESS);
  // 按层拉取：区间落在 cache 内。
  EXPECT_EQ(CheckSrcTensorRange(8U, 4U, 64U), ge::SUCCESS);
  // 空区间。
  EXPECT_EQ(CheckSrcTensorRange(0U, 0U, 64U), ge::SUCCESS);
}

TEST(TransferRequestValidationTest, SrcTensorRangeRejectsOutOfRangeLayerRange) {
  EXPECT_EQ(CheckSrcTensorRange(62U, 4U, 64U), ge::LLM_PARAM_INVALID);
  EXPECT_EQ(CheckSrcTensorRange(65U, 1U, 64U), ge::LLM_PARAM_INVALID);
  // start_index 超过 cache 数时不能因为无符号减法下溢而被判为合法。
  EXPECT_EQ(CheckSrcTensorRange(UINT64_MAX, 0U, 64U), ge::LLM_PARAM_INVALID);
}

TEST(TransferRequestValidationTest, SrcTensorRangeRejectsNonZeroStartOffsetWithLayerRange) {
  // 按层拉取（src_tensor_indices_size != 0）时 start_index 会生效，区间必须整体落在 cache 内。
  // 注意：整段 cache 场景下调用方会把 start_index 统一视作 0（忽略对端值），
  // 因此那条路径不会走到这里的拒绝分支。
  EXPECT_EQ(CheckSrcTensorRange(1U, 64U, 64U), ge::LLM_PARAM_INVALID);
  EXPECT_EQ(CheckSrcTensorRange(63U, 64U, 64U), ge::LLM_PARAM_INVALID);
  EXPECT_EQ(CheckSrcTensorRange(63U, 1U, 64U), ge::SUCCESS);
}

TEST(TransferRequestValidationTest, TensorCountMustNotOverrunDstAddrSection) {
  // H2D 用张量下标索引 dst_addr 段，张量数不得超过该段容量。
  EXPECT_EQ(CheckTensorCountWithinDstAddr(4U, 4U), ge::SUCCESS);
  EXPECT_EQ(CheckTensorCountWithinDstAddr(1U, 4U), ge::SUCCESS);
  EXPECT_EQ(CheckTensorCountWithinDstAddr(5U, 4U), ge::LLM_PARAM_INVALID);
  // 对端只给 1 个 dst_addr 却声明 64 个张量，正是语义错位读的场景。
  EXPECT_EQ(CheckTensorCountWithinDstAddr(64U, 1U), ge::LLM_PARAM_INVALID);
}

TEST(TransferRequestValidationTest, BlockSpanAloneIsNotEnough) {
  // 检视意见指出的场景：下标与长度各自合法，但乘积越过张量。
  // tensor_size=8MB（batch_size=1，故 stride 也是 8MB）、request.block_size=4MB、
  // block_start_index=1、buffer_len=8MB：
  //   - block_start_index(1) < ceil(8MB/4MB)=2            -> 下标检查通过
  //   - buffer_len(8MB) <= stride(8MB)                    -> 长度检查通过
  //   - 但 1*4MB + 8MB = 12MB > 8MB                       -> 实际读越界 4MB
  constexpr uint64_t kTensorSize = 8U * 1024U * 1024U;
  constexpr uint64_t kBlockSize = 4U * 1024U * 1024U;
  EXPECT_EQ(CheckBlockSpanWithinRegion(1U, kBlockSize, kTensorSize, kTensorSize), ge::LLM_PARAM_INVALID);
  // 同一个请求只用单个 stride 做界会误拒合法的「cont -> 离散块」布局（下标表示张量内的批次），
  // 所以界必须取张量分配大小；下标 0 时两种界都通过。
  EXPECT_EQ(CheckBlockSpanWithinRegion(0U, kBlockSize, kTensorSize, kTensorSize), ge::SUCCESS);
}

TEST(TransferRequestValidationTest, BlockSpanUsesDispatchedCountNotBufferLen) {
  // D2D 的「cont -> 离散块 + remainder」布局：张量 28B、块大小 8B，
  // 最后一个块实际只下发 remainder=4B。若用 buffer_len(8B) 当长度，会算出 3*8+8=32 > 28 而误拒；
  // 用实际下发的 4B 则是 3*8+4=28，正好填满张量。
  constexpr uint64_t kTensorSize = 28U;
  constexpr uint64_t kBlockSize = 8U;
  EXPECT_EQ(CheckBlockSpanWithinRegion(3U, kBlockSize, 8U, kTensorSize), ge::LLM_PARAM_INVALID);
  EXPECT_EQ(CheckBlockSpanWithinRegion(3U, kBlockSize, 4U, kTensorSize), ge::SUCCESS);
  // 累加仍不能越过张量。
  EXPECT_EQ(CheckBlockSpanWithinRegion(3U, kBlockSize, 5U, kTensorSize), ge::LLM_PARAM_INVALID);
}

TEST(TransferRequestValidationTest, D2dBlockSpanAccountsForBatchOffset) {
  // D2D 单边读的起点是 base + offset + block_index * block_size，其中 offset = batch_index * stride。
  // tensor_size=8MB、stride=1MB、batch_size=8，对端给 batch_index=7（offset=7MB）、block_size=1MB、
  // block_start_index=1、count=1MB：下标检查、长度检查、以及只按 tensor_size 做界都会通过，
  // 但实际地址是 7MB + 1MB，读到 [8MB, 9MB)，越过张量 1MB。
  constexpr uint64_t kTensorSize = 8U * 1024U * 1024U;
  constexpr uint64_t kStride = 1U * 1024U * 1024U;
  constexpr uint64_t kLastBatchOffset = 7U * kStride;
  EXPECT_EQ(CheckBlockSpanWithinRegion(1U, kStride, kStride, kTensorSize), ge::SUCCESS);
  EXPECT_EQ(CheckBlockSpanWithinD2dRegion(1U, kStride, kStride, kTensorSize, kLastBatchOffset), ge::LLM_PARAM_INVALID);
  // 最后一个 batch 从 offset 起正好读到张量末尾，是合法的。
  EXPECT_EQ(CheckBlockSpanWithinD2dRegion(0U, kStride, kStride, kTensorSize, kLastBatchOffset), ge::SUCCESS);
  // offset 为 0（block cache、整段拉取）时结果与只按张量大小做界一致。
  EXPECT_EQ(CheckBlockSpanWithinD2dRegion(1U, kStride, kStride, kTensorSize, 0U), ge::SUCCESS);
  EXPECT_EQ(CheckBlockSpanWithinD2dRegion(7U, kStride, kStride, kTensorSize, 0U), ge::SUCCESS);
  EXPECT_EQ(CheckBlockSpanWithinD2dRegion(8U, kStride, kStride, kTensorSize, 0U), ge::LLM_PARAM_INVALID);
  // offset 自身越过张量时不能因为无符号减法下溢而被判合法。
  EXPECT_EQ(CheckBlockSpanWithinD2dRegion(0U, kStride, kStride, kTensorSize, kTensorSize + 1U), ge::LLM_PARAM_INVALID);
}

TEST(TransferRequestValidationTest, BlockSpanMustStayWithinRegion) {
  constexpr uint64_t kStride = 8U * 1024U * 1024U;
  constexpr uint64_t kTensorSize = 128U * 1024U * 1024U;
  constexpr uint64_t kBlockSize = 8U * 1024U * 1024U;
  // block cache：越界下标会被拒绝。
  EXPECT_EQ(CheckBlockSpanWithinRegion(15U, kBlockSize, kBlockSize, kTensorSize), ge::SUCCESS);
  EXPECT_EQ(CheckBlockSpanWithinRegion(16U, kBlockSize, kBlockSize, kTensorSize), ge::LLM_PARAM_INVALID);
  EXPECT_EQ(CheckBlockSpanWithinRegion(UINT64_MAX, kBlockSize, kBlockSize, kTensorSize), ge::LLM_PARAM_INVALID);
  // 下标本身在范围内，但跨块长度越过分配区。
  EXPECT_EQ(CheckBlockSpanWithinRegion(15U, kBlockSize, 2U * kBlockSize, kTensorSize), ge::LLM_PARAM_INVALID);
  // cont cache：region 用 stride，合法请求 block_start_index 为 0。
  EXPECT_EQ(CheckBlockSpanWithinRegion(0U, kStride, kStride, kStride), ge::SUCCESS);
  EXPECT_EQ(CheckBlockSpanWithinRegion(1U, kStride, kStride, kStride), ge::LLM_PARAM_INVALID);
  // 长度本身超过 region。
  EXPECT_EQ(CheckBlockSpanWithinRegion(0U, kBlockSize, kStride + 1U, kStride), ge::LLM_PARAM_INVALID);
  // block_size 为 0 时偏移恒为 0，不构成越界。
  EXPECT_EQ(CheckBlockSpanWithinRegion(UINT64_MAX, 0U, 0U, kStride), ge::SUCCESS);
  // 极端 footprint 不应因乘法溢出而被判合法。
  EXPECT_EQ(CheckBlockSpanWithinRegion(UINT64_MAX, UINT64_MAX, kStride, kStride), ge::LLM_PARAM_INVALID);
}

TEST(TransferRequestValidationTest, D2dBlockSizeMustNotBeZero) {
  // 对端给了非 0 的 block_size 时，本端 stride 是否为 0 都不影响整除。
  EXPECT_EQ(CheckD2dBlockSize(2U * 1024U * 1024U, 0U), ge::SUCCESS);
  EXPECT_EQ(CheckD2dBlockSize(2U * 1024U * 1024U, 8U * 1024U * 1024U), ge::SUCCESS);
  // request.block_size 为 0 时回落到本端 stride，stride 非 0 仍可整除。
  EXPECT_EQ(CheckD2dBlockSize(0U, 8U * 1024U * 1024U), ge::SUCCESS);
  // 两者同时为 0 会让 GetSendTask 里的 tensor_size % / block_size 除零，必须挡掉。
  EXPECT_EQ(CheckD2dBlockSize(0U, 0U), ge::LLM_PARAM_INVALID);
}

TEST(TransferRequestValidationTest, SyncFlagAddressesMustAllBeProvided) {
  const uint64_t provided[] = {0x1000U, 0x1004U};
  EXPECT_EQ(CheckSyncFlagAddresses(provided, 2U), ge::SUCCESS);
  // 只回短响应时，未被覆盖的槽位保持客户端预置的 0，说明对端没提供该地址。
  const uint64_t truncated[] = {0x1000U, 0U};
  EXPECT_EQ(CheckSyncFlagAddresses(truncated, 2U), ge::LLM_PARAM_INVALID);
  const uint64_t empty[] = {0U, 0U};
  EXPECT_EQ(CheckSyncFlagAddresses(empty, 2U), ge::LLM_PARAM_INVALID);
  // 不需要任何地址时视为通过。
  EXPECT_EQ(CheckSyncFlagAddresses(empty, 0U), ge::SUCCESS);
  EXPECT_EQ(CheckSyncFlagAddresses(nullptr, 0U), ge::LLM_PARAM_INVALID);
}

TEST(TransferRequestValidationTest, ResponseTransferCountToleratesLegacyPeers) {
  // 老版本对端不填 transfer_count（保持 0），跳过比较以免影响跨版本互通。
  EXPECT_EQ(CheckResponseTransferCount(0U, 2U), ge::SUCCESS);
  EXPECT_EQ(CheckResponseTransferCount(2U, 2U), ge::SUCCESS);
  EXPECT_EQ(CheckResponseTransferCount(3U, 2U), ge::SUCCESS);
  // 对端明确回了比客户端需要更少的地址，说明响应被截断。
  EXPECT_EQ(CheckResponseTransferCount(1U, 2U), ge::LLM_PARAM_INVALID);
}

TEST(TransferRequestValidationTest, RecvFlagAreaMustFitResponsePayload) {
  constexpr uint64_t kMaxPayload = transfer_message_limits::kMaxResponsePayloadSize;
  // 正常 D2H 请求只有 2 个 dst 地址。
  EXPECT_EQ(CheckRecvFlagArea(2U, transfer_message_limits::CalcResponseSize(2U), kMaxPayload, sizeof(int32_t)),
            ge::SUCCESS);
  // 临界值：resp(32 + 8d) + 4d <= 16376 => d <= 1362。
  EXPECT_EQ(CheckRecvFlagArea(1362U, transfer_message_limits::CalcResponseSize(1362U), kMaxPayload, sizeof(int32_t)),
            ge::SUCCESS);
  EXPECT_EQ(CheckRecvFlagArea(1363U, transfer_message_limits::CalcResponseSize(1363U), kMaxPayload, sizeof(int32_t)),
            ge::LLM_PARAM_INVALID);
  // 对端把 dst_addr_count 顶到报文上限时，flag 区会写出响应缓冲区之外。
  constexpr uint32_t kMaxDstAddrCount = transfer_message_limits::kMaxDstAddrCount;
  EXPECT_EQ(CheckRecvFlagArea(kMaxDstAddrCount, transfer_message_limits::CalcResponseSize(kMaxDstAddrCount),
                              kMaxPayload, sizeof(int32_t)),
            ge::LLM_PARAM_INVALID);
}

TEST(TransferRequestValidationTest, D2hDstBufferSizeMustBeSane) {
  // 内置客户端固定发 32MB。
  EXPECT_EQ(CheckD2hDstBufferSize(32U * 1024U * 1024U), ge::SUCCESS);
  EXPECT_EQ(CheckD2hDstBufferSize(kMinDstBufferSize), ge::SUCCESS);
  // 0 会让分块循环停不下来。
  EXPECT_EQ(CheckD2hDstBufferSize(0U), ge::LLM_PARAM_INVALID);
  EXPECT_EQ(CheckD2hDstBufferSize(kMinDstBufferSize - 1U), ge::LLM_PARAM_INVALID);
  // 超过 uint32_t 会被截断，可能截成 0 或极小值。
  EXPECT_EQ(CheckD2hDstBufferSize(static_cast<uint64_t>(UINT32_MAX) + 1U), ge::LLM_PARAM_INVALID);
  EXPECT_EQ(CheckD2hDstBufferSize(0x1ULL << 32U), ge::LLM_PARAM_INVALID);
}
}  // namespace
}  // namespace transfer_request_validation
}  // namespace llm
