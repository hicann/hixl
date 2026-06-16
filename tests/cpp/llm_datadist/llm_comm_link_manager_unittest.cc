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

#include "llm_datadist_v2.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "depends/llm_datadist/src/hccl_stub.h"
#include "depends/llm_datadist/src/hccl_test_helper.h"

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

HcclResult HcclBatchGet1(HcclComm comm, uint32_t remoteRank, HcclOneSideOpDesc *desc, uint32_t descNum,
                         aclrtStream stream) {
  *static_cast<uint64_t *>(desc->localAddr) = UINT64_MAX;
  return HcclResult::HCCL_SUCCESS;
}

uintptr_t mock_handle = 0x8001;

std::map<std::string, void *> GetBaseFuncMap() {
  auto m = GetBaseHcclFuncMap();
  m["HcclBatchGet"] = reinterpret_cast<void *>(&HcclBatchGet1);
  return m;
}

// MockMmpa 基类：提供 DlOpen / DlSym / DlClose 的公共实现。
// 子类只需重写 GetFuncMap() 替换需要注入失败桩的函数指针。
class MockMmpaBase : public MmpaStubApiGe {
 public:
  void *DlOpen(const char *file_name, int32_t mode) override {
    return reinterpret_cast<void *>(mock_handle);
  }

  void *DlSym(void *handle, const char *func_name) override {
    std::map<std::string, void *> func_map = GetFuncMap();
    return LookupFuncByName(func_map, func_name);
  }

  int32_t DlClose(void *handle) override {
    return 0;
  }

 protected:
  virtual std::map<std::string, void *> GetFuncMap() {
    return GetBaseFuncMap();
  }
};

// 默认 Mock：所有 HCCL 函数均使用真实桩（正常成功路径）
class MockMmpa : public MockMmpaBase {};

// 注入 HcclCommBindMemFail 的 Mock
class MockMmpaCommBindMemFail : public MockMmpaBase {
 protected:
  std::map<std::string, void *> GetFuncMap() override {
    auto m = GetBaseFuncMap();
    m["HcclCommBindMem"] = reinterpret_cast<void *>(&HcclCommBindMemFail);
    return m;
  }
};

// 注入 HcclCommPrepareFail 的 Mock
class MockMmpaCommPrepareFail : public MockMmpaBase {
 protected:
  std::map<std::string, void *> GetFuncMap() override {
    auto m = GetBaseFuncMap();
    m["HcclCommPrepare"] = reinterpret_cast<void *>(&HcclCommPrepareFail);
    return m;
  }
};

// 用于长时间注册场景的 Mock（与 MockMmpa 使用相同的函数映射）
class MockMmpaLongTimeRegister : public MockMmpaBase {};
}  // namespace
class LLMCommLinkManagerUTest : public ::testing::Test {
 protected:
  void SetUp() override {}

  void TearDown() override {
    HcclAdapter::GetInstance().Finalize();
  }

  // 初始化 LLMDataDistV2（Decoder 角色），支持可选的额外选项
  void InitDecoder(LLMDataDistV2 &datadist, const std::map<ge::AscendString, ge::AscendString> &extra = {}) {
    std::map<ge::AscendString, ge::AscendString> opts;
    opts["llm.Role"] = "Decoder";
    for (const auto &[k, v] : extra) {
      opts[k] = v;
    }
    EXPECT_EQ(datadist.LLMDataDistInitialize(opts), ge::SUCCESS);
  }

  // 注册一个测试用 Cache，times 控制注册次数
  void RegisterTestCache(LLMDataDistV2 &datadist, int times = 1) {
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
    for (int i = 0; i < times; ++i) {
      EXPECT_EQ(datadist.RegisterCache(cache_desc, cache, cache_keys), ge::SUCCESS);
    }
  }

  // 执行 Link 并返回 comm_id
  uint64_t DoLink(LLMDataDistV2 &datadist, const std::string &cluster_name = "link",
                  const std::map<uint64_t, uint32_t> &cluster2rank = {{1, 0}, {2, 1}}) {
    std::string name = cluster_name;
    std::string rank_table;
    uint64_t comm_id;
    EXPECT_EQ(datadist.Link(name, cluster2rank, rank_table, comm_id), ge::SUCCESS);
    return comm_id;
  }
};

TEST_F(LLMCommLinkManagerUTest, LINK_REGISTER_FAILED_AND_NOUNLINK) {
  MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpaCommBindMemFail>());
  LLMDataDistV2 llm_datadist(1U);
  InitDecoder(llm_datadist, {{"llm.LinkTotalTime", "30"}, {"llm.LinkRetryCount", "2"}});
  RegisterTestCache(llm_datadist);

  uint64_t comm_id = DoLink(llm_datadist);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  RegisterMemoryStatus status;
  EXPECT_EQ(llm_datadist.QueryRegisterMemStatus(comm_id, status), ge::SUCCESS);
  EXPECT_EQ(status, RegisterMemoryStatus::FAILED);
  llm_datadist.LLMDataDistFinalize();
}

TEST_F(LLMCommLinkManagerUTest, LinkCommPrepareFailed) {
  MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpaCommPrepareFail>());
  LLMDataDistV2 llm_datadist(1U);
  InitDecoder(llm_datadist, {{"llm.LinkTotalTime", "10"}, {"llm.LinkRetryCount", "2"}});
  RegisterTestCache(llm_datadist);

  uint64_t comm_id = DoLink(llm_datadist);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  RegisterMemoryStatus status;
  EXPECT_EQ(llm_datadist.QueryRegisterMemStatus(comm_id, status), ge::SUCCESS);
  EXPECT_EQ(status, RegisterMemoryStatus::FAILED);
  llm_datadist.LLMDataDistFinalize();
}

TEST_F(LLMCommLinkManagerUTest, LinkAndUnlinkSuc) {
  MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpa>());
  LLMDataDistV2 llm_datadist(1U);
  InitDecoder(llm_datadist);
  RegisterTestCache(llm_datadist);

  uint64_t comm_id = DoLink(llm_datadist);
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
  InitDecoder(llm_datadist);
  RegisterTestCache(llm_datadist);

  uint64_t comm_id = DoLink(llm_datadist);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_EQ(llm_datadist.Unlink(comm_id), ge::SUCCESS);
  llm_datadist.LLMDataDistFinalize();
}

TEST_F(LLMCommLinkManagerUTest, FinalizeWhenLinkNotFinished) {
  MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpaLongTimeRegister>());
  LLMDataDistV2 llm_datadist(1U);
  InitDecoder(llm_datadist);
  RegisterTestCache(llm_datadist);

  (void)DoLink(llm_datadist);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  llm_datadist.LLMDataDistFinalize();
}

TEST_F(LLMCommLinkManagerUTest, LinkMultipleComm) {
  MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpa>());
  LLMDataDistV2 llm_datadist(1U);
  InitDecoder(llm_datadist);
  RegisterTestCache(llm_datadist, 16);

  uint64_t comm_id = DoLink(llm_datadist);
  uint64_t comm_id2 = DoLink(llm_datadist, "link2", {{1, 0}, {3, 1}});
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
  InitDecoder(llm_datadist);

  uint64_t comm_id = DoLink(llm_datadist);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  RegisterMemoryStatus status;
  EXPECT_EQ(llm_datadist.QueryRegisterMemStatus(comm_id, status), ge::SUCCESS);
  EXPECT_EQ(status, RegisterMemoryStatus::OK);

  LLMMemInfo mem_info{};
  EXPECT_EQ(llm_datadist.RemapRegisteredMemory({mem_info}), ge::SUCCESS);
  llm_datadist.LLMDataDistFinalize();
}

TEST_F(LLMCommLinkManagerUTest, RemapRegisteredMemoryRejectsEmptyInput) {
  MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpa>());
  LLMDataDistV2 llm_datadist(1U);
  InitDecoder(llm_datadist);

  (void)DoLink(llm_datadist);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::vector<LLMMemInfo> empty_mem_infos{};
  EXPECT_NE(llm_datadist.RemapRegisteredMemory(empty_mem_infos), ge::SUCCESS);
  llm_datadist.LLMDataDistFinalize();
}
}  // namespace llm
