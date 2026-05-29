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

#include "cache_mgr/comm_mem_manager.h"

namespace llm {
namespace {
constexpr int64_t kTensorSize = 4096;
constexpr uintptr_t kTestAddr = 0x100000000UL;
constexpr int64_t kCacheId1 = 1;
constexpr int64_t kCacheId2 = 2;

class MockTransferEngine : public TransferEngine {
 public:
  MockTransferEngine() : TransferEngine(0) {}

  ge::Status Initialize(const std::map<ge::AscendString, ge::AscendString> &) override {
    return ge::SUCCESS;
  }

  void Finalize() override {}

  ge::Status RegisterMem(void *addr, uint64_t, CommMemType, void *&handle) override {
    handle = addr;
    register_count_.fetch_add(1, std::memory_order_relaxed);
    return ge::SUCCESS;
  }

  ge::Status UnregisterMem(void *) override {
    unregister_count_.fetch_add(1, std::memory_order_relaxed);
    return ge::SUCCESS;
  }

  ge::Status LinkClusters(const std::vector<ClusterInfo> &, std::vector<ge::Status> &, int32_t) override {
    return ge::SUCCESS;
  }

  ge::Status UnlinkClusters(const std::vector<ClusterInfo> &, std::vector<ge::Status> &, int32_t, bool) override {
    return ge::SUCCESS;
  }

  ge::Status Link(std::string &, const std::map<uint64_t, uint32_t> &, std::string &, uint64_t &) override {
    return ge::SUCCESS;
  }

  ge::Status Unlink(uint64_t) override {
    return ge::SUCCESS;
  }

  void UnlinkAllClusters() override {}

  ge::Status QueryRegisterMemStatus(uint64_t, RegisterMemoryStatus &) override {
    return ge::SUCCESS;
  }

  ge::Status SwitchRole(const std::string &, const std::map<std::string, std::string> &) override {
    return ge::SUCCESS;
  }

  int32_t GetRegisterCount() const {
    return register_count_.load(std::memory_order_relaxed);
  }

  int32_t GetUnregisterCount() const {
    return unregister_count_.load(std::memory_order_relaxed);
  }

 private:
  std::atomic<int32_t> register_count_{0};
  std::atomic<int32_t> unregister_count_{0};
};

CacheDesc MakeRemoteAccessibleCacheDesc() {
  CacheDesc cache_desc{};
  cache_desc.remote_accessible = true;
  cache_desc.placement = static_cast<uint32_t>(CachePlacement::DEVICE);
  return cache_desc;
}
}  // namespace

class CommMemManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(comm_mem_manager_.Initialize(&transfer_engine_), ge::SUCCESS);
  }

  void TearDown() override {
    comm_mem_manager_.Finalize();
  }

  MockTransferEngine transfer_engine_;
  CommMemManager comm_mem_manager_;
};

TEST_F(CommMemManagerTest, RegisterCacheMem_SkipWhenNotRemoteAccessible) {
  CacheDesc cache_desc{};
  cache_desc.remote_accessible = false;
  EXPECT_EQ(comm_mem_manager_.RegisterCacheMem(kCacheId1, cache_desc, {kTestAddr}, kTensorSize), ge::SUCCESS);
  EXPECT_EQ(transfer_engine_.GetRegisterCount(), 0);
}

TEST_F(CommMemManagerTest, RegisterAndUnregisterCacheMem) {
  const CacheDesc cache_desc = MakeRemoteAccessibleCacheDesc();
  EXPECT_EQ(comm_mem_manager_.RegisterCacheMem(kCacheId1, cache_desc, {kTestAddr}, kTensorSize), ge::SUCCESS);
  EXPECT_EQ(transfer_engine_.GetRegisterCount(), 1);
  EXPECT_EQ(comm_mem_manager_.UnregisterCacheMem(kCacheId1), ge::SUCCESS);
  EXPECT_EQ(transfer_engine_.GetUnregisterCount(), 1);
}

TEST_F(CommMemManagerTest, RegisterCacheMem_DeduplicateSharedAddr) {
  const CacheDesc cache_desc = MakeRemoteAccessibleCacheDesc();
  EXPECT_EQ(comm_mem_manager_.RegisterCacheMem(kCacheId1, cache_desc, {kTestAddr}, kTensorSize), ge::SUCCESS);
  EXPECT_EQ(comm_mem_manager_.RegisterCacheMem(kCacheId2, cache_desc, {kTestAddr}, kTensorSize), ge::SUCCESS);
  EXPECT_EQ(transfer_engine_.GetRegisterCount(), 1);
  EXPECT_EQ(comm_mem_manager_.UnregisterCacheMem(kCacheId2), ge::SUCCESS);
  EXPECT_EQ(transfer_engine_.GetUnregisterCount(), 0);
  EXPECT_EQ(comm_mem_manager_.UnregisterCacheMem(kCacheId1), ge::SUCCESS);
  EXPECT_EQ(transfer_engine_.GetUnregisterCount(), 1);
}

TEST_F(CommMemManagerTest, UnregisterCacheMem_NotFound) {
  EXPECT_EQ(comm_mem_manager_.UnregisterCacheMem(kCacheId1), ge::SUCCESS);
  EXPECT_EQ(transfer_engine_.GetUnregisterCount(), 0);
}

TEST_F(CommMemManagerTest, RegisterCacheMem_InvalidAddr) {
  const CacheDesc cache_desc = MakeRemoteAccessibleCacheDesc();
  EXPECT_EQ(comm_mem_manager_.RegisterCacheMem(kCacheId1, cache_desc, {0U}, kTensorSize), ge::LLM_PARAM_INVALID);
}
}  // namespace llm
