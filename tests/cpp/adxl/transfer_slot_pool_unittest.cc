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
#include <memory>

#include "adxl/transfer_slot_pool.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"

namespace adxl {
namespace {
constexpr int32_t kUtDeviceId = 0;
constexpr size_t kMaxSlots = 4U;

// Injects aclrtSetStreamFailureMode failure on a chosen InitSlotLocked attempt and tracks context create/destroy.
class TransferSlotInitFailMock : public llm::AutoCommResRuntimeMock {
 public:
  aclError aclrtCreateContext(aclrtContext *context, int32_t deviceId) override {
    ++create_context_count_;
    return llm::AclRuntimeStub::aclrtCreateContext(context, deviceId);
  }

  aclError aclrtDestroyContext(aclrtContext context) override {
    ++destroy_context_count_;
    return llm::AclRuntimeStub::aclrtDestroyContext(context);
  }

  aclError aclrtSetStreamFailureMode(aclrtStream stream, uint64_t mode) override {
    ++init_slot_attempt_count_;
    if (fail_set_stream_failure_mode_on_attempt != 0U &&
        init_slot_attempt_count_ == fail_set_stream_failure_mode_on_attempt) {
      return ACL_ERROR_RT_INTERNAL_ERROR;
    }
    return llm::AclRuntimeStub::aclrtSetStreamFailureMode(stream, mode);
  }

  int create_context_count_ = 0;
  int destroy_context_count_ = 0;
  uint32_t init_slot_attempt_count_ = 0U;
  uint32_t fail_set_stream_failure_mode_on_attempt = 0U;
};
}  // namespace

class TransferSlotPoolUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    llm::AutoCommResRuntimeMock::InstallWithoutHccnConfFile();
  }
  void TearDown() override {
    pool_.Finalize();
    llm::AutoCommResRuntimeMock::ResetWithoutHccnConfFile();
  }

  TransferSlotPool pool_{kUtDeviceId, kMaxSlots};
};

TEST_F(TransferSlotPoolUTest, AcquireReleaseReusesSameSlotIndex) {
  ASSERT_EQ(pool_.Initialize(), SUCCESS);
  SlotHandle first{};
  SlotHandle second{};
  ASSERT_EQ(pool_.Acquire(&first), SUCCESS);
  EXPECT_NE(first.ctx, nullptr);
  EXPECT_NE(first.stream, nullptr);
  EXPECT_NE(first.dev_const_one, nullptr);
  pool_.Release(first);
  ASSERT_EQ(pool_.Acquire(&second), SUCCESS);
  EXPECT_EQ(first.slot_index, second.slot_index);
  pool_.Release(second);
}

TEST_F(TransferSlotPoolUTest, AbortReturnsSlotToPool) {
  ASSERT_EQ(pool_.Initialize(), SUCCESS);
  SlotHandle handle{};
  ASSERT_EQ(pool_.Acquire(&handle), SUCCESS);
  pool_.Abort(handle);
  SlotHandle again{};
  ASSERT_EQ(pool_.Acquire(&again), SUCCESS);
  EXPECT_EQ(handle.slot_index, again.slot_index);
  pool_.Release(again);
}

TEST_F(TransferSlotPoolUTest, CapacityLimitReached) {
  ASSERT_EQ(pool_.Initialize(), SUCCESS);
  std::vector<SlotHandle> handles(kMaxSlots);
  for (size_t i = 0; i < kMaxSlots; ++i) {
    ASSERT_EQ(pool_.Acquire(&handles[i]), SUCCESS);
  }
  SlotHandle overflow{};
  EXPECT_EQ(pool_.Acquire(&overflow), RESOURCE_EXHAUSTED);
  for (auto &handle : handles) {
    pool_.Release(handle);
  }
}

TEST_F(TransferSlotPoolUTest, HostFlagRecyclePerSlot) {
  ASSERT_EQ(pool_.Initialize(), SUCCESS);
  SlotHandle slot{};
  ASSERT_EQ(pool_.Acquire(&slot), SUCCESS);
  void *flag1 = nullptr;
  void *flag2 = nullptr;
  ASSERT_EQ(pool_.AcquireHostFlag(slot, flag1), SUCCESS);
  ASSERT_EQ(pool_.AcquireHostFlag(slot, flag2), SUCCESS);
  pool_.ReleaseHostFlag(slot, flag1);
  void *flag_reused = nullptr;
  ASSERT_EQ(pool_.AcquireHostFlag(slot, flag_reused), SUCCESS);
  EXPECT_EQ(flag1, flag_reused);
  pool_.ReleaseHostFlag(slot, flag2);
  pool_.ReleaseHostFlag(slot, flag_reused);
  pool_.Release(slot);
}

TEST_F(TransferSlotPoolUTest, AcquireInitFailureCleansContextAndReturnsSlot) {
  auto mock = std::make_shared<TransferSlotInitFailMock>();
  mock->fail_set_stream_failure_mode_on_attempt = 1U;
  llm::AclRuntimeStub::SetInstance(mock);

  TransferSlotPool pool(kUtDeviceId, kMaxSlots);
  ASSERT_EQ(pool.Initialize(), SUCCESS);
  SlotHandle handle{};
  EXPECT_NE(pool.Acquire(&handle), SUCCESS);
  EXPECT_EQ(mock->create_context_count_, 1);
  EXPECT_EQ(mock->destroy_context_count_, 1);

  mock->fail_set_stream_failure_mode_on_attempt = 0U;
  ASSERT_EQ(pool.Acquire(&handle), SUCCESS);
  EXPECT_EQ(handle.slot_index, 0U);
  pool.Release(handle);
  pool.Finalize();
}

TEST_F(TransferSlotPoolUTest, AbortReinitFailureReturnsSlotToPool) {
  auto mock = std::make_shared<TransferSlotInitFailMock>();
  llm::AclRuntimeStub::SetInstance(mock);

  TransferSlotPool pool(kUtDeviceId, kMaxSlots);
  ASSERT_EQ(pool.Initialize(), SUCCESS);
  SlotHandle handle{};
  ASSERT_EQ(pool.Acquire(&handle), SUCCESS);
  EXPECT_EQ(mock->create_context_count_, 1);
  EXPECT_EQ(mock->destroy_context_count_, 0);

  mock->fail_set_stream_failure_mode_on_attempt = 2U;
  pool.Abort(handle);
  EXPECT_EQ(mock->create_context_count_, 2);
  EXPECT_EQ(mock->destroy_context_count_, 2);

  mock->fail_set_stream_failure_mode_on_attempt = 0U;
  SlotHandle again{};
  ASSERT_EQ(pool.Acquire(&again), SUCCESS);
  EXPECT_EQ(handle.slot_index, again.slot_index);
  pool.Release(again);
  pool.Finalize();
}

}  // namespace adxl
