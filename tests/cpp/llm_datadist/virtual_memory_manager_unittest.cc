/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>
#include "adxl/virtual_memory_manager.h"
#include "depends/ascendcl/src/ascendcl_stub.h"
#include "acl/acl.h"
#include "common/def_types.h"

namespace adxl {
namespace {
constexpr size_t kTestSize1GB = 1024UL * 1024UL * 1024UL;
constexpr size_t kTestSize2GB = 2UL * kTestSize1GB;
constexpr size_t kTestSize500MB = 500UL * 1024UL * 1024UL;
constexpr const char *kSoName = "libascendcl_stub.so";

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
    VirtualMemoryManager::GetInstance().SetSoName(kSoName);
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
};

TEST_F(VirtualMemoryManagerTest, GetInstance_ReturnsSameInstance) {
  VirtualMemoryManager& instance1 = VirtualMemoryManager::GetInstance();
  VirtualMemoryManager& instance2 = VirtualMemoryManager::GetInstance();
  EXPECT_EQ(&instance1, &instance2);
}

TEST_F(VirtualMemoryManagerTest, Initialize_Success) {
  VirtualMemoryManager& manager = VirtualMemoryManager::GetInstance();
  EXPECT_EQ(manager.Initialize(), SUCCESS);
  // Second initialization should also succeed
  EXPECT_EQ(manager.Initialize(), SUCCESS);
}

TEST_F(VirtualMemoryManagerTest, ReserveMemory_ZeroSize_Fails) {
  VirtualMemoryManager& manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(0, addr), PARAM_INVALID);
}

TEST_F(VirtualMemoryManagerTest, ReserveMemory_1GB_Success) {
  VirtualMemoryManager& manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(kTestSize1GB, addr), SUCCESS);
  EXPECT_NE(addr, 0);
}

TEST_F(VirtualMemoryManagerTest, ReserveMemory_500MB_RoundsUpTo1GB) {
  VirtualMemoryManager& manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(kTestSize500MB, addr), SUCCESS);
  EXPECT_NE(addr, 0);
}

TEST_F(VirtualMemoryManagerTest, ReserveMemory_MultipleAllocations) {
  VirtualMemoryManager& manager = VirtualMemoryManager::GetInstance();
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
  VirtualMemoryManager& manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(kTestSize1GB, addr), SUCCESS);
  EXPECT_EQ(manager.ReleaseMemory(addr), SUCCESS);
}

TEST_F(VirtualMemoryManagerTest, ReleaseMemory_InvalidAddress_Fails) {
  VirtualMemoryManager& manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  uintptr_t invalid_addr = 0x1000; // Not 1GB aligned
  EXPECT_EQ(manager.ReleaseMemory(invalid_addr), PARAM_INVALID);
}

TEST_F(VirtualMemoryManagerTest, ReleaseMemory_UnallocatedAddress_Fails) {
  VirtualMemoryManager& manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  constexpr uintptr_t unallocated_addr = 1024UL * 1024UL * 1024UL * 1024UL * 40UL; // Start of reserved range
  EXPECT_EQ(manager.ReleaseMemory(unallocated_addr), PARAM_INVALID);
}

TEST_F(VirtualMemoryManagerTest, ReserveMemory_AfterRelease_ReusesAddress) {
  VirtualMemoryManager& manager = VirtualMemoryManager::GetInstance();
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
  VirtualMemoryManager& manager = VirtualMemoryManager::GetInstance();
  manager.Initialize();
  // Try to allocate more than total capacity (128TB)
  constexpr size_t huge_size = 1024UL * 1024UL * 1024UL * 1024UL * 129UL; // 129TB > 128TB
  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(huge_size, addr), RESOURCE_EXHAUSTED);
}

TEST_F(VirtualMemoryManagerTest, Concurrency_MultipleThreads) {
  VirtualMemoryManager& manager = VirtualMemoryManager::GetInstance();
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

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(success_count.load(), kNumThreads);
}

TEST_F(VirtualMemoryManagerTest, SetVirtualMemoryCapacity_Success) {
  VirtualMemoryManager& manager = VirtualMemoryManager::GetInstance();
  constexpr size_t kCustomCapacityTB = 32UL; // 32TB
  manager.SetVirtualMemoryCapacity(kCustomCapacityTB);
  EXPECT_EQ(manager.Initialize(), SUCCESS);

  // Try to allocate memory to verify capacity
  uintptr_t addr = 0;
  // Try to allocate 1GB - should succeed
  EXPECT_EQ(manager.ReserveMemory(kTestSize1GB, addr), SUCCESS);
  EXPECT_NE(addr, 0);
  EXPECT_EQ(manager.ReleaseMemory(addr), SUCCESS);
}

TEST_F(VirtualMemoryManagerTest, SetVirtualMemoryCapacity_AfterInitialized_Fails) {
  VirtualMemoryManager& manager = VirtualMemoryManager::GetInstance();
  EXPECT_EQ(manager.Initialize(), SUCCESS);

  // Try to set capacity after initialization - should fail silently
  constexpr size_t kCustomCapacityTB = 64UL;
  manager.SetVirtualMemoryCapacity(kCustomCapacityTB);

  // The capacity should not change, try to allocate more than default capacity
  // Default capacity is 64TB (kDefaultNumBlocks = 64 * 1024 blocks = 64TB)
  // So allocating 65TB should fail
  constexpr size_t k65TB = 65UL * 1024UL * 1024UL * 1024UL * 1024UL;
  uintptr_t addr = 0;
  EXPECT_EQ(manager.ReserveMemory(k65TB, addr), RESOURCE_EXHAUSTED);
}

}  // namespace adxl