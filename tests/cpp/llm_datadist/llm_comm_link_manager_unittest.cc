/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <vector>
#include <gtest/gtest.h>

#include "llm_datadist_v2.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "depends/llm_datadist/src/hccl_stub.h"

using namespace std;
using namespace ::testing;
using namespace llm;

namespace llm {
namespace {
HcclResult HcclCommBindMemFail(HcclComm comm, void **memHandle) {
  return HcclResult::HCCL_E_DRV;
}

HcclResult HcclCommPrepareFail(HcclComm comm, void **memHandle) {
  return HcclResult::HCCL_E_TIMEOUT;
}

HcclResult HcclExchangeMemDesc1(HcclComm comm, uint32_t remoteRank, HcclMemDescs *local, int timeout,
                                HcclMemDescs *remote, uint32_t *actualNum) {
  for (uint32_t i = 0U; i < local->arrayLength; ++i) {
    strcpy(remote->array[i].desc, local->array[i].desc);
  }
  *actualNum = local->arrayLength;
  remote->arrayLength = local->arrayLength;
  return HcclResult::HCCL_SUCCESS;
}

HcclResult HcclBatchGet1(HcclComm comm, uint32_t remoteRank, HcclOneSideOpDesc *desc, uint32_t descNum,
                         aclrtStream stream) {
  *static_cast<uint64_t *>(desc->localAddr) = UINT64_MAX;
  return HcclResult::HCCL_SUCCESS;
}
uintptr_t mock_handle = 0x8001;
class MockMmpa : public MmpaStubApiGe {
 public:
  void *DlOpen(const char *file_name, int32_t mode) override {
    return reinterpret_cast<void *>(mock_handle);
  }

  void *DlSym(void *handle, const char *func_name) override {
    static const std::map<std::string, void*> func_map = {
        {"HcclCommInitClusterInfoMemConfig", reinterpret_cast<void*>(&HcclCommInitClusterInfoMemConfig)},
        {"HcclExchangeMemDesc",              reinterpret_cast<void*>(&HcclExchangeMemDesc1)},
        {"HcclCommDestroy",                  reinterpret_cast<void*>(&HcclCommDestroy)},
        {"HcclBatchPut",                     reinterpret_cast<void*>(&HcclBatchPut)},
        {"HcclBatchGet",                     reinterpret_cast<void*>(&HcclBatchGet1)},
        {"HcclRemapRegistedMemory",          reinterpret_cast<void*>(&HcclRemapRegistedMemory)},
        {"HcclRegisterGlobalMem",            reinterpret_cast<void*>(&HcclRegisterGlobalMem)},
        {"HcclDeregisterGlobalMem",          reinterpret_cast<void*>(&HcclDeregisterGlobalMem)},
        {"HcclCommBindMem",                  reinterpret_cast<void*>(&HcclCommBindMem)},
        {"HcclCommUnbindMem",                reinterpret_cast<void*>(&HcclCommUnbindMem)},
        {"HcclCommPrepare",                  reinterpret_cast<void*>(&HcclCommPrepare)},
    };
    auto it = func_map.find(func_name);
    if (it != func_map.end()) {
      LLMLOGI("%s addr: %lu", func_name,
              static_cast<uint64_t>(reinterpret_cast<uintptr_t>(it->second)));
      return it->second;
    }
    return nullptr;
  }

  int32_t DlClose(void *handle) override {
    return 0;
  }
};
class MockMmpaCommBindMemFail : public MmpaStubApiGe {
 public:
  void *DlOpen(const char *file_name, int32_t mode) override {
    return reinterpret_cast<void *>(mock_handle);
  }
  void *DlSym(void *handle, const char *func_name) override {
    static const std::map<std::string, void*> func_map = {
        {"HcclCommInitClusterInfoMemConfig", reinterpret_cast<void*>(&HcclCommInitClusterInfoMemConfig)},
        {"HcclExchangeMemDesc",              reinterpret_cast<void*>(&HcclExchangeMemDesc1)},
        {"HcclCommDestroy",                  reinterpret_cast<void*>(&HcclCommDestroy)},
        {"HcclBatchPut",                     reinterpret_cast<void*>(&HcclBatchPut)},
        {"HcclBatchGet",                     reinterpret_cast<void*>(&HcclBatchGet1)},
        {"HcclRemapRegistedMemory",          reinterpret_cast<void*>(&HcclRemapRegistedMemory)},
        {"HcclRegisterGlobalMem",            reinterpret_cast<void*>(&HcclRegisterGlobalMem)},
        {"HcclDeregisterGlobalMem",          reinterpret_cast<void*>(&HcclDeregisterGlobalMem)},
        {"HcclCommBindMem",                  reinterpret_cast<void*>(&HcclCommBindMemFail)},
        {"HcclCommUnbindMem",                reinterpret_cast<void*>(&HcclCommUnbindMem)},
        {"HcclCommPrepare",                  reinterpret_cast<void*>(&HcclCommPrepare)},
    };
    auto it = func_map.find(func_name);
    if (it != func_map.end()) {
      LLMLOGI("%s addr: %lu", func_name,
              static_cast<uint64_t>(reinterpret_cast<uintptr_t>(it->second)));
      return it->second;
    }
    return nullptr;
  }
  int32_t DlClose(void *handle) override {
    return 0;
  }
};

class MockMmpaCommPrepareFail: public MmpaStubApiGe {
  public:
   void *DlOpen(const char *file_name, int32_t mode) override {
    return reinterpret_cast<void *>(mock_handle);
   }
   void *DlSym(void *handle, const char *func_name) override {
    static const std::map<std::string, void*> func_map = {
        {"HcclCommInitClusterInfoMemConfig", reinterpret_cast<void*>(&HcclCommInitClusterInfoMemConfig)},
        {"HcclExchangeMemDesc",              reinterpret_cast<void*>(&HcclExchangeMemDesc1)},
        {"HcclCommDestroy",                  reinterpret_cast<void*>(&HcclCommDestroy)},
        {"HcclBatchPut",                     reinterpret_cast<void*>(&HcclBatchPut)},
        {"HcclBatchGet",                     reinterpret_cast<void*>(&HcclBatchGet1)},
        {"HcclRemapRegistedMemory",          reinterpret_cast<void*>(&HcclRemapRegistedMemory)},
        {"HcclRegisterGlobalMem",            reinterpret_cast<void*>(&HcclRegisterGlobalMem)},
        {"HcclDeregisterGlobalMem",          reinterpret_cast<void*>(&HcclDeregisterGlobalMem)},
        {"HcclCommBindMem",                  reinterpret_cast<void*>(&HcclCommBindMem)},
        {"HcclCommUnbindMem",                reinterpret_cast<void*>(&HcclCommUnbindMem)},
        {"HcclCommPrepare",                  reinterpret_cast<void*>(&HcclCommPrepareFail)},
    };
    auto it = func_map.find(func_name);
    if (it != func_map.end()) {
      LLMLOGI("%s addr: %lu", func_name,
              static_cast<uint64_t>(reinterpret_cast<uintptr_t>(it->second)));
      return it->second;
    }
    return nullptr;
  }
  int32_t DlClose(void *handle) override {
    return 0;
  }
};

class MockMmpaLongTimeRegister : public MmpaStubApiGe {
 public:
  void *DlOpen(const char *file_name, int32_t mode) override {
    return reinterpret_cast<void *>(mock_handle);
  }

  void *DlSym(void *handle, const char *func_name) override {
    static const std::map<std::string, void*> func_map = {
        {"HcclCommInitClusterInfoMemConfig", reinterpret_cast<void*>(&HcclCommInitClusterInfoMemConfig)},
        {"HcclExchangeMemDesc",              reinterpret_cast<void*>(&HcclExchangeMemDesc1)},
        {"HcclCommDestroy",                  reinterpret_cast<void*>(&HcclCommDestroy)},
        {"HcclBatchPut",                     reinterpret_cast<void*>(&HcclBatchPut)},
        {"HcclBatchGet",                     reinterpret_cast<void*>(&HcclBatchGet1)},
        {"HcclRemapRegistedMemory",          reinterpret_cast<void*>(&HcclRemapRegistedMemory)},
        {"HcclRegisterGlobalMem",            reinterpret_cast<void*>(&HcclRegisterGlobalMem)},
        {"HcclDeregisterGlobalMem",          reinterpret_cast<void*>(&HcclDeregisterGlobalMem)},
        {"HcclCommBindMem",                  reinterpret_cast<void*>(&HcclCommBindMem)},
        {"HcclCommUnbindMem",                reinterpret_cast<void*>(&HcclCommUnbindMem)},
        {"HcclCommPrepare",                  reinterpret_cast<void*>(&HcclCommPrepare)},
    };
    auto it = func_map.find(func_name);
    if (it != func_map.end()) {
        LLMLOGI("%s addr: %lu", func_name,
               static_cast<uint64_t>(reinterpret_cast<uintptr_t>(it->second)));
        return it->second;
    }
    return nullptr;
  }

  int32_t DlClose(void *handle) override {
    return 0;
  }
};
}  // namespace
class LLMCommLinkManagerUTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  // 在测试类中进行清理工作，如果需要的话
  void TearDown() override {
    HcclAdapter::GetInstance().Finalize();
  }
};

TEST_F(LLMCommLinkManagerUTest, LINK_REGISTER_FAILED_AND_NOUNLINK) {
  MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpaCommBindMemFail>());
  LLMDataDistV2 llm_datadist(1U);
  std::map<ge::AscendString, ge::AscendString> options{};
  options["llm.Role"] = "Decoder";
  options["llm.LinkTotalTime"] = "30";
  options["llm.LinkRetryCount"] = "2";
  EXPECT_EQ(llm_datadist.LLMDataDistInitialize(options), ge::SUCCESS);

  CacheDesc cache_desc{};
  cache_desc.num_tensors = 1U;
  cache_desc.data_type = ge::DT_FLOAT;
  cache_desc.shape = {2, 3};
  cache_desc.placement = 1U;
  Cache cache{};
  (void)cache.per_device_tensor_addrs.emplace_back(std::vector<uint64_t>{0x8001U});
  std::vector<CacheKey> cache_keys = {};
  CacheKey cache_key{};
  cache_key.is_allocate_blocks = true;
  cache_key.model_id = 0;
  cache_key.req_id = UINT64_MAX;
  cache_keys.emplace_back(cache_key);
  EXPECT_EQ(llm_datadist.RegisterCache(cache_desc, cache, cache_keys), ge::SUCCESS);

  std::map<uint64_t, uint32_t> cluster2rank{{1, 0}, {2, 1}};
  std::string rank_table;
  uint64_t comm_id;
  std::string cluster_name = "link";
  EXPECT_EQ(llm_datadist.Link(cluster_name, cluster2rank, rank_table, comm_id), ge::SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  RegisterMemoryStatus status;
  EXPECT_EQ(llm_datadist.QueryRegisterMemStatus(comm_id, status), ge::SUCCESS);
  EXPECT_EQ(status, RegisterMemoryStatus::FAILED);
  llm_datadist.LLMDataDistFinalize();
}

TEST_F(LLMCommLinkManagerUTest, LinkCommPrepareFailed) {
  MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpaCommPrepareFail>());
  LLMDataDistV2 llm_datadist(1U);
  std::map<ge::AscendString, ge::AscendString> options{};
  options["llm.Role"] = "Decoder";
  options["llm.LinkTotalTime"] = "10";
  options["llm.LinkRetryCount"] = "2";
  EXPECT_EQ(llm_datadist.LLMDataDistInitialize(options), ge::SUCCESS);

  CacheDesc cache_desc{};
  cache_desc.num_tensors = 1U;
  cache_desc.data_type = ge::DT_FLOAT;
  cache_desc.shape = {2, 3};
  cache_desc.placement = 1U;
  Cache cache{};
  (void)cache.per_device_tensor_addrs.emplace_back(std::vector<uint64_t>{0x8001U});
  std::vector<CacheKey> cache_keys = {};
  CacheKey cache_key{};
  cache_key.is_allocate_blocks = true;
  cache_key.model_id = 0;
  cache_key.req_id = UINT64_MAX;
  cache_keys.emplace_back(cache_key);
  EXPECT_EQ(llm_datadist.RegisterCache(cache_desc, cache, cache_keys), ge::SUCCESS);

  std::map<uint64_t, uint32_t> cluster2rank{{1, 0}, {2, 1}};
  std::string rank_table;
  uint64_t comm_id;
  std::string cluster_name = "link";
  EXPECT_EQ(llm_datadist.Link(cluster_name, cluster2rank, rank_table, comm_id), ge::SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  RegisterMemoryStatus status;
  EXPECT_EQ(llm_datadist.QueryRegisterMemStatus(comm_id, status), ge::SUCCESS);
  EXPECT_EQ(status, RegisterMemoryStatus::FAILED);
  llm_datadist.LLMDataDistFinalize();
}

TEST_F(LLMCommLinkManagerUTest, LinkAndUnlinkSuc) {
  MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpa>());
  LLMDataDistV2 llm_datadist(1U);
  std::map<ge::AscendString, ge::AscendString> options{};
  options["llm.Role"] = "Decoder";
  EXPECT_EQ(llm_datadist.LLMDataDistInitialize(options), ge::SUCCESS);

  CacheDesc cache_desc{};
  cache_desc.num_tensors = 1U;
  cache_desc.data_type = ge::DT_FLOAT;
  cache_desc.shape = {2, 3};
  cache_desc.placement = 1U;
  Cache cache{};
  (void)cache.per_device_tensor_addrs.emplace_back(std::vector<uint64_t>{0x8001U});
  std::vector<CacheKey> cache_keys = {};
  CacheKey cache_key{};
  cache_key.is_allocate_blocks = true;
  cache_key.model_id = 0;
  cache_key.req_id = UINT64_MAX;
  cache_keys.emplace_back(cache_key);
  EXPECT_EQ(llm_datadist.RegisterCache(cache_desc, cache, cache_keys), ge::SUCCESS);

  std::map<uint64_t, uint32_t> cluster2rank{{1, 0}, {2, 1}};
  std::string rank_table;
  uint64_t comm_id;
  std::string cluster_name = "link";
  EXPECT_EQ(llm_datadist.Link(cluster_name, cluster2rank, rank_table, comm_id), ge::SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  RegisterMemoryStatus status;
  EXPECT_EQ(llm_datadist.QueryRegisterMemStatus(comm_id, status), ge::SUCCESS);
  EXPECT_EQ(status, RegisterMemoryStatus::OK);
  EXPECT_EQ(llm_datadist.Unlink(comm_id), ge::SUCCESS);
  llm_datadist.LLMDataDistFinalize();
}

TEST_F(LLMCommLinkManagerUTest, UnlinkWhenLinkNotFinished) {
  MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpaLongTimeRegister>());
  LLMDataDistV2 llm_datadist(1U);
  std::map<ge::AscendString, ge::AscendString> options{};
  options["llm.Role"] = "Decoder";
  EXPECT_EQ(llm_datadist.LLMDataDistInitialize(options), ge::SUCCESS);

  CacheDesc cache_desc{};
  cache_desc.num_tensors = 1U;
  cache_desc.data_type = ge::DT_FLOAT;
  cache_desc.shape = {2, 3};
  cache_desc.placement = 1U;
  Cache cache{};
  (void)cache.per_device_tensor_addrs.emplace_back(std::vector<uint64_t>{0x8001U});
  std::vector<CacheKey> cache_keys = {};
  CacheKey cache_key{};
  cache_key.is_allocate_blocks = true;
  cache_key.model_id = 0;
  cache_key.req_id = UINT64_MAX;
  cache_keys.emplace_back(cache_key);
  EXPECT_EQ(llm_datadist.RegisterCache(cache_desc, cache, cache_keys), ge::SUCCESS);

  std::map<uint64_t, uint32_t> cluster2rank{{1, 0}, {2, 1}};
  std::string rank_table;
  uint64_t comm_id;
  std::string cluster_name = "link";
  EXPECT_EQ(llm_datadist.Link(cluster_name, cluster2rank, rank_table, comm_id), ge::SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_EQ(llm_datadist.Unlink(comm_id), ge::SUCCESS);
  llm_datadist.LLMDataDistFinalize();
}

TEST_F(LLMCommLinkManagerUTest, FinalizeWhenLinkNotFinished) {
  MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpaLongTimeRegister>());
  LLMDataDistV2 llm_datadist(1U);
  std::map<ge::AscendString, ge::AscendString> options{};
  options["llm.Role"] = "Decoder";
  EXPECT_EQ(llm_datadist.LLMDataDistInitialize(options), ge::SUCCESS);

  CacheDesc cache_desc{};
  cache_desc.num_tensors = 1U;
  cache_desc.data_type = ge::DT_FLOAT;
  cache_desc.shape = {2, 3};
  cache_desc.placement = 1U;
  Cache cache{};
  (void)cache.per_device_tensor_addrs.emplace_back(std::vector<uint64_t>{0x8001U});
  std::vector<CacheKey> cache_keys = {};
  CacheKey cache_key{};
  cache_key.is_allocate_blocks = true;
  cache_key.model_id = 0;
  cache_key.req_id = UINT64_MAX;
  cache_keys.emplace_back(cache_key);
  EXPECT_EQ(llm_datadist.RegisterCache(cache_desc, cache, cache_keys), ge::SUCCESS);

  std::map<uint64_t, uint32_t> cluster2rank{{1, 0}, {2, 1}};
  std::string rank_table;
  uint64_t comm_id;
  std::string cluster_name = "link";
  EXPECT_EQ(llm_datadist.Link(cluster_name, cluster2rank, rank_table, comm_id), ge::SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  llm_datadist.LLMDataDistFinalize();
}

TEST_F(LLMCommLinkManagerUTest, LinkMultipleComm) {
  MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpa>());
  LLMDataDistV2 llm_datadist(1U);
  std::map<ge::AscendString, ge::AscendString> options{};
  options["llm.Role"] = "Decoder";
  EXPECT_EQ(llm_datadist.LLMDataDistInitialize(options), ge::SUCCESS);

  CacheDesc cache_desc{};
  cache_desc.num_tensors = 1U;
  cache_desc.data_type = ge::DT_FLOAT;
  cache_desc.shape = {2, 3};
  cache_desc.placement = 1U;
  Cache cache{};
  (void)cache.per_device_tensor_addrs.emplace_back(std::vector<uint64_t>{0x8001U});
  std::vector<CacheKey> cache_keys = {};
  CacheKey cache_key{};
  cache_key.is_allocate_blocks = true;
  cache_key.model_id = 0;
  cache_key.req_id = UINT64_MAX;
  cache_keys.emplace_back(cache_key);
  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(llm_datadist.RegisterCache(cache_desc, cache, cache_keys), ge::SUCCESS);
  }

  std::map<uint64_t, uint32_t> cluster2rank{{1, 0}, {2, 1}};
  std::string rank_table;
  uint64_t comm_id;
  std::string cluster_name = "link";
  EXPECT_EQ(llm_datadist.Link(cluster_name, cluster2rank, rank_table, comm_id), ge::SUCCESS);

  std::map<uint64_t, uint32_t> cluster2rank2{{1, 0}, {3, 1}};
  std::string rank_table2;
  uint64_t comm_id2;
  std::string cluster_name2 = "link2";
  EXPECT_EQ(llm_datadist.Link(cluster_name2, cluster2rank2, rank_table2, comm_id2), ge::SUCCESS);

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  RegisterMemoryStatus status;
  EXPECT_EQ(llm_datadist.QueryRegisterMemStatus(comm_id, status), ge::SUCCESS);
  EXPECT_EQ(status, RegisterMemoryStatus::OK);

  RegisterMemoryStatus status2;
  EXPECT_EQ(llm_datadist.QueryRegisterMemStatus(comm_id2, status2), ge::SUCCESS);
  EXPECT_EQ(status2, RegisterMemoryStatus::OK);

  llm_datadist.LLMDataDistFinalize();
}

TEST_F(LLMCommLinkManagerUTest, RemapRegisteredMemorySuc) {
  MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpa>());
  LLMDataDistV2 llm_datadist(1U);
  std::map<ge::AscendString, ge::AscendString> options{};
  options["llm.Role"] = "Decoder";
  EXPECT_EQ(llm_datadist.LLMDataDistInitialize(options), ge::SUCCESS);

  std::map<uint64_t, uint32_t> cluster2rank{{1, 0}, {2, 1}};
  std::string rank_table;
  uint64_t comm_id;
  std::string cluster_name = "link";
  EXPECT_EQ(llm_datadist.Link(cluster_name, cluster2rank, rank_table, comm_id), ge::SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  RegisterMemoryStatus status;
  EXPECT_EQ(llm_datadist.QueryRegisterMemStatus(comm_id, status), ge::SUCCESS);
  EXPECT_EQ(status, RegisterMemoryStatus::OK);

  LLMMemInfo mem_info{};
  EXPECT_EQ(llm_datadist.RemapRegisteredMemory({mem_info}), ge::SUCCESS);
  llm_datadist.LLMDataDistFinalize();
}
}  // namespace llm
