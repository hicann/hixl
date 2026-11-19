/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. See LICENSE in the root of
 * the software repository for the full text of the License.
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "cs/ubmem/ubmem_virtual_memory_manager.h"
#include "depends/ascendcl/src/ascendcl_stub.h"
#include "acl/acl.h"
#include "common/def_types.h"

namespace hixl {
namespace {
constexpr size_t kTestSize1GB = 1024UL * 1024UL * 1024UL;
constexpr size_t kTestSize2GB = 2UL * kTestSize1GB;
constexpr size_t kTestSize500MB = 500UL * 1024UL * 1024UL;
constexpr size_t kTestSize1TB = 1024UL * kTestSize1GB;
constexpr size_t kTestSize2TB = 2UL * kTestSize1TB;
constexpr uintptr_t kTest40TB = 40UL * kTestSize1TB;
constexpr uintptr_t kTest80TB = 80UL * kTestSize1TB;

struct ReserveCallRecord {
  void *expect_ptr = nullptr;
  size_t size = 0;
};

class MockVmAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  std::string soc_name_;
  void *last_expect_ptr_ = nullptr;

  const char *aclrtGetSocName() override {
    return soc_name_.c_str();
  }

  aclError aclrtReserveMemAddress(void **devPtr, size_t size, size_t alignment, void *devAddr,
                                  uint64_t flags) override {
    last_expect_ptr_ = devAddr;
    return llm::AclRuntimeStub::aclrtReserveMemAddress(devPtr, size, alignment, devAddr, flags);
  }
};

class FakeRangeReserveRuntimeStub : public llm::AclRuntimeStub {
 public:
  explicit FakeRangeReserveRuntimeStub(std::string soc_name) : soc_name_(std::move(soc_name)) {}

  struct BlockedRange {
    uintptr_t start = 0;
    uintptr_t end = 0;
  };

  std::string soc_name_;
  std::vector<BlockedRange> blocked_ranges_;
  std::vector<ReserveCallRecord> reserve_calls_;
  std::vector<std::pair<uintptr_t, size_t>> active_reserves_;
  std::vector<void *> released_ptrs_;
  size_t max_single_reserve_size_ = std::numeric_limits<size_t>::max();
  size_t succeed_reserve_limit_ = std::numeric_limits<size_t>::max();
  uintptr_t next_auto_va_ = 100UL * kTestSize1TB;
  uintptr_t override_returned_va_ = 0;
  size_t override_returned_va_call_index_ = std::numeric_limits<size_t>::max();
  size_t fail_next_release_count_ = 0;

  const char *aclrtGetSocName() override {
    return soc_name_.c_str();
  }

  bool OverlapsAny(uintptr_t start, size_t size) const {
    if (size == 0 || start > std::numeric_limits<uintptr_t>::max() - size) {
      return true;
    }
    const uintptr_t end = start + size;
    for (const auto &blocked : blocked_ranges_) {
      if (start < blocked.end && end > blocked.start) {
        return true;
      }
    }
    for (const auto &active : active_reserves_) {
      if (start < active.first + active.second && end > active.first) {
        return true;
      }
    }
    return false;
  }

  aclError aclrtReserveMemAddress(void **devPtr, size_t size, size_t alignment, void *devAddr,
                                  uint64_t flags) override {
    (void)alignment;
    (void)flags;
    const size_t call_index = reserve_calls_.size();
    reserve_calls_.push_back({devAddr, size});
    if (size > max_single_reserve_size_ || active_reserves_.size() >= succeed_reserve_limit_) {
      return ACL_ERROR_RT_INTERNAL_ERROR;
    }
    uintptr_t chosen_va = 0;
    if (call_index == override_returned_va_call_index_) {
      chosen_va = override_returned_va_;
    } else if (devAddr != nullptr) {
      chosen_va = reinterpret_cast<uintptr_t>(devAddr);
      if (OverlapsAny(chosen_va, size)) {
        return ACL_ERROR_RT_INTERNAL_ERROR;
      }
    } else {
      chosen_va = next_auto_va_;
      next_auto_va_ += size + kTestSize1TB;
    }
    active_reserves_.emplace_back(chosen_va, size);
    *devPtr = reinterpret_cast<void *>(chosen_va);
    return ACL_SUCCESS;
  }

  aclError aclrtReleaseMemAddress(void *devPtr) override {
    released_ptrs_.push_back(devPtr);
    if (fail_next_release_count_ > 0U) {
      --fail_next_release_count_;
      return ACL_ERROR_RT_INTERNAL_ERROR;
    }
    const uintptr_t target = reinterpret_cast<uintptr_t>(devPtr);
    for (auto it = active_reserves_.begin(); it != active_reserves_.end(); ++it) {
      if (it->first == target) {
        active_reserves_.erase(it);
        return ACL_SUCCESS;
      }
    }
    return ACL_ERROR_INVALID_PARAM;
  }
};

class ScopedRuntimeMockForVmManager {
 public:
  explicit ScopedRuntimeMockForVmManager(const std::shared_ptr<llm::AclRuntimeStub> &instance) {
    llm::AclRuntimeStub::SetInstance(instance);
  }
  ~ScopedRuntimeMockForVmManager() {
    llm::AclRuntimeStub::Reset();
  }
  ScopedRuntimeMockForVmManager(const ScopedRuntimeMockForVmManager &) = delete;
  ScopedRuntimeMockForVmManager &operator=(const ScopedRuntimeMockForVmManager &) = delete;
};
}  // namespace

class VirtualMemoryManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a mock runtime stub
    mock_runtime_ = std::make_shared<llm::AclRuntimeStub>();
    scoped_mock_ = std::make_unique<ScopedRuntimeMockForVmManager>(mock_runtime_);
  }

  void TearDown() override {
    VirtualMemoryManager::GetInstance().Finalize();
    scoped_mock_.reset();
    mock_runtime_.reset();
  }

  std::shared_ptr<llm::AclRuntimeStub> mock_runtime_;
  std::unique_ptr<ScopedRuntimeMockForVmManager> scoped_mock_;

  void InstallRuntimeMock(const std::shared_ptr<llm::AclRuntimeStub> &mock) {
    scoped_mock_.reset();
    scoped_mock_ = std::make_unique<ScopedRuntimeMockForVmManager>(mock);
  }
};

TEST_F(VirtualMemoryManagerTest, GetInstance_ReturnsSameInstance) {
  VirtualMemoryManager &instance1 = VirtualMemoryManager::GetInstance();
  VirtualMemoryManager &instance2 = VirtualMemoryManager::GetInstance();
  EXPECT_EQ(&instance1, &instance2);
}

TEST_F(VirtualMemoryManagerTest, Initialize_Success) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  EXPECT_EQ(manager.Initialize(), SUCCESS);
  // Second initialization should also succeed
  EXPECT_EQ(manager.Initialize(), SUCCESS);
}

TEST_F(VirtualMemoryManagerTest, ReserveMemAddress_UsesNoUCMemoryOnA3) {
  auto mock = std::make_shared<MockVmAclRuntimeStub>();
  mock->soc_name_ = "Ascend910_9391";
  InstallRuntimeMock(mock);

  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  void *addr = nullptr;
  EXPECT_EQ(manager.ReserveMemAddress(addr, kTestSize500MB), SUCCESS);
  ASSERT_NE(addr, nullptr);
  EXPECT_NE(mock->last_expect_ptr_, nullptr);
  EXPECT_EQ(aclrtReleaseMemAddress(addr), ACL_SUCCESS);
}

TEST_F(VirtualMemoryManagerTest, ReserveMemAddress_UsesReserveMemAddressOnNonA3) {
  auto mock = std::make_shared<MockVmAclRuntimeStub>();
  mock->soc_name_ = "Ascend910B1";
  InstallRuntimeMock(mock);

  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  void *addr = nullptr;
  EXPECT_EQ(manager.ReserveMemAddress(addr, kTestSize500MB), SUCCESS);
  ASSERT_NE(addr, nullptr);
  EXPECT_EQ(mock->last_expect_ptr_, nullptr);
  EXPECT_EQ(aclrtReleaseMemAddress(addr), ACL_SUCCESS);
}

TEST_F(VirtualMemoryManagerTest, ReserveMemAddress_Success) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  void *addr = nullptr;
  EXPECT_EQ(manager.ReserveMemAddress(addr, kTestSize500MB), SUCCESS);
  ASSERT_NE(addr, nullptr);
  EXPECT_EQ(aclrtReleaseMemAddress(addr), ACL_SUCCESS);
}

TEST_F(VirtualMemoryManagerTest, ReserveMemory_ZeroSize_Fails) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(0, addr), PARAM_INVALID);
}

TEST_F(VirtualMemoryManagerTest, ReserveMemory_1GB_Success) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(kTestSize1GB, addr), SUCCESS);
  EXPECT_NE(addr, 0);
}

TEST_F(VirtualMemoryManagerTest, ReserveMemory_500MB_RoundsUpTo1GB) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(kTestSize500MB, addr), SUCCESS);
  EXPECT_NE(addr, 0);
}

TEST_F(VirtualMemoryManagerTest, ReserveMemory_MultipleAllocations) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  uintptr_t addr1 = 0, addr2 = 0, addr3 = 0;
  EXPECT_EQ(manager.ReserveMemory(kTestSize1GB, addr1), SUCCESS);
  EXPECT_EQ(manager.ReserveMemory(kTestSize2GB, addr2), SUCCESS);
  EXPECT_EQ(manager.ReserveMemory(kTestSize1GB, addr3), SUCCESS);
  EXPECT_NE(addr1, 0);
  EXPECT_NE(addr2, 0);
  EXPECT_NE(addr3, 0);
  // Addresses should not overlap
  EXPECT_NE(addr1, addr2);
  EXPECT_NE(addr1, addr3);
  EXPECT_NE(addr2, addr3);
}

TEST_F(VirtualMemoryManagerTest, ReleaseMemory_Success) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(kTestSize1GB, addr), SUCCESS);
  EXPECT_EQ(manager.ReleaseMemory(addr), SUCCESS);
}

TEST_F(VirtualMemoryManagerTest, ReleaseMemory_InvalidAddress_Fails) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  uintptr_t invalid_addr = 0x1000;  // Not 1GB aligned
  EXPECT_EQ(manager.ReleaseMemory(invalid_addr), PARAM_INVALID);
}

TEST_F(VirtualMemoryManagerTest, ReleaseMemory_UnallocatedAddress_Fails) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  constexpr uintptr_t unallocated_addr = 1024UL * 1024UL * 1024UL * 1024UL * 40UL;  // Start of reserved range
  EXPECT_EQ(manager.ReleaseMemory(unallocated_addr), PARAM_INVALID);
}

TEST_F(VirtualMemoryManagerTest, ReserveMemory_AfterRelease_ReusesAddress) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  uintptr_t addr1 = 0;
  EXPECT_EQ(manager.ReserveMemory(kTestSize1GB, addr1), SUCCESS);
  EXPECT_EQ(manager.ReleaseMemory(addr1), SUCCESS);
  uintptr_t addr2 = 0;
  EXPECT_EQ(manager.ReserveMemory(kTestSize1GB, addr2), SUCCESS);
  // Since we use first-fit, the released block should be reused
  EXPECT_EQ(addr1, addr2);
}

TEST_F(VirtualMemoryManagerTest, ReserveMemory_Exhaustion_Fails) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  // Try to allocate more than total capacity (32TB)
  constexpr size_t huge_size = 1024UL * 1024UL * 1024UL * 1024UL * 33UL;  // 33TB > 32TB
  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(huge_size, addr), RESOURCE_EXHAUSTED);
}

TEST_F(VirtualMemoryManagerTest, ReserveMemory_SizeNearMax_NoOverflow_Fails) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  // A size near SIZE_MAX must not wrap blocks_needed to a tiny value that bypasses the capacity check; it must be
  // rejected as RESOURCE_EXHAUSTED instead of silently succeeding.
  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(std::numeric_limits<size_t>::max(), addr), RESOURCE_EXHAUSTED);
  EXPECT_EQ(addr, 0U);
}

TEST_F(VirtualMemoryManagerTest, Concurrency_MultipleThreads) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  constexpr int kNumThreads = 4;
  std::vector<std::thread> threads;
  std::atomic<int> success_count{0};

  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&manager, &success_count]() {
      uintptr_t addr = 0;
      if (manager.ReserveMemory(kTestSize1GB, addr) == SUCCESS) {
        success_count++;
        manager.ReleaseMemory(addr);
      }
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  EXPECT_EQ(success_count.load(), kNumThreads);
}

TEST_F(VirtualMemoryManagerTest, SetVirtualMemoryCapacity_Success) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  constexpr size_t kCustomCapacityTB = 32UL;  // 32TB
  manager.SetVirtualMemoryCapacity(kCustomCapacityTB);
  EXPECT_EQ(manager.Initialize(), SUCCESS);

  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(kTestSize1GB, addr), SUCCESS);
  EXPECT_NE(addr, 0);
  EXPECT_EQ(manager.ReleaseMemory(addr), SUCCESS);
}

TEST_F(VirtualMemoryManagerTest, SetVirtualMemoryCapacity_AfterInitialized_Fails) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  EXPECT_EQ(manager.Initialize(), SUCCESS);

  constexpr size_t kCustomCapacityTB = 64UL;
  manager.SetVirtualMemoryCapacity(kCustomCapacityTB);

  constexpr size_t k65TB = 65UL * 1024UL * 1024UL * 1024UL * 1024UL;
  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(k65TB, addr), RESOURCE_EXHAUSTED);
}

TEST_F(VirtualMemoryManagerTest, SetGlobalStartAddress_Success) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  constexpr size_t kCustomStartAddrTB = 100UL;
  EXPECT_EQ(manager.SetGlobalStartAddress(kCustomStartAddrTB), SUCCESS);
  EXPECT_EQ(manager.Initialize(), SUCCESS);

  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(kTestSize1GB, addr), SUCCESS);
  EXPECT_NE(addr, 0);
  EXPECT_EQ(manager.ReleaseMemory(addr), SUCCESS);
}

TEST_F(VirtualMemoryManagerTest, SetGlobalStartAddress_ExplicitZeroPassedToRuntime) {
  auto mock = std::make_shared<MockVmAclRuntimeStub>();
  mock->soc_name_ = "Ascend910_9391";
  InstallRuntimeMock(mock);

  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  EXPECT_EQ(manager.SetGlobalStartAddress(0UL), SUCCESS);

  void *addr = nullptr;
  EXPECT_EQ(manager.ReserveMemAddress(addr, kTestSize500MB), SUCCESS);
  ASSERT_NE(addr, nullptr);
  EXPECT_EQ(mock->last_expect_ptr_, nullptr);
  EXPECT_EQ(aclrtReleaseMemAddress(addr), ACL_SUCCESS);
}

TEST_F(VirtualMemoryManagerTest, SetGlobalStartAddress_BoundaryMaxAccepted) {
  EXPECT_EQ(VirtualMemoryManager::GetInstance().SetGlobalStartAddress(1024UL), SUCCESS);
}

TEST_F(VirtualMemoryManagerTest, SetGlobalStartAddress_AfterInitialized_Fails) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  EXPECT_EQ(manager.Initialize(), SUCCESS);

  EXPECT_EQ(manager.SetGlobalStartAddress(50UL), PARAM_INVALID);
}

TEST_F(VirtualMemoryManagerTest, SetGlobalStartAddress_OutOfRange_Fails) {
  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  EXPECT_EQ(manager.SetGlobalStartAddress(1025UL), PARAM_INVALID);
}

TEST_F(VirtualMemoryManagerTest, Gen2_Default40TB_SucceedsSingleRegion) {
  auto mock = std::make_shared<FakeRangeReserveRuntimeStub>("Ascend910_9391");
  InstallRuntimeMock(mock);

  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  ASSERT_EQ(manager.Initialize(), SUCCESS);
  ASSERT_EQ(mock->reserve_calls_.size(), 1U);
  EXPECT_EQ(mock->reserve_calls_[0].expect_ptr, reinterpret_cast<void *>(kTest40TB));
  EXPECT_EQ(mock->reserve_calls_[0].size, 32UL * kTestSize1TB);
  EXPECT_EQ(mock->active_reserves_.size(), 1U);

  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(kTestSize1GB, addr), SUCCESS);
  EXPECT_EQ(addr, kTest40TB);
  EXPECT_EQ(manager.ReleaseMemory(addr), SUCCESS);

  manager.Finalize();
  EXPECT_TRUE(mock->active_reserves_.empty());
}

TEST_F(VirtualMemoryManagerTest, Gen2_ExplicitStartAddressAbove80TB_Allowed) {
  auto mock = std::make_shared<FakeRangeReserveRuntimeStub>("Ascend910_9391");
  InstallRuntimeMock(mock);

  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  ASSERT_EQ(manager.SetVirtualMemoryCapacity(64UL), SUCCESS);
  ASSERT_EQ(manager.SetGlobalStartAddress(80UL), SUCCESS);
  ASSERT_EQ(manager.Initialize(), SUCCESS);

  ASSERT_EQ(mock->reserve_calls_.size(), 1U);
  EXPECT_EQ(mock->reserve_calls_[0].expect_ptr, reinterpret_cast<void *>(kTest80TB));
  EXPECT_EQ(mock->reserve_calls_[0].size, 64UL * kTestSize1TB);

  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(kTestSize1GB, addr), SUCCESS);
  EXPECT_EQ(addr, kTest80TB);
  EXPECT_EQ(manager.ReleaseMemory(addr), SUCCESS);

  manager.Finalize();
  EXPECT_TRUE(mock->active_reserves_.empty());
}

TEST_F(VirtualMemoryManagerTest, Gen2_ExplicitStartPlusCapacityAbove80TB_Allowed) {
  auto mock = std::make_shared<FakeRangeReserveRuntimeStub>("Ascend910_9391");
  InstallRuntimeMock(mock);

  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  ASSERT_EQ(manager.SetVirtualMemoryCapacity(64UL), SUCCESS);
  ASSERT_EQ(manager.SetGlobalStartAddress(40UL), SUCCESS);
  ASSERT_EQ(manager.Initialize(), SUCCESS);

  ASSERT_EQ(mock->reserve_calls_.size(), 1U);
  EXPECT_EQ(mock->reserve_calls_[0].expect_ptr, reinterpret_cast<void *>(kTest40TB));
  EXPECT_EQ(mock->reserve_calls_[0].size, 64UL * kTestSize1TB);

  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(kTestSize1GB, addr), SUCCESS);
  EXPECT_EQ(addr, kTest40TB);
  EXPECT_EQ(manager.ReleaseMemory(addr), SUCCESS);

  manager.Finalize();
  EXPECT_TRUE(mock->active_reserves_.empty());
}

TEST_F(VirtualMemoryManagerTest, Gen2_Default40TBFails_Chunks2TBWithin40To80TB) {
  auto mock = std::make_shared<FakeRangeReserveRuntimeStub>("Ascend910_9391");
  // Simulate HCCL occupying 40 TB and an obstacle at 55.5 TB.
  mock->blocked_ranges_.push_back({kTest40TB, 42UL * kTestSize1TB});
  mock->blocked_ranges_.push_back({55UL * kTestSize1TB + 512UL * kTestSize1GB, 56UL * kTestSize1TB});
  InstallRuntimeMock(mock);

  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  ASSERT_EQ(manager.Initialize(), SUCCESS);

  // 1 full attempt at 40 TB + 18 chunk attempts (2 blocked + 16 succeeded) = 19 calls total.
  EXPECT_EQ(mock->reserve_calls_.size(), 19U);
  for (const auto &call : mock->reserve_calls_) {
    EXPECT_GE(reinterpret_cast<uintptr_t>(call.expect_ptr), kTest40TB);
    EXPECT_LT(reinterpret_cast<uintptr_t>(call.expect_ptr), kTest80TB);
  }
  EXPECT_EQ(mock->active_reserves_.size(), 16U);
  for (const auto &region : mock->active_reserves_) {
    EXPECT_GE(region.first, kTest40TB);
    EXPECT_LE(region.first + region.second, kTest80TB);
  }

  // Single allocation larger than the 2 TB chunk size cannot span disjoint VA regions.
  uintptr_t oversized_addr = 0;
  EXPECT_EQ(manager.ReserveMemory(3UL * kTestSize1TB, oversized_addr), RESOURCE_EXHAUSTED);

  // Allocate across multiple 2 TB regions and verify aligned release validation.
  std::vector<uintptr_t> allocated;
  for (int i = 0; i < 10; ++i) {
    uintptr_t addr = 0;
    ASSERT_EQ(manager.ReserveMemory(kTestSize2TB, addr), SUCCESS);
    allocated.push_back(addr);
  }
  EXPECT_EQ(manager.ReleaseMemory(allocated.front() + 0x1000UL), PARAM_INVALID);
  for (uintptr_t addr : allocated) {
    EXPECT_EQ(manager.ReleaseMemory(addr), SUCCESS);
  }

  manager.Finalize();
  EXPECT_EQ(mock->released_ptrs_.size(), 16U);
  EXPECT_TRUE(mock->active_reserves_.empty());
}

TEST_F(VirtualMemoryManagerTest, Gen2_Custom32TBWithoutStartAddress_SameFallbackAsDefault) {
  auto mock = std::make_shared<FakeRangeReserveRuntimeStub>("Ascend910_9391");
  mock->blocked_ranges_.push_back({56UL * kTestSize1TB, 57UL * kTestSize1TB});
  InstallRuntimeMock(mock);

  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  ASSERT_EQ(manager.SetVirtualMemoryCapacity(32UL), SUCCESS);
  ASSERT_EQ(manager.Initialize(), SUCCESS);

  // 1 full attempt at 40 TB + 17 chunk attempts (1 blocked at 56 TB + 16 succeeded) = 18 calls total.
  EXPECT_EQ(mock->reserve_calls_.size(), 18U);
  EXPECT_EQ(mock->reserve_calls_[0].expect_ptr, reinterpret_cast<void *>(kTest40TB));
  EXPECT_EQ(mock->active_reserves_.size(), 16U);
  for (const auto &region : mock->active_reserves_) {
    EXPECT_GE(region.first, kTest40TB);
    EXPECT_LE(region.first + region.second, kTest80TB);
  }

  manager.Finalize();
  EXPECT_TRUE(mock->active_reserves_.empty());
}

TEST_F(VirtualMemoryManagerTest, Gen2_40TBCapacityWithoutStartAddress_SucceedsAt40TB) {
  auto mock = std::make_shared<FakeRangeReserveRuntimeStub>("Ascend910_9391");
  InstallRuntimeMock(mock);

  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  ASSERT_EQ(manager.SetVirtualMemoryCapacity(40UL), SUCCESS);
  ASSERT_EQ(manager.Initialize(), SUCCESS);

  ASSERT_EQ(mock->reserve_calls_.size(), 1U);
  EXPECT_EQ(mock->reserve_calls_[0].expect_ptr, reinterpret_cast<void *>(kTest40TB));
  EXPECT_EQ(mock->reserve_calls_[0].size, 40UL * kTestSize1TB);
}

TEST_F(VirtualMemoryManagerTest, Gen2_CapacityExceeds40TBWithoutStartAddress_Fails) {
  auto mock = std::make_shared<FakeRangeReserveRuntimeStub>("Ascend910_9391");
  InstallRuntimeMock(mock);

  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  ASSERT_EQ(manager.SetVirtualMemoryCapacity(48UL), SUCCESS);
  EXPECT_EQ(manager.Initialize(), PARAM_INVALID);
  EXPECT_TRUE(mock->reserve_calls_.empty());
}

TEST_F(VirtualMemoryManagerTest, Gen2_ReturnedVaExceeds80TB_ReleasedAndRolledBack) {
  auto mock = std::make_shared<FakeRangeReserveRuntimeStub>("Ascend910_9391");
  mock->max_single_reserve_size_ = kTestSize2TB;
  // Block [40 TB, 76 TB) (18 2TB slots) and force the 76 TB slot (call index 19) to return 79 TB (> 80 TB top).
  mock->blocked_ranges_.push_back({kTest40TB, 76UL * kTestSize1TB});
  mock->override_returned_va_call_index_ = 19U;  // 1 full call (failed by size) + 18 blocked 2TB slots -> 76TB slot
  mock->override_returned_va_ = 79UL * kTestSize1TB;
  InstallRuntimeMock(mock);

  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  EXPECT_EQ(manager.Initialize(), FAILED);
  EXPECT_TRUE(mock->active_reserves_.empty());
  EXPECT_EQ(mock->released_ptrs_.size(), 2U);
}

TEST_F(VirtualMemoryManagerTest, Gen2_OutOfRangeReleaseFailsOnce_RetriedOnRollback) {
  auto mock = std::make_shared<FakeRangeReserveRuntimeStub>("Ascend910_9391");
  mock->max_single_reserve_size_ = kTestSize2TB;
  mock->blocked_ranges_.push_back({kTest40TB, 76UL * kTestSize1TB});
  mock->override_returned_va_call_index_ = 19U;
  // Use 80 TB ([80 TB, 82 TB)) so it exceeds the bound without overlapping the next [78 TB, 80 TB) probe slot.
  mock->override_returned_va_ = kTest80TB;
  mock->fail_next_release_count_ = 1U;
  InstallRuntimeMock(mock);

  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  EXPECT_EQ(manager.Initialize(), FAILED);
  EXPECT_TRUE(mock->active_reserves_.empty());
  // 1st release on 80 TB fails, rollback retries 80 TB and also releases the 78 TB region -> 3 release calls total.
  EXPECT_EQ(mock->released_ptrs_.size(), 3U);
}

TEST_F(VirtualMemoryManagerTest, A5_FullFails_Chunks2TBWithNullptrWithout80TBLimit) {
  auto mock = std::make_shared<FakeRangeReserveRuntimeStub>("Ascend950A");
  mock->max_single_reserve_size_ = kTestSize2TB;
  mock->next_auto_va_ = 100UL * kTestSize1TB;
  InstallRuntimeMock(mock);

  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  ASSERT_EQ(manager.Initialize(), SUCCESS);

  // 1 full attempt + 16 chunk attempts = 17 calls, all with expectPtr == nullptr.
  ASSERT_EQ(mock->reserve_calls_.size(), 17U);
  for (const auto &call : mock->reserve_calls_) {
    EXPECT_EQ(call.expect_ptr, nullptr);
  }
  EXPECT_EQ(mock->active_reserves_.size(), 16U);

  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(kTestSize1GB, addr), SUCCESS);
  EXPECT_GE(addr, 100UL * kTestSize1TB);
  EXPECT_EQ(manager.ReleaseMemory(addr), SUCCESS);

  manager.Finalize();
  EXPECT_EQ(mock->released_ptrs_.size(), 16U);
  EXPECT_TRUE(mock->active_reserves_.empty());
}

TEST_F(VirtualMemoryManagerTest, A5_ChunkFailureRollsBackPreviouslyReservedChunks) {
  auto mock = std::make_shared<FakeRangeReserveRuntimeStub>("Ascend950A");
  mock->max_single_reserve_size_ = kTestSize2TB;
  mock->succeed_reserve_limit_ = 3U;
  InstallRuntimeMock(mock);

  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  EXPECT_EQ(manager.Initialize(), FAILED);
  EXPECT_EQ(mock->released_ptrs_.size(), 3U);
  EXPECT_TRUE(mock->active_reserves_.empty());
}

TEST_F(VirtualMemoryManagerTest, ExplicitZeroStartAddress_FullFails_FallsBackTo2TBNullptrChunks) {
  auto mock = std::make_shared<FakeRangeReserveRuntimeStub>("Ascend910_9391");
  mock->max_single_reserve_size_ = kTestSize2TB;
  mock->next_auto_va_ = 90UL * kTestSize1TB;
  InstallRuntimeMock(mock);

  VirtualMemoryManager &manager = VirtualMemoryManager::GetInstance();
  ASSERT_EQ(manager.SetGlobalStartAddress(0UL), SUCCESS);
  ASSERT_EQ(manager.Initialize(), SUCCESS);

  ASSERT_EQ(mock->reserve_calls_.size(), 17U);
  for (const auto &call : mock->reserve_calls_) {
    EXPECT_EQ(call.expect_ptr, nullptr);
  }
  manager.Finalize();
  EXPECT_EQ(mock->released_ptrs_.size(), 16U);
}

}  // namespace hixl
