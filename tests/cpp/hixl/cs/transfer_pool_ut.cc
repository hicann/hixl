/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <vector>

#include "gtest/gtest.h"
#include "hixl/hixl_types.h"
#include "transfer_pool.h"

namespace hixl {
namespace {

// 使用非常规 device_id，避免与其它用例共享 GetInstance 单例时互相干扰
constexpr int32_t kTransferPoolUtDevId = 910246;

class TransferPoolTest : public ::testing::Test {
 protected:
  void TearDown() override { TransferPool::GetInstance(kTransferPoolUtDevId).Finalize(); }
};

TEST_F(TransferPoolTest, AbortWhenNotInitializedIsNoOp) {
  TransferPool::SlotHandle h{};
  h.device_id = kTransferPoolUtDevId;
  h.slot_index = 0U;
  EXPECT_NO_THROW(TransferPool::GetInstance(kTransferPoolUtDevId).Abort(h));
}

TEST_F(TransferPoolTest, AbortDeviceIdMismatchDoesNotFreeSlot) {
  auto &pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_EQ(pool.Initialize(2U), SUCCESS);
  TransferPool::SlotHandle acquired{};
  ASSERT_EQ(pool.Acquire(&acquired), SUCCESS);
  EXPECT_EQ(acquired.device_id, kTransferPoolUtDevId);

  TransferPool::SlotHandle wrong_dev = acquired;
  wrong_dev.device_id = acquired.device_id + 1;
  pool.Abort(wrong_dev);

  TransferPool::SlotHandle second{};
  ASSERT_EQ(pool.Acquire(&second), SUCCESS);
  pool.Release(second);
  pool.Release(acquired);
}

TEST_F(TransferPoolTest, AbortInvalidSlotIndexIsNoOp) {
  auto &pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_EQ(pool.Initialize(1U), SUCCESS);
  TransferPool::SlotHandle bad{};
  bad.device_id = kTransferPoolUtDevId;
  bad.slot_index = 9999U;
  EXPECT_NO_THROW(pool.Abort(bad));
}

TEST_F(TransferPoolTest, AbortInUseSlotReturnsItToPoolAndAcquireSucceedsAgain) {
  auto &pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_EQ(pool.Initialize(2U), SUCCESS);
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool.Acquire(&h), SUCCESS);
  pool.Abort(h);
  TransferPool::SlotHandle a{};
  TransferPool::SlotHandle b{};
  ASSERT_EQ(pool.Acquire(&a), SUCCESS);
  ASSERT_EQ(pool.Acquire(&b), SUCCESS);
  pool.Release(a);
  pool.Release(b);
}

TEST_F(TransferPoolTest, AbortIdleSlotIsNoOp) {
  auto &pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_EQ(pool.Initialize(2U), SUCCESS);
  std::vector<TransferPool::SlotHandle> all;
  ASSERT_EQ(pool.GetAllSlots(all), SUCCESS);
  ASSERT_GE(all.size(), 2U);
  TransferPool::SlotHandle free_slot = all[0];
  EXPECT_NO_THROW(pool.Abort(free_slot));
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool.Acquire(&h), SUCCESS);
  pool.Release(h);
}

TEST_F(TransferPoolTest, DoubleAbortSameHandleSecondIsNoOp) {
  auto &pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_EQ(pool.Initialize(1U), SUCCESS);
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool.Acquire(&h), SUCCESS);
  pool.Abort(h);
  EXPECT_NO_THROW(pool.Abort(h));
  TransferPool::SlotHandle again{};
  ASSERT_EQ(pool.Acquire(&again), SUCCESS);
  pool.Release(again);
}

TEST_F(TransferPoolTest, ReleaseAfterAbortOnSameHandleIsSafe) {
  auto &pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_EQ(pool.Initialize(1U), SUCCESS);
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool.Acquire(&h), SUCCESS);
  pool.Abort(h);
  EXPECT_NO_THROW(pool.Release(h));
  TransferPool::SlotHandle again{};
  ASSERT_EQ(pool.Acquire(&again), SUCCESS);
  pool.Release(again);
}

TEST_F(TransferPoolTest, AbortOneSlotLeavesOtherAcquiredSlotIntact) {
  auto &pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_EQ(pool.Initialize(2U), SUCCESS);
  TransferPool::SlotHandle first{};
  TransferPool::SlotHandle second{};
  ASSERT_EQ(pool.Acquire(&first), SUCCESS);
  ASSERT_EQ(pool.Acquire(&second), SUCCESS);
  pool.Abort(first);
  EXPECT_NO_THROW(pool.Release(second));
  TransferPool::SlotHandle a{};
  TransferPool::SlotHandle b{};
  ASSERT_EQ(pool.Acquire(&a), SUCCESS);
  ASSERT_EQ(pool.Acquire(&b), SUCCESS);
  pool.Release(a);
  pool.Release(b);
}

}  // namespace
}  // namespace hixl
