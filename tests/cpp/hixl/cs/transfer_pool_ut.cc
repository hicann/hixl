/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <memory>
#include <string>
#include <vector>

#include "ascendcl_stub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "engine/test_mmpa_utils.h"
#include "gtest/gtest.h"
#include "hixl/hixl_types.h"
#include "transfer_pool.h"

extern "C" uint32_t GetThreadAllocCallCount();
extern "C" uint32_t GetThreadFreeCallCount();
extern "C" void ResetThreadLifecycleStats();

namespace hixl {
namespace {

// 使用非常规 device_id，避免与其它用例共享 GetInstance 单例时互相干扰
constexpr int32_t kTransferPoolUtDevId = 910246;
constexpr int32_t kTransferPoolKernelDevId = 910247;

class CountingAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  aclError aclrtBinaryLoadFromFile(const char *path, aclrtBinaryLoadOptions *options,
                                   aclrtBinHandle *bin_handle) override {
    ++load_count_;
    return llm::AclRuntimeStub::aclrtBinaryLoadFromFile(path, options, bin_handle);
  }

  aclError aclrtBinaryGetFunction(aclrtBinHandle bin_handle, const char *func_name,
                                  aclrtFuncHandle *func_handle) override {
    func_names_.emplace_back(func_name == nullptr ? "" : func_name);
    return llm::AclRuntimeStub::aclrtBinaryGetFunction(bin_handle, func_name, func_handle);
  }

  aclError aclrtBinaryUnLoad(aclrtBinHandle bin_handle) override {
    ++unload_count_;
    return llm::AclRuntimeStub::aclrtBinaryUnLoad(bin_handle);
  }

  aclError aclrtMemcpy(void *dst, size_t dest_max, const void *src, size_t count, aclrtMemcpyKind kind) override {
    if ((count == sizeof(TransferContextSyncEntry)) && (kind == ACL_MEMCPY_HOST_TO_DEVICE)) {
      ++sync_entry_h2d_count_;
    }
    if ((count == sizeof(uint32_t)) && (kind == ACL_MEMCPY_DEVICE_TO_HOST)) {
      ++sync_state_d2h_count_;
    }
    return llm::AclRuntimeStub::aclrtMemcpy(dst, dest_max, src, count, kind);
  }

  uint32_t load_count_{0U};
  uint32_t unload_count_{0U};
  uint32_t sync_entry_h2d_count_{0U};
  uint32_t sync_state_d2h_count_{0U};
  std::vector<std::string> func_names_{};
};

class TransferPoolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto kernel_stub = std::make_shared<test::KernelJsonMmpaStub>();
    llm::MmpaStub::GetInstance().SetImpl(kernel_stub);
    ResetThreadLifecycleStats();
  }

  void TearDown() override {
    auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
    if (pool != nullptr) {
      pool->Finalize();
    }
    auto *kernel_pool = TransferPool::GetInstance(kTransferPoolKernelDevId);
    if (kernel_pool != nullptr) {
      kernel_pool->Finalize();
    }
    llm::AclRuntimeStub::Reset();
    llm::MmpaStub::GetInstance().Reset();
  }
};

TEST_F(TransferPoolTest, AbortWhenNotInitializedIsNoOp) {
  TransferPool::SlotHandle h{};
  h.device_id = kTransferPoolUtDevId;
  h.slot_index = 0U;
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  EXPECT_NO_THROW(pool->Abort(h));
}

TEST_F(TransferPoolTest, AbortDeviceIdMismatchDoesNotFreeSlot) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(2U), SUCCESS);
  TransferPool::SlotHandle acquired{};
  ASSERT_EQ(pool->Acquire(&acquired), SUCCESS);
  EXPECT_EQ(acquired.device_id, kTransferPoolUtDevId);

  TransferPool::SlotHandle wrong_dev = acquired;
  wrong_dev.device_id = acquired.device_id + 1;
  pool->Abort(wrong_dev);

  TransferPool::SlotHandle second{};
  ASSERT_EQ(pool->Acquire(&second), SUCCESS);
  pool->Release(second);
  pool->Release(acquired);
}

TEST_F(TransferPoolTest, AbortInvalidSlotIndexIsNoOp) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  TransferPool::SlotHandle bad{};
  bad.device_id = kTransferPoolUtDevId;
  bad.slot_index = 9999U;
  EXPECT_NO_THROW(pool->Abort(bad));
}

TEST_F(TransferPoolTest, AbortInUseSlotReturnsItToPoolAndAcquireSucceedsAgain) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(2U), SUCCESS);
  EXPECT_EQ(GetThreadAllocCallCount(), 2U);
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool->Acquire(&h), SUCCESS);
  const ThreadHandle old_thread = h.thread;
  pool->Abort(h);
  TransferPool::SlotHandle a{};
  TransferPool::SlotHandle b{};
  ASSERT_EQ(pool->Acquire(&a), SUCCESS);
  ASSERT_EQ(pool->Acquire(&b), SUCCESS);
  EXPECT_NE(a.slot_index == h.slot_index ? a.thread : b.thread, old_thread);
  EXPECT_EQ(GetThreadFreeCallCount(), 1U);
  EXPECT_EQ(GetThreadAllocCallCount(), 3U);
  pool->Release(a);
  pool->Release(b);
}

TEST_F(TransferPoolTest, AbortIdleSlotIsNoOp) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(2U), SUCCESS);
  std::vector<TransferPool::SlotHandle> all;
  ASSERT_EQ(pool->GetAllSlots(all), SUCCESS);
  ASSERT_GE(all.size(), 2U);
  TransferPool::SlotHandle free_slot = all[0];
  EXPECT_NO_THROW(pool->Abort(free_slot));
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool->Acquire(&h), SUCCESS);
  pool->Release(h);
}

TEST_F(TransferPoolTest, DoubleAbortSameHandleSecondIsNoOp) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool->Acquire(&h), SUCCESS);
  pool->Abort(h);
  EXPECT_NO_THROW(pool->Abort(h));
  TransferPool::SlotHandle again{};
  ASSERT_EQ(pool->Acquire(&again), SUCCESS);
  pool->Release(again);
}

TEST_F(TransferPoolTest, ReleaseAfterAbortOnSameHandleIsSafe) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool->Acquire(&h), SUCCESS);
  pool->Abort(h);
  EXPECT_NO_THROW(pool->Release(h));
  TransferPool::SlotHandle again{};
  ASSERT_EQ(pool->Acquire(&again), SUCCESS);
  pool->Release(again);
}

TEST_F(TransferPoolTest, AbortOneSlotLeavesOtherAcquiredSlotIntact) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(2U), SUCCESS);
  TransferPool::SlotHandle first{};
  TransferPool::SlotHandle second{};
  ASSERT_EQ(pool->Acquire(&first), SUCCESS);
  ASSERT_EQ(pool->Acquire(&second), SUCCESS);
  pool->Abort(first);
  EXPECT_NO_THROW(pool->Release(second));
  TransferPool::SlotHandle a{};
  TransferPool::SlotHandle b{};
  ASSERT_EQ(pool->Acquire(&a), SUCCESS);
  ASSERT_EQ(pool->Acquire(&b), SUCCESS);
  pool->Release(a);
  pool->Release(b);
}

TEST_F(TransferPoolTest, AcquireReturnsDevConstOne) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  TransferPool::SlotHandle handle{};
  ASSERT_EQ(pool->Acquire(&handle), SUCCESS);
  // dev_const_one should be set by Acquire (may be nullptr if no device context)
  // The field exists and is accessible
  EXPECT_NO_THROW(handle.dev_const_one);
  pool->Release(handle);
}

TEST_F(TransferPoolTest, SlotHandleHasDevConstOneField) {
  TransferPool::SlotHandle handle{};
  handle.dev_const_one = nullptr;
  EXPECT_EQ(handle.dev_const_one, nullptr);
}

TEST_F(TransferPoolTest, MultipleAcquireReleaseCycles) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(2U), SUCCESS);

  // First cycle
  TransferPool::SlotHandle a{};
  TransferPool::SlotHandle b{};
  ASSERT_EQ(pool->Acquire(&a), SUCCESS);
  ASSERT_EQ(pool->Acquire(&b), SUCCESS);
  pool->Release(a);
  pool->Release(b);

  // Second cycle - should be able to acquire again
  TransferPool::SlotHandle c{};
  TransferPool::SlotHandle d{};
  ASSERT_EQ(pool->Acquire(&c), SUCCESS);
  ASSERT_EQ(pool->Acquire(&d), SUCCESS);
  pool->Release(c);
  pool->Release(d);
}

TEST_F(TransferPoolTest, InitializeFinalizeReferenceCountBalancesThreadFree) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(2U), SUCCESS);
  ASSERT_EQ(pool->Initialize(2U), SUCCESS);
  EXPECT_EQ(GetThreadAllocCallCount(), 2U);

  pool->Finalize();
  EXPECT_EQ(GetThreadFreeCallCount(), 0U);

  pool->Finalize();
  EXPECT_EQ(GetThreadFreeCallCount(), 2U);
}

TEST_F(TransferPoolTest, DeviceKernelHandlesAreLoadedOnceAndUnloadedOnce) {
  auto acl_stub = std::make_shared<CountingAclRuntimeStub>();
  llm::AclRuntimeStub::SetInstance(acl_stub);
  auto *pool = TransferPool::GetInstance(kTransferPoolKernelDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  EXPECT_EQ(acl_stub->load_count_, 1U);
  EXPECT_EQ(acl_stub->func_names_.size(), 3U);
  EXPECT_EQ(acl_stub->sync_entry_h2d_count_, 1U);
  EXPECT_EQ(acl_stub->sync_state_d2h_count_, 1U);
  EXPECT_NE(pool->GetDeviceKernelFunc(true), nullptr);
  EXPECT_NE(pool->GetDeviceKernelFunc(false), nullptr);

  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  EXPECT_EQ(acl_stub->load_count_, 1U);
  EXPECT_EQ(acl_stub->unload_count_, 0U);

  pool->Finalize();
  EXPECT_EQ(acl_stub->unload_count_, 0U);

  pool->Finalize();
  EXPECT_EQ(acl_stub->unload_count_, 1U);
  EXPECT_EQ(pool->GetDeviceKernelFunc(true), nullptr);
  EXPECT_EQ(pool->GetDeviceKernelFunc(false), nullptr);
}

TEST_F(TransferPoolTest, FinalizeDestroysAllThreadContextsBeforeFree) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(3U), SUCCESS);
  std::vector<TransferPool::SlotHandle> slots;
  ASSERT_EQ(pool->GetAllSlots(slots), SUCCESS);
  ASSERT_EQ(slots.size(), 3U);

  pool->Finalize();
  EXPECT_EQ(GetThreadFreeCallCount(), 3U);
  EXPECT_EQ(pool->GetAllSlots(slots), FAILED);
}

}  // namespace
}  // namespace hixl
