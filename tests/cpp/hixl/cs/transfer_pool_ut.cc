/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include <securec.h>

#include "ascendcl_stub.h"
#include "slog_stub.h"
#include "depends/hccl/src/hccl_stub.h"
#include "depends/hccl/src/hccl_host_exception_stub.h"
#include "depends/ascend_hal/src/ascend_hal_stub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "depends/sys_api/src/sys_api_wrap.h"
#include "depends/runtime/src/runtime_stub.h"
#include "engine/test_mmpa_utils.h"
#include "cs/hcomm_channel.h"
#include "cs/transfer_pool.cc"  // Keep access to process-local callback state inside the UT translation unit.
#include "cs/ubmem/ubmem_channel.h"
#include "gtest/gtest.h"
#include "hixl/hixl_types.h"

extern "C" uint32_t GetThreadAllocCallCount();
extern "C" uint32_t GetThreadFreeCallCount();
extern "C" void ResetThreadLifecycleStats();

namespace hixl {
namespace {

// Use distinctive device IDs so the process-wide pool is not shared with unrelated tests.
constexpr int32_t kTransferPoolUtDevId = 910246;
constexpr int32_t kTransferPoolKernelDevId = 910247;
constexpr int32_t kTransferPoolNotifyDevId = 910248;

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
    if ((count == sizeof(HixlTransferContextSyncEntry)) && (kind == ACL_MEMCPY_HOST_TO_DEVICE)) {
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

class SocNameAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  explicit SocNameAclRuntimeStub(std::string soc_name) : soc_name_(std::move(soc_name)) {}

  const char *aclrtGetSocName() override {
    return soc_name_.c_str();
  }

 private:
  std::string soc_name_;
};

class SyncEntryCaptureAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  aclError aclrtMemcpy(void *dst, size_t dest_max, const void *src, size_t count, aclrtMemcpyKind kind) override {
    if ((count == sizeof(HixlTransferContextSyncEntry)) && (kind == ACL_MEMCPY_HOST_TO_DEVICE) && (src != nullptr)) {
      HixlTransferContextSyncEntry entry{};
      (void)memcpy_s(&entry, sizeof(entry), src, sizeof(entry));
      captured_entries_.push_back(entry);
    }
    return llm::AclRuntimeStub::aclrtMemcpy(dst, dest_max, src, count, kind);
  }

  std::vector<HixlTransferContextSyncEntry> captured_entries_{};
};

// 捕获下发给 HixlSyncTransferContext 的 param，用于校验 host_exception_enabled 的实际取值。
class SyncParamCaptureAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  const char *aclrtGetSocName() override {
    return soc_name_;
  }

  aclError aclrtKernelArgsAppend(aclrtArgsHandle args_handle, void *data, size_t size,
                                 aclrtParamHandle *param_handle) override {
    if ((size == sizeof(HixlTransferContextSyncParam)) && (data != nullptr)) {
      HixlTransferContextSyncParam param{};
      (void)memcpy_s(&param, sizeof(param), data, sizeof(param));
      // 与 HixlOneSideOpParam（同为 128 字节）区分：sync param 有 version 与 entry/state 地址。
      if ((param.version == kHixlSyncParamVersion) && (param.entry_list_addr != 0U) && (param.state_list_addr != 0U)) {
        captured_params_.push_back(param);
      }
    }
    return llm::AclRuntimeStub::aclrtKernelArgsAppend(args_handle, data, size, param_handle);
  }

  std::vector<HixlTransferContextSyncParam> captured_params_{};
  const char *soc_name_{"Ascend950A"};
};

constexpr int32_t kTransferPoolA2DevId = 910250;
constexpr int32_t kTransferPoolA5DevId = 910251;
constexpr int32_t kTransferPoolSyncEntryDevId = 910252;
constexpr int32_t kTransferPoolHostRegFailDevId = 910253;
constexpr int32_t kTransferPoolInitFailDevId = 910254;
constexpr int32_t kTransferPoolSkipHcommDevId = 910255;
constexpr int32_t kTransferPoolHostExcDevId = 910256;
constexpr int32_t kTransferPoolHostExcBDevId = 910257;
constexpr int32_t kTransferPoolFlagLifecycleDevId = 910259;
constexpr int32_t kTransferPoolSyncParamDevId = 910261;

class HostExcAbortCountingAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  aclError aclrtStreamAbort(aclrtStream stream) override {
    ++abort_count_;
    return llm::AclRuntimeStub::aclrtStreamAbort(stream);
  }

  std::atomic<uint32_t> abort_count_{0U};
};

class HostExcDeviceMappingAclRuntimeStub : public HostExcAbortCountingAclRuntimeStub {
 public:
  aclError aclrtGetUserDevIdByLogicDevId(const int32_t logic_device_id, int32_t *const user_device_id) override {
    if (logic_device_id != logic_device_id_) {
      return llm::AclRuntimeStub::aclrtGetUserDevIdByLogicDevId(logic_device_id, user_device_id);
    }
    *user_device_id = user_device_id_;
    return ACL_SUCCESS;
  }

  int32_t logic_device_id_{kTransferPoolHostExcBDevId};
  int32_t user_device_id_{kTransferPoolHostExcDevId};
};

// 调用当前注册的 host 异常回调；返回回调是否存在。
bool InvokeHostExceptionCallback(int32_t device_id, ThreadHandle thread, uint32_t ret_code = 1U) {
  HcommExceptionCallback cb = GetRegisteredHostExceptionCallback();
  if (cb == nullptr) {
    return false;
  }
  HcommExceptionInfoLite info{};
  info.header.magicWord = HCOMM_EXCEPTION_LITE_MAGIC_WORD;
  info.header.version = HCOMM_EXCEPTION_LITE_VERSION;
  info.deviceId = static_cast<uint32_t>(device_id);
  info.thread = thread;
  info.retCode = ret_code;
  cb(&info, GetRegisteredHostExceptionUserData());
  return true;
}

class InitNotifyIdFailureAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  aclError aclrtGetNotifyId(aclrtNotify notify, uint32_t *notify_id) override {
    (void)notify;
    (void)notify_id;
    return ACL_ERROR_FAILURE;
  }

  aclError aclrtDestroyNotify(aclrtNotify notify) override {
    ++destroy_notify_count_;
    return llm::AclRuntimeStub::aclrtDestroyNotify(notify);
  }

  uint32_t destroy_notify_count_{0U};
};

class TransferPoolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto kernel_stub = std::make_shared<test::KernelJsonMmpaStub>();
    hixl_test::InstallSysApiHooks(kernel_stub);
    ResetThreadLifecycleStats();
    SaveHostExceptionCallbackState();
    ResetHostExceptionCallbackStub();
    ResetExceptionCallbackStub();
    ResetHcommVersionStub();
  }

  void TearDown() override {
    llm::HixlTestSetNotifyBatchResetAbortCount(0U);
    FinalizePools();
    RestoreHostExceptionCallbackState();
    ResetExceptionCallbackStub();
    ResetHcommVersionStub();
    AscendHalStubReset();
    llm::AclRuntimeStub::Reset();
    llm::RuntimeStub::Reset();
    hixl_test::ResetSysApiHooks();
  }

 private:
  void SaveHostExceptionCallbackState() {
    saved_host_callback_ = GetRegisteredHostExceptionCallback();
    saved_host_user_data_ = GetRegisteredHostExceptionUserData();
    std::lock_guard<std::mutex> lock(g_host_cb_mu);
    saved_host_registered_ = g_host_cb_registered;
    saved_host_ref_cnt_ = g_host_cb_ref_cnt;
    g_host_cb_registered = false;
    g_host_cb_ref_cnt = 0U;
  }

  void RestoreHostExceptionCallbackState() {
    ResetHostExceptionCallbackStub();
    if (saved_host_callback_ != nullptr) {
      EXPECT_EQ(HcommExceptionRegisterCallback(saved_host_callback_, saved_host_user_data_), 0);
    }
    std::lock_guard<std::mutex> lock(g_host_cb_mu);
    g_host_cb_registered = saved_host_registered_;
    g_host_cb_ref_cnt = saved_host_ref_cnt_;
  }

  static void FinalizePools() {
    SetUnregisterHostExceptionResult(0);
    for (const int32_t device_id :
         {kTransferPoolUtDevId, kTransferPoolKernelDevId, kTransferPoolNotifyDevId, kTransferPoolA2DevId,
          kTransferPoolA5DevId, kTransferPoolSyncEntryDevId, kTransferPoolHostRegFailDevId, kTransferPoolInitFailDevId,
          kTransferPoolSkipHcommDevId, kTransferPoolHostExcDevId, kTransferPoolHostExcBDevId,
          kTransferPoolFlagLifecycleDevId, kTransferPoolSyncParamDevId}) {
      auto *pool = TransferPool::GetInstance(device_id);
      if (pool != nullptr) {
        pool->Finalize();
        pool->DisableHostExceptionCallback();
        EXPECT_FALSE(pool->host_cb_ref_held_);
      }
    }
    EXPECT_EQ(g_host_cb_ref_cnt, 0U);
  }

  HcommExceptionCallback saved_host_callback_{nullptr};
  void *saved_host_user_data_{nullptr};
  bool saved_host_registered_{false};
  uint32_t saved_host_ref_cnt_{0U};
};

TEST_F(TransferPoolTest, InitializeRejectsZeroAndOverMaxPoolSize) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  EXPECT_EQ(pool->Initialize(0U), PARAM_INVALID);
  EXPECT_EQ(pool->Initialize(TransferPool::kMaxPoolSize + 1U), PARAM_INVALID);
}

TEST_F(TransferPoolTest, InitializeFailureCleansPartiallyInitializedSlot) {
  auto acl_stub = std::make_shared<InitNotifyIdFailureAclRuntimeStub>();
  llm::AclRuntimeStub::SetInstance(acl_stub);
  auto *pool = TransferPool::GetInstance(kTransferPoolInitFailDevId);
  ASSERT_NE(pool, nullptr);

  EXPECT_NE(pool->Initialize(1U), SUCCESS);
  EXPECT_EQ(GetThreadAllocCallCount(), 1U);
  EXPECT_EQ(GetThreadFreeCallCount(), 1U);
  EXPECT_EQ(acl_stub->destroy_notify_count_, 1U);
}

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

TEST_F(TransferPoolTest, DeviceAbortPostRebuildsThreads) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(4U), SUCCESS);
  std::vector<TransferPool::SlotHandle> before;
  ASSERT_EQ(pool->GetAllSlots(before), SUCCESS);
  for (const auto &h : before) {
    EXPECT_NE(h.thread, 0U);
  }

  // 模拟 stop_device 的 POST 回调：标记设备已 abort，线程需重建
  llm::HixlTestTriggerDeviceAbortPost(kTransferPoolUtDevId);

  // 下一次 Acquire 检测到标志 → 全量 ThreadFree+ThreadAlloc 重建所有空闲 slot
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool->Acquire(&h), SUCCESS);
  EXPECT_NE(h.thread, 0U);

  // 重建后所有 slot 的 thread 句柄都是新的（不等于重建前的旧句柄）
  std::vector<TransferPool::SlotHandle> after;
  ASSERT_EQ(pool->GetAllSlots(after), SUCCESS);
  for (size_t i = 0; i < after.size(); ++i) {
    EXPECT_NE(after[i].thread, before[i].thread);
  }
  pool->Release(h);
}

TEST_F(TransferPoolTest, AbortDestroysUbMemStreamBeforeRetryingNotifyReset) {
  class UbMemStreamDestroyStub : public llm::AclRuntimeStub {
   public:
    aclError aclrtDestroyStream(aclrtStream stream) override {
      if (worker_stream != nullptr && stream == worker_stream) {
        worker_stream = nullptr;
        EXPECT_EQ(llm::HixlTestGetNotifyBatchResetCallCount(), 0U);
      }
      return llm::AclRuntimeStub::aclrtDestroyStream(stream);
    }

    aclrtStream worker_stream{nullptr};
  };
  auto acl_stub = std::make_shared<UbMemStreamDestroyStub>();
  llm::AclRuntimeStub::SetInstance(acl_stub);
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool->Acquire(&h), SUCCESS);
  ASSERT_EQ(pool->EnsureUbMemStream(h), SUCCESS);
  ASSERT_NE(h.ubmem_stream, nullptr);
  acl_stub->worker_stream = h.ubmem_stream;
  // 模拟设备复位窗口：前两次 aclrtNotifyBatchReset 返 107022，重试后成功
  llm::HixlTestSetNotifyBatchResetAbortCount(2U);
  pool->Abort(h);
  EXPECT_EQ(acl_stub->worker_stream, nullptr);
  // 断言重试确实发生：reset 被调用 3 次（2 次失败 + 1 次成功）
  EXPECT_EQ(llm::HixlTestGetNotifyBatchResetCallCount(), 3U);
  // 重试成功后 slot 回到 free list，可再次 Acquire
  TransferPool::SlotHandle again{};
  ASSERT_EQ(pool->Acquire(&again), SUCCESS);
  pool->Release(again);
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

TEST_F(TransferPoolTest, ResolveNotifyAddrAcceptsSuccessfulLookupWithZeroOutput) {
  class EmptyNotifyAddressStub : public llm::RuntimeStub {
   public:
    rtError_t rtGetDevResAddress(rtDevResInfo *, rtDevResAddrInfo *) override {
      return RT_ERROR_NONE;
    }
  };
  llm::AclRuntimeStub::SetInstance(std::make_shared<SocNameAclRuntimeStub>("Ascend950A"));
  auto *pool = TransferPool::GetInstance(kTransferPoolNotifyDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(2U), SUCCESS);
  std::vector<TransferPool::SlotHandle> slots;
  ASSERT_EQ(pool->GetAllSlots(slots), SUCCESS);
  ASSERT_EQ(slots.size(), 2U);
  for (const auto &slot : slots) {
    EXPECT_EQ(slot.notify_addr, 0U);
    EXPECT_EQ(slot.notify_len, 0U);
  }
  llm::RuntimeStub::SetInstance(std::make_shared<EmptyNotifyAddressStub>());
  EXPECT_EQ(pool->ResolveNotifyAddr(), SUCCESS);
}

TEST_F(TransferPoolTest, FlagBlockAllocatedWithContiguousSlotOffsets) {
  auto *pool = TransferPool::GetInstance(kTransferPoolNotifyDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(4U), SUCCESS);
  std::vector<TransferPool::SlotHandle> slots;
  ASSERT_EQ(pool->GetAllSlots(slots), SUCCESS);
  ASSERT_EQ(slots.size(), 4U);
  const uint64_t base = slots[0].flag_dev_addr;
  ASSERT_NE(base, 0U);
  for (size_t i = 0U; i < slots.size(); ++i) {
    EXPECT_EQ(slots[i].flag_dev_addr, base + i * sizeof(uint64_t));
  }
  pool->Finalize();
}

TEST_F(TransferPoolTest, AbortKeepsFlagBlockAndClearsSlotFlag) {
  auto *pool = TransferPool::GetInstance(kTransferPoolFlagLifecycleDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(2U), SUCCESS);
  std::vector<TransferPool::SlotHandle> before;
  ASSERT_EQ(pool->GetAllSlots(before), SUCCESS);
  ASSERT_EQ(before.size(), 2U);
  const uint64_t flag_base = before[0].flag_dev_addr;
  ASSERT_NE(flag_base, 0U);

  TransferPool::SlotHandle handle{};
  ASSERT_EQ(pool->Acquire(&handle), SUCCESS);
  ASSERT_NE(handle.flag_dev_addr, 0U);
  // 模拟 abort 跳过 ValueWrite(0) 后残留的 flag=1，归还 slot 前必须清零。
  *reinterpret_cast<volatile uint64_t *>(static_cast<uintptr_t>(handle.flag_dev_addr)) = 1U;
  const uint64_t aborted_flag_addr = handle.flag_dev_addr;
  pool->Abort(handle);

  // abort/reinit 只能复位 slot，不得销毁重建 flag block（整块内存与地址保持不变）。
  std::vector<TransferPool::SlotHandle> after;
  ASSERT_EQ(pool->GetAllSlots(after), SUCCESS);
  ASSERT_EQ(after.size(), 2U);
  EXPECT_EQ(after[0].flag_dev_addr, flag_base);
  EXPECT_EQ(after[1].flag_dev_addr, flag_base + sizeof(uint64_t));
  EXPECT_EQ(*reinterpret_cast<volatile uint64_t *>(static_cast<uintptr_t>(aborted_flag_addr)), 0U);

  TransferPool::SlotHandle next{};
  ASSERT_EQ(pool->Acquire(&next), SUCCESS);
  EXPECT_EQ(*reinterpret_cast<volatile uint64_t *>(static_cast<uintptr_t>(next.flag_dev_addr)), 0U);
  pool->Release(next);
  pool->Finalize();
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

TEST_F(TransferPoolTest, ReleaseWritesBackLaunchedTasksAndAcquireSeedsIt) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool->Acquire(&h), SUCCESS);
  h.launched_tasks = 100U;
  pool->Release(h);

  TransferPool::SlotHandle again{};
  ASSERT_EQ(pool->Acquire(&again), SUCCESS);
  EXPECT_EQ(again.slot_index, h.slot_index);
  EXPECT_EQ(again.launched_tasks, 100U);
  pool->Release(again);
}

TEST_F(TransferPoolTest, LaunchedTasksAccumulateAcrossReuse) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool->Acquire(&h), SUCCESS);
  h.launched_tasks = 100U;
  pool->Release(h);

  ASSERT_EQ(pool->Acquire(&h), SUCCESS);
  EXPECT_EQ(h.launched_tasks, 100U);
  h.launched_tasks += 50U;
  pool->Release(h);

  ASSERT_EQ(pool->Acquire(&h), SUCCESS);
  EXPECT_EQ(h.launched_tasks, 150U);
  pool->Release(h);
}

TEST_F(TransferPoolTest, AbortReinitResetsLaunchedTasks) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool->Acquire(&h), SUCCESS);
  h.launched_tasks = 100U;
  pool->Abort(h);

  TransferPool::SlotHandle again{};
  ASSERT_EQ(pool->Acquire(&again), SUCCESS);
  EXPECT_EQ(again.slot_index, h.slot_index);
  EXPECT_EQ(again.launched_tasks, 0U);
  pool->Release(again);
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
  EXPECT_EQ(acl_stub->func_names_.size(), 5U);
  EXPECT_EQ(acl_stub->func_names_[3], "HixlUbMemBatchRead");
  EXPECT_EQ(acl_stub->func_names_[4], "HixlUbMemBatchWrite");
  EXPECT_EQ(acl_stub->sync_entry_h2d_count_, 1U);
  EXPECT_EQ(acl_stub->sync_state_d2h_count_, 1U);
  EXPECT_NE(pool->GetDeviceKernelFunc(true), nullptr);
  EXPECT_NE(pool->GetDeviceKernelFunc(false), nullptr);
  EXPECT_NE(pool->GetDeviceKernelFunc(true, COMM_PROTOCOL_UB_MEM), nullptr);
  EXPECT_NE(pool->GetDeviceKernelFunc(false, COMM_PROTOCOL_UB_MEM), nullptr);
  EXPECT_NE(pool->GetDeviceKernelFunc(true, COMM_PROTOCOL_UB_MEM), pool->GetDeviceKernelFunc(true));

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

TEST_F(TransferPoolTest, ErrFlagAllocatedDuringInitialize) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(3U), SUCCESS);
  std::vector<TransferPool::SlotHandle> slots;
  ASSERT_EQ(pool->GetAllSlots(slots), SUCCESS);
  ASSERT_EQ(slots.size(), 3U);
  for (const auto &slot : slots) {
    EXPECT_NE(slot.err_flag_host_addr, nullptr);
    EXPECT_NE(slot.err_flag_dev_addr, 0U);
  }
  pool->Finalize();
}

TEST_F(TransferPoolTest, ErrFlagSlotAddressesAreContiguous) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(3U), SUCCESS);
  std::vector<TransferPool::SlotHandle> slots;
  ASSERT_EQ(pool->GetAllSlots(slots), SUCCESS);
  ASSERT_EQ(slots.size(), 3U);
  uint8_t *host_base0 = slots[0].err_flag_host_addr;
  uint8_t *access_base0 = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(slots[0].err_flag_dev_addr));
  for (uint32_t i = 1U; i < 3U; ++i) {
    EXPECT_EQ(slots[i].err_flag_host_addr, host_base0 + i);
    EXPECT_EQ(reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(slots[i].err_flag_dev_addr)), access_base0 + i);
  }
  pool->Finalize();
}

TEST_F(TransferPoolTest, ErrFlagResetOnRelease) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(2U), SUCCESS);
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool->Acquire(&h), SUCCESS);
  ASSERT_NE(h.err_flag_host_addr, nullptr);
  *h.err_flag_host_addr = 1U;
  pool->Release(h);

  TransferPool::SlotHandle h2{};
  ASSERT_EQ(pool->Acquire(&h2), SUCCESS);
  EXPECT_EQ(*h2.err_flag_host_addr, 0U);
  pool->Release(h2);
  pool->Finalize();
}

TEST_F(TransferPoolTest, ReleaseResetsErrorAndCompletionFlags) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  TransferPool::SlotHandle handle{};
  ASSERT_EQ(pool->Acquire(&handle), SUCCESS);
  ASSERT_NE(handle.err_flag_host_addr, nullptr);
  ASSERT_NE(handle.flag_dev_addr, 0U);
  auto *completion_flag = reinterpret_cast<uint64_t *>(static_cast<uintptr_t>(handle.flag_dev_addr));
  *handle.err_flag_host_addr = 1U;
  *completion_flag = 1U;
  pool->Release(handle);
  EXPECT_EQ(*handle.err_flag_host_addr, 0U);
  EXPECT_EQ(*completion_flag, 0U);
  TransferPool::SlotHandle next{};
  ASSERT_EQ(pool->Acquire(&next), SUCCESS);
  EXPECT_EQ(next.slot_index, handle.slot_index);
  EXPECT_EQ(*completion_flag, 0U);
  pool->Release(next);
  pool->Finalize();
}

TEST_F(TransferPoolTest, ErrFlagClearedAcrossAbort) {
  auto *pool = TransferPool::GetInstance(kTransferPoolUtDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool->Acquire(&h), SUCCESS);
  ASSERT_NE(h.err_flag_host_addr, nullptr);
  ASSERT_NE(h.err_flag_dev_addr, 0U);
  *h.err_flag_host_addr = 1U;
  pool->Abort(h);

  TransferPool::SlotHandle h2{};
  ASSERT_EQ(pool->Acquire(&h2), SUCCESS);
  EXPECT_NE(h2.err_flag_host_addr, nullptr);
  EXPECT_NE(h2.err_flag_dev_addr, 0U);
  EXPECT_EQ(*h2.err_flag_host_addr, 0U);
  pool->Release(h2);
  pool->Finalize();
}

TEST_F(TransferPoolTest, ErrFlagOnA5UsesHostMappedPath) {
  llm::AclRuntimeStub::SetInstance(std::make_shared<SocNameAclRuntimeStub>("Ascend950A"));
  auto *pool = TransferPool::GetInstance(kTransferPoolA5DevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(2U), SUCCESS);
  std::vector<TransferPool::SlotHandle> slots;
  ASSERT_EQ(pool->GetAllSlots(slots), SUCCESS);
  ASSERT_EQ(slots.size(), 2U);
  for (const auto &slot : slots) {
    EXPECT_NE(slot.err_flag_host_addr, nullptr);
    EXPECT_NE(slot.err_flag_dev_addr, 0U);
  }
  pool->Finalize();
}

TEST_F(TransferPoolTest, ErrFlagOnA2UsesDeviceMappedPath) {
  llm::AclRuntimeStub::SetInstance(std::make_shared<SocNameAclRuntimeStub>("Ascend910B1"));
  auto *pool = TransferPool::GetInstance(kTransferPoolA2DevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(2U), SUCCESS);
  std::vector<TransferPool::SlotHandle> slots;
  ASSERT_EQ(pool->GetAllSlots(slots), SUCCESS);
  ASSERT_EQ(slots.size(), 2U);
  for (const auto &slot : slots) {
    EXPECT_NE(slot.err_flag_host_addr, nullptr);
    EXPECT_NE(slot.err_flag_dev_addr, 0U);
  }
  // Host VA comes from DEV_SVM_MAP_HOST mapping; writable from host side.
  *slots[0].err_flag_host_addr = 1U;
  EXPECT_EQ(*slots[0].err_flag_host_addr, 1U);
  pool->Finalize();
}

TEST_F(TransferPoolTest, SyncEntryAddContainsErrFlagAndNotifyId) {
  auto acl_stub = std::make_shared<SyncEntryCaptureAclRuntimeStub>();
  llm::AclRuntimeStub::SetInstance(acl_stub);
  auto *pool = TransferPool::GetInstance(kTransferPoolSyncEntryDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  ASSERT_FALSE(acl_stub->captured_entries_.empty());

  const HixlTransferContextSyncEntry &entry = acl_stub->captured_entries_.front();
  EXPECT_EQ(entry.op, TRANSFER_CONTEXT_OP_ADD);
  EXPECT_NE(entry.err_flag_dev_va, 0U);
  EXPECT_NE(entry.notify_id, 0U);
  pool->Finalize();
}

TEST_F(TransferPoolTest, Hcomm930UsesHostExceptionRegistration) {
  SetHcommVersionNum(90300000);
  auto acl_stub = std::make_shared<SyncParamCaptureAclRuntimeStub>();
  llm::AclRuntimeStub::SetInstance(acl_stub);
  auto *pool = TransferPool::GetInstance(kTransferPoolSyncParamDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  ASSERT_FALSE(acl_stub->captured_params_.empty());
  EXPECT_EQ(acl_stub->captured_params_.front().host_exception_enabled, 1U);
  EXPECT_TRUE(pool->host_exception_enabled_);
  pool->Finalize();
}

class TransferPoolLegacyHcommTest : public TransferPoolTest, public testing::WithParamInterface<int32_t> {};

TEST_P(TransferPoolLegacyHcommTest, UsesDeviceExceptionRegistration) {
  SetHcommVersionNum(GetParam());
  auto acl_stub = std::make_shared<SyncParamCaptureAclRuntimeStub>();
  llm::AclRuntimeStub::SetInstance(acl_stub);
  auto *pool = TransferPool::GetInstance(kTransferPoolSyncParamDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  ASSERT_FALSE(acl_stub->captured_params_.empty());
  EXPECT_EQ(acl_stub->captured_params_.front().host_exception_enabled, 0U);
  EXPECT_EQ(GetRegisteredHostExceptionCallback(), nullptr);
  EXPECT_NE(GetRegisteredExceptionCallback(), nullptr);
  EXPECT_FALSE(pool->host_exception_enabled_);
  pool->Finalize();
}

INSTANTIATE_TEST_SUITE_P(HcommBelow930, TransferPoolLegacyHcommTest, testing::Values(90200000, 90299999));

class TransferPoolLegacySocTest : public TransferPoolTest, public testing::WithParamInterface<const char *> {};

TEST_P(TransferPoolLegacySocTest, Hcomm930UsesDeviceExceptionRegistration) {
  SetHcommVersionNum(90300000);
  auto acl_stub = std::make_shared<SyncParamCaptureAclRuntimeStub>();
  acl_stub->soc_name_ = GetParam();
  llm::AclRuntimeStub::SetInstance(acl_stub);
  auto *pool = TransferPool::GetInstance(kTransferPoolSyncParamDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  ASSERT_FALSE(acl_stub->captured_params_.empty());
  EXPECT_EQ(acl_stub->captured_params_.front().host_exception_enabled, 0U);
  EXPECT_EQ(GetRegisteredHostExceptionCallback(), nullptr);
  EXPECT_NE(GetRegisteredExceptionCallback(), nullptr);
  EXPECT_FALSE(pool->host_exception_enabled_);
  std::vector<TransferPool::SlotHandle> slots;
  ASSERT_EQ(pool->GetAllSlots(slots), SUCCESS);
  ASSERT_EQ(slots.size(), 1U);
  EXPECT_EQ(slots.front().flag_dev_addr, 0U);
  pool->Finalize();
}

INSTANTIATE_TEST_SUITE_P(A2AndA3, TransferPoolLegacySocTest, testing::Values("Ascend910B1", "Ascend910_9391"));

TEST_F(TransferPoolTest, SyncParamDisablesHostExceptionWhenUnsupported) {
  SetRegisterHostExceptionResult(HCCL_E_NOT_SUPPORT);
  auto acl_stub = std::make_shared<SyncParamCaptureAclRuntimeStub>();
  llm::AclRuntimeStub::SetInstance(acl_stub);
  auto *pool = TransferPool::GetInstance(kTransferPoolSyncParamDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  ASSERT_FALSE(acl_stub->captured_params_.empty());
  EXPECT_EQ(acl_stub->captured_params_.front().host_exception_enabled, 0U);
  pool->Finalize();
}

TEST_F(TransferPoolTest, ErrFlagHostRegisterFailStillInitializes) {
  llm::AclRuntimeStub::SetInstance(std::make_shared<SocNameAclRuntimeStub>("Ascend950A"));
  AscendHalStubSetHostRegisterRet(-1);
  auto *pool = TransferPool::GetInstance(kTransferPoolHostRegFailDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(2U), SUCCESS);

  std::vector<TransferPool::SlotHandle> slots;
  ASSERT_EQ(pool->GetAllSlots(slots), SUCCESS);
  ASSERT_EQ(slots.size(), 2U);
  for (const auto &slot : slots) {
    EXPECT_EQ(slot.err_flag_host_addr, nullptr);
    EXPECT_EQ(slot.err_flag_dev_addr, 0U);
  }

  TransferPool::SlotHandle handle{};
  ASSERT_EQ(pool->Acquire(&handle), SUCCESS);
  pool->Release(handle);
  AscendHalStubReset();
  pool->Finalize();
}

TEST_F(TransferPoolTest, InitializeAlwaysAllocatesHcommThreads) {
  auto *pool = TransferPool::GetInstance(kTransferPoolSkipHcommDevId);
  ASSERT_NE(pool, nullptr);
  ResetThreadLifecycleStats();
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  EXPECT_EQ(GetThreadAllocCallCount(), 1U);
  TransferPool::SlotHandle handle{};
  ASSERT_EQ(pool->Acquire(&handle), SUCCESS);
  EXPECT_NE(handle.thread, static_cast<ThreadHandle>(0));
  ASSERT_EQ(pool->EnsureUbMemStream(handle), SUCCESS);
  EXPECT_NE(handle.ubmem_stream, nullptr);
  pool->Release(handle);
  pool->Finalize();
  EXPECT_EQ(GetThreadFreeCallCount(), 1U);
}

// ==================== host exception callback tests ====================

TEST_F(TransferPoolTest, HostExceptionCallbackSurvivesPeerPoolFinalize) {
  auto acl_stub = std::make_shared<HostExcAbortCountingAclRuntimeStub>();
  llm::AclRuntimeStub::SetInstance(acl_stub);

  auto *pool_a = TransferPool::GetInstance(kTransferPoolHostExcDevId);
  auto *pool_b = TransferPool::GetInstance(kTransferPoolHostExcBDevId);
  ASSERT_NE(pool_a, nullptr);
  ASSERT_NE(pool_b, nullptr);
  ASSERT_EQ(pool_a->Initialize(1U), SUCCESS);
  ASSERT_EQ(pool_b->Initialize(1U), SUCCESS);
  const HcommExceptionCallback registered = GetRegisteredHostExceptionCallback();
  ASSERT_NE(registered, nullptr);

  TransferPool::SlotHandle hb{};
  ASSERT_EQ(pool_b->Acquire(&hb), SUCCESS);
  ASSERT_NE(hb.err_flag_host_addr, nullptr);

  // pool_a 收尾后 pool_b 仍存活：进程级回调不得被注销
  pool_a->Finalize();
  EXPECT_EQ(GetRegisteredHostExceptionCallback(), registered);

  // pool_b 仍能收到异常并 abort
  acl_stub->abort_count_ = 0U;
  ASSERT_TRUE(InvokeHostExceptionCallback(kTransferPoolHostExcBDevId, hb.thread));
  EXPECT_EQ(*hb.err_flag_host_addr, 1U);
  EXPECT_EQ(acl_stub->abort_count_.load(), 1U);
  pool_b->Release(hb);

  // Finalize 不注销回调（注销只在 ~TransferPool），pool 对象仍存活
  pool_b->Finalize();
  EXPECT_EQ(GetRegisteredHostExceptionCallback(), registered);
}

TEST_F(TransferPoolTest, HostExceptionRoutesRemappedLogicDeviceToUserDevicePool) {
  auto acl_stub = std::make_shared<HostExcDeviceMappingAclRuntimeStub>();
  llm::AclRuntimeStub::SetInstance(acl_stub);
  auto *pool = TransferPool::GetInstance(acl_stub->user_device_id_);
  auto *other_pool = TransferPool::GetInstance(acl_stub->logic_device_id_);
  ASSERT_NE(pool, nullptr);
  ASSERT_NE(other_pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  ASSERT_EQ(other_pool->Initialize(1U), SUCCESS);
  TransferPool::SlotHandle handle{};
  TransferPool::SlotHandle other{};
  ASSERT_EQ(pool->Acquire(&handle), SUCCESS);
  ASSERT_EQ(other_pool->Acquire(&other), SUCCESS);
  ASSERT_NE(handle.err_flag_host_addr, nullptr);
  ASSERT_NE(other.err_flag_host_addr, nullptr);

  ASSERT_TRUE(InvokeHostExceptionCallback(acl_stub->logic_device_id_, handle.thread));

  EXPECT_EQ(*handle.err_flag_host_addr, 1U);
  EXPECT_EQ(*other.err_flag_host_addr, 0U);
  EXPECT_EQ(acl_stub->abort_count_.load(), 1U);
  pool->Release(handle);
  other_pool->Release(other);
  pool->Finalize();
  other_pool->Finalize();
}

TEST_F(TransferPoolTest, HostExceptionIgnoresUnmatchedThreadAndInvalidArgs) {
  auto acl_stub = std::make_shared<HostExcAbortCountingAclRuntimeStub>();
  llm::AclRuntimeStub::SetInstance(acl_stub);
  auto *pool = TransferPool::GetInstance(kTransferPoolHostExcDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool->Acquire(&h), SUCCESS);
  ASSERT_NE(h.err_flag_host_addr, nullptr);
  acl_stub->abort_count_ = 0U;

  HcommExceptionCallback cb = GetRegisteredHostExceptionCallback();
  ASSERT_NE(cb, nullptr);
  // info 为 null
  cb(nullptr, GetRegisteredHostExceptionUserData());
  HcommExceptionInfoLite info{};
  info.header.magicWord = HCOMM_EXCEPTION_LITE_MAGIC_WORD;
  info.header.version = HCOMM_EXCEPTION_LITE_VERSION;
  info.deviceId = static_cast<uint32_t>(kTransferPoolHostExcDevId);
  info.thread = h.thread;
  // userData magic 错误
  uint32_t bogus_magic = 0U;
  cb(&info, &bogus_magic);
  // Invalid ABI headers must not mark or abort the matching active slot.
  info.header.magicWord = 0U;
  cb(&info, GetRegisteredHostExceptionUserData());
  EXPECT_EQ(*h.err_flag_host_addr, 0U);
  EXPECT_EQ(acl_stub->abort_count_.load(), 0U);
  info.header.magicWord = HCOMM_EXCEPTION_LITE_MAGIC_WORD;
  info.header.version = HCOMM_EXCEPTION_LITE_VERSION + 1U;
  cb(&info, GetRegisteredHostExceptionUserData());
  EXPECT_EQ(*h.err_flag_host_addr, 0U);
  EXPECT_EQ(acl_stub->abort_count_.load(), 0U);
  info.header.version = HCOMM_EXCEPTION_LITE_VERSION;
  // thread 不匹配
  info.thread = h.thread + 12345U;
  cb(&info, GetRegisteredHostExceptionUserData());

  EXPECT_EQ(*h.err_flag_host_addr, 0U);
  EXPECT_EQ(acl_stub->abort_count_.load(), 0U);

  pool->Release(h);
  pool->Finalize();
}

TEST_F(TransferPoolTest, HostExceptionAbortIsSingleShotPerSlotUse) {
  auto acl_stub = std::make_shared<HostExcAbortCountingAclRuntimeStub>();
  llm::AclRuntimeStub::SetInstance(acl_stub);
  auto *pool = TransferPool::GetInstance(kTransferPoolHostExcDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  TransferPool::SlotHandle h{};
  ASSERT_EQ(pool->Acquire(&h), SUCCESS);

  acl_stub->abort_count_ = 0U;
  ASSERT_TRUE(InvokeHostExceptionCallback(kTransferPoolHostExcDevId, h.thread));
  EXPECT_EQ(acl_stub->abort_count_.load(), 1U);
  ASSERT_TRUE(InvokeHostExceptionCallback(kTransferPoolHostExcDevId, h.thread));
  EXPECT_EQ(acl_stub->abort_count_.load(), 1U);

  // 用户路径 Abort 必须跳过第二次 stream abort
  pool->Abort(h);
  EXPECT_EQ(acl_stub->abort_count_.load(), 1U);

  // slot 已重建并可再次 Acquire；再次异常重新触发一次 abort
  TransferPool::SlotHandle h2{};
  ASSERT_EQ(pool->Acquire(&h2), SUCCESS);
  ASSERT_TRUE(InvokeHostExceptionCallback(kTransferPoolHostExcDevId, h2.thread));
  EXPECT_EQ(acl_stub->abort_count_.load(), 2U);

  pool->Release(h2);
  pool->Finalize();
}

TEST_F(TransferPoolTest, HostExceptionAfterFinalizeIsIgnored) {
  auto acl_stub = std::make_shared<HostExcAbortCountingAclRuntimeStub>();
  llm::AclRuntimeStub::SetInstance(acl_stub);
  auto *pool = TransferPool::GetInstance(kTransferPoolHostExcDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_EQ(pool->Initialize(1U), SUCCESS);
  const HcommExceptionCallback registered = GetRegisteredHostExceptionCallback();
  ASSERT_NE(registered, nullptr);

  pool->Finalize();
  // Finalize 不注销回调（注销只在 ~TransferPool），pool 对象仍存活，回调靠 inited_ 自守
  EXPECT_EQ(GetRegisteredHostExceptionCallback(), registered);

  acl_stub->abort_count_ = 0U;
  ASSERT_TRUE(InvokeHostExceptionCallback(kTransferPoolHostExcDevId, 999999ULL));
  EXPECT_EQ(acl_stub->abort_count_.load(), 0U);
}

// hcomm 会在 Register/Unregister 持内部锁期间回调注册方。若 hixl 在 mu_ 下注册，回调内再次取 mu_ 会自死锁。
// 这里让 stub 在 Register 的调用栈内同步触发一次回调，用 wait_for 把死锁转成失败而不是永久挂起。
TEST_F(TransferPoolTest, HostExceptionCallbackRegistrationIsReentrancySafe) {
  auto *pool = TransferPool::GetInstance(kTransferPoolHostExcDevId);
  ASSERT_NE(pool, nullptr);
  SetRegisterHostExceptionReentrantInfo(static_cast<uint32_t>(kTransferPoolHostExcDevId), 0U);

  auto init_future = std::async(std::launch::async, [pool]() { return pool->Initialize(1U); });
  ASSERT_EQ(init_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  EXPECT_EQ(init_future.get(), SUCCESS);
  EXPECT_NE(GetRegisteredHostExceptionCallback(), nullptr);

  ClearRegisterHostExceptionReentrantInfo();
  pool->Finalize();
}

}  // namespace
}  // namespace hixl
