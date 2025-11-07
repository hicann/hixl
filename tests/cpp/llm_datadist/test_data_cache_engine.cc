/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <memory>
#include <algorithm>
#include <vector>
#include <cstdlib>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "llm_datadist/llm_error_codes.h"
#include "llm_datadist_v2.h"

#include "common/llm_inner_types.h"
#include "llm_datadist/llm_engine_types.h"

#include "dlog_pub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"
#include "common/llm_mem_pool.h"
#include "rt_error_codes.h"

#include "data_transfer/d2d_data_transfer_job.h"
#include "cache_mgr/data_cache_engine.h"
#include "link_mgr/comm_entity_manager.h"
#include "fsm/send_state.h"
#include "hccl/hccl_adapter.h"
#include "depends/llm_datadist/src/hccl_stub.h"

using namespace std;
using namespace ge;
using namespace ::testing;
using ::testing::Invoke;
using ::testing::Mock;

RTS_API rtError_t rtEventQueryStatus(rtEvent_t evt, rtEventStatus_t *status) {
  (void) evt;
  static int32_t i = 0;
  bool success = (++i % 2) == 0;
  *status = success ? RT_EVENT_RECORDED : RT_EVENT_INIT;
  LLMLOGI("Wait event ret = %d", success);
  return RT_ERROR_NONE;
}

namespace llm {
namespace {
class DataCacheEngineRunner {
 public:
  DataCacheEngineRunner() = default;
  ~DataCacheEngineRunner() = default;
  void LlmDatadistInitAndLink(const llm::CacheDesc &src_cache_desc, const llm::CacheDesc &dst_cache_desc,
                              const llm::PullCacheParam &pull_cache_param, bool use_host_mem_pool = false,
                              bool enable_remote_cache_accessible = false) {
    std::map<ge::AscendString, ge::AscendString> default_options{
        {llm::LLM_OPTION_ROLE, llm::kDecoder},
        {OPTION_EXEC_DEVICE_ID, "0"},
        {llm::LLM_OPTION_SYNC_KV_CACHE_WAIT_TIME, "600000"},
        {llm::LLM_OPTION_MEM_POOL_CONFIG, "{\"memory_size\": 1024288000}"}};
    if (use_host_mem_pool) {
      default_options[llm::LLM_OPTION_HOST_MEM_POOL_CONFIG] = "{\"memory_size\": 102428800}";
    }
    if (enable_remote_cache_accessible) {
      default_options[llm::kLlmOptionEnableRemoteCacheAccessible] = "1";
    }
    EXPECT_EQ(llm_data_dist_.LLMDataDistInitialize(default_options), ge::SUCCESS);
    EXPECT_EQ(llm_data_dist_.CheckCapacity(1024), ge::SUCCESS);

    // allocate cache
    if (!pull_cache_param.decoder_blocks.empty()) {
      dst_cache_keys_.resize(1);
      dst_cache_keys_.front().is_allocate_blocks = true;
      dst_cache_keys_.front().req_id = 1;
    }
    if (!pull_cache_param.prompt_blocks.empty()) {
      src_cache_keys_.resize(1);
      src_cache_keys_.front().is_allocate_blocks = true;
    }
    uint32_t host_buffers_size = 0U;
    if (src_cache_desc.placement == 0) {
      host_buffers_size = src_cache_desc.num_tensors;
    }
    if (dst_cache_desc.placement == 0) {
      host_buffers_size = dst_cache_desc.num_tensors;
    }
    host_buffers_.resize(host_buffers_size, std::vector<int32_t>(8 * 1024 * 1024));

    if (src_cache_desc.placement == 1) {
      ASSERT_EQ(llm_data_dist_.AllocateCache(src_cache_desc, src_cache_, src_cache_keys_), SUCCESS);
    } else {
      src_cache_.per_device_tensor_addrs.resize(1);
      for (auto &host_buffer : host_buffers_) {
        src_cache_.per_device_tensor_addrs[0].emplace_back(reinterpret_cast<uintptr_t>(&host_buffer[0]));
      }
      ASSERT_EQ(llm_data_dist_.RegisterCache(src_cache_desc, src_cache_, src_cache_keys_), SUCCESS);
    }
    if (dst_cache_desc.placement == 1) {
      ASSERT_EQ(llm_data_dist_.AllocateCache(dst_cache_desc, dst_cache_, dst_cache_keys_), SUCCESS);
    } else {
      if (use_host_mem_pool) {
        ASSERT_EQ(llm_data_dist_.AllocateCache(dst_cache_desc, dst_cache_, dst_cache_keys_), SUCCESS);
      } else {
        dst_cache_.per_device_tensor_addrs.resize(1);
        for (auto &host_buffer : host_buffers_) {
          dst_cache_.per_device_tensor_addrs[0].emplace_back(reinterpret_cast<uintptr_t>(&host_buffer[0]));
        }
        ASSERT_EQ(llm_data_dist_.RegisterCache(dst_cache_desc, dst_cache_, dst_cache_keys_), SUCCESS);
      }
    }

    const std::map<uint64_t, uint32_t> cluster_to_rank{{0, 0}, {1, 1}};
    std::string rank_table = "01";
    std::string cluster_name = "link";
    EXPECT_EQ(llm_data_dist_.Link(cluster_name, cluster_to_rank, rank_table, comm_id_), ge::SUCCESS);

    std::vector<int32_t> tensor_data{1, 2, 3, 4};
    memcpy((void *)src_cache_.per_device_tensor_addrs[0][0], tensor_data.data(), tensor_data.size() * sizeof(int32_t));

    llm::RegisterMemoryStatus status = llm::RegisterMemoryStatus::PREPARING;
    while (status != llm::RegisterMemoryStatus::OK) {
      EXPECT_EQ(llm_data_dist_.QueryRegisterMemStatus(comm_id_, status), ge::SUCCESS);
    }
  }

  void ReleaseResource() {
    EXPECT_EQ(llm_data_dist_.DeallocateCache(src_cache_.cache_id), ge::SUCCESS);
    EXPECT_EQ(llm_data_dist_.DeallocateCache(dst_cache_.cache_id), ge::SUCCESS);
    for (const auto src_cache_key : src_cache_keys_) {
      EXPECT_EQ(llm_data_dist_.RemoveCacheKey(src_cache_key), ge::SUCCESS);
    }
    for (const auto dst_cache_key : dst_cache_keys_) {
      EXPECT_EQ(llm_data_dist_.RemoveCacheKey(dst_cache_key), ge::SUCCESS);
    }

    EXPECT_EQ(llm_data_dist_.Unlink(comm_id_), ge::SUCCESS);
    llm_data_dist_.LLMDataDistFinalize();
  }

  void PullDataCache(const llm::PullCacheParam &pull_cache_param, std::vector<int32_t> &pull_result) {
    llm::CacheKey cache_key{};
    cache_key.prompt_cluster_id = 0;
    cache_key.prompt_cache_id = 1;

    EXPECT_EQ(llm_data_dist_.PullCache(dst_cache_.cache_id, cache_key, pull_cache_param), SUCCESS);
    auto pulled_data = reinterpret_cast<int32_t *>(dst_cache_.per_device_tensor_addrs[0][0]);
    memcpy(pull_result.data(), pulled_data, sizeof(int32_t) * pull_result.size());
  }

  void TransferDataCache(const llm::TransferCacheConfig &transfer_cache_config,
                         const llm::TransferBlockConfig &transfer_block_config, std::vector<int32_t> &pull_result) {
    EXPECT_EQ(llm_data_dist_.TransferCache(0, transfer_cache_config, transfer_block_config), SUCCESS);
    LLMLOGI("dst_addrs 0 addrs:%p", transfer_cache_config.dst_addrs[0]);
    auto pulled_data = reinterpret_cast<int32_t *>(transfer_cache_config.dst_addrs[0]);
    memcpy(pull_result.data(), pulled_data, sizeof(int32_t) * pull_result.size());
  }

  llm::Cache &GetSrcCache() {
    return src_cache_;
  }

  llm::Cache &GetDstCache() {
    return dst_cache_;
  }

  uint64_t GetCommId() const {
    return comm_id_;
  }

  llm::LLMDataDistV2 &GetLlmDataDist() {
    return llm_data_dist_;
  }

 private:
  llm::LLMDataDistV2 llm_data_dist_{llm::LLMDataDistV2(1)};
  uint64_t comm_id_{0U};
  llm::Cache src_cache_;
  llm::Cache dst_cache_;
  std::vector<llm::CacheKey> src_cache_keys_;
  std::vector<llm::CacheKey> dst_cache_keys_;
  std::vector<std::vector<int32_t>> host_buffers_;
};

HcclResult HcclExchangeMemDesc1(HcclComm comm, uint32_t remoteRank, HcclMemDescs *local, int timeout,
                                HcclMemDescs *remote, uint32_t *actualNum) {
  for (uint32_t i = 0U; i < local->arrayLength; ++i) {
    strcpy(remote->array[i].desc, local->array[i].desc);
  }
  *actualNum = local->arrayLength;
  remote->arrayLength = local->arrayLength;
  return HcclResult::HCCL_SUCCESS;
}

class MockMmpaLongTimeRegister : public MmpaStubApiGe {
 public:
  void *DlOpen(const char *file_name, int32_t mode) override {
    return (void *) 0x10000000;
  }

  void *DlSym(void *handle, const char *func_name) override {
    static const std::map<std::string, void*> func_map = {
        {"HcclCommInitClusterInfoMemConfig", reinterpret_cast<void*>(&HcclCommInitClusterInfoMemConfig)},
        {"HcclExchangeMemDesc", reinterpret_cast<void*>(&HcclExchangeMemDesc1)},
        {"HcclCommDestroy", reinterpret_cast<void*>(&HcclCommDestroy)},
        {"HcclBatchPut", reinterpret_cast<void*>(&HcclBatchPut)},
        {"HcclBatchGet", reinterpret_cast<void*>(&HcclBatchGet)},
        {"HcclRemapRegistedMemory", reinterpret_cast<void*>(&HcclRemapRegistedMemory)},
        {"HcclRegisterGlobalMem", reinterpret_cast<void*>(&HcclRegisterGlobalMem)},
        {"HcclDeregisterGlobalMem", reinterpret_cast<void*>(&HcclDeregisterGlobalMem)},
        {"HcclCommBindMem", reinterpret_cast<void*>(&HcclCommBindMem)},
        {"HcclCommUnbindMem", reinterpret_cast<void*>(&HcclCommUnbindMem)},
        {"HcclCommPrepare", reinterpret_cast<void*>(&HcclCommPrepare)},
    };
    auto it = func_map.find(func_name);
    if (it != func_map.end()) {
      LLMLOGI("%s addr:%lu", func_name, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(it->second)));
      return it->second;
    }
    return nullptr;
  }

  int32_t DlClose(void *handle) override {
    return 0;
  }
};

class MockRuntime : public llm::RuntimeStub {
 public:
  rtError_t rtStreamSynchronizeWithTimeout(rtStream_t stm, int32_t timeout) override {
    return count_++ == 1 ? ACL_ERROR_RT_STREAM_SYNC_TIMEOUT : RT_ERROR_NONE;
  }

  int32_t count_ = 0;
};
}  // namespace

class DataCacheEngineSTest : public ::testing::Test {
 protected:
  // 在测试类中设置一些准备工作，如果需要的话
  void SetUp() override {
    llm::MockMmpaForHcclApi::Install();
  }
  // 在测试类中进行清理工作，如果需要的话
  void TearDown() override {
    llm::HcclAdapter::GetInstance().Finalize();
    llm::MockMmpaForHcclApi::Reset();
  }
};

TEST_F(DataCacheEngineSTest, UnlinkWhenPrepareNotFinished) {
  MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpaLongTimeRegister>());
  llm::LLMDataDistV2 llm_datadist(1U);
  std::map<ge::AscendString, ge::AscendString> options{};
  options["llm.Role"] = "Decoder";
  EXPECT_EQ(llm_datadist.LLMDataDistInitialize(options), ge::SUCCESS);

  std::map<uint64_t, uint32_t> cluster2rank{{1, 0}, {2, 1}};
  std::string rank_table;
  uint64_t comm_id;
  std::string cluster_name = "link";
  EXPECT_EQ(llm_datadist.Link(cluster_name, cluster2rank, rank_table, comm_id), ge::SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_EQ(llm_datadist.Unlink(comm_id), ge::SUCCESS);
  llm_datadist.LLMDataDistFinalize();
}

TEST_F(DataCacheEngineSTest, TestMemPool) {
  void *base_address_ = (void *) 0x1000000000UL;
  size_t pool_size = 10UL * 64 * 1024;
  llm::ScalableConfig config{};
  config.page_idem_num = 16;
  config.page_mem_size_total_threshold = pool_size;
  llm::LlmMemPool mem_pool(config);
  ASSERT_EQ(mem_pool.Initialize(base_address_, pool_size), ge::SUCCESS);
  std::vector<void *> addrs;
  for (int i = 0; i < 10; ++i) {
    auto addr = mem_pool.Alloc(32 * 1024);
    EXPECT_TRUE(addr != nullptr);
    addrs.emplace_back(addr);
  }
  EXPECT_TRUE(mem_pool.Alloc(32 * 1024, 1000) == nullptr);
  std::thread free_th([&mem_pool, &addrs]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (auto addr : addrs) {
      mem_pool.Free(addr);
    }
  });
  for (int i = 0; i < 10; ++i) {
    LLMLOGI("TEST--- allocate index = %d, start", i);
    auto addr = mem_pool.Alloc(32 * 1024, 3000);
    LLMLOGI("TEST--- allocate index = %d, ret = %p", i, addr);
    ASSERT_TRUE(addr != nullptr);
  }
  free_th.join();
}

TEST_F(DataCacheEngineSTest, TestMemPool_Shared) {
  void *base_address_ = (void *) 0x1000000000UL;
  size_t pool_size = 10UL * 64 * 1024;
  llm::ScalableConfig config{};
  config.page_idem_num = 16;
  config.page_mem_size_total_threshold = pool_size;
  llm::LlmMemPool mem_pool(config);
  ASSERT_EQ(mem_pool.Initialize(base_address_, pool_size), ge::SUCCESS);
  std::vector<std::shared_ptr<void>> addrs;
  for (int i = 0; i < 10; ++i) {
    auto addr = mem_pool.AllocShared(32 * 1024);
    EXPECT_TRUE(addr != nullptr);
    addrs.emplace_back(addr);
  }
  addrs.clear();
}

TEST_F(DataCacheEngineSTest, PullDataCache_D2H_C2C) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {4, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  src_cache_desc.shape = {2, 128};
  dst_cache_desc.placement = 0;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = 128 * 4;
  pull_cache_param.batch_index = 1;

  std::vector<int32_t> pull_result(2 * 128);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, PullDataCache_D2H_B2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 0;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0,1,4,5,6};
  pull_cache_param.decoder_blocks = {1,2,4,6,9};

  std::vector<int32_t> pull_result(128 * 128);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, PullCache_D2H_B2B_BigBlock) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 1;
  src_cache_desc.shape = {1, 10L * 1024 * 1024};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 0;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks.resize(32);
  pull_cache_param.decoder_blocks.resize(32);

  llm::MockMmpaForHcclApi::Install();
  llm::RuntimeStub::SetInstance(std::make_shared<llm::DataCacheEngineRuntimeMock>());
  llm::HcclAdapter::GetInstance().Initialize();
  llm::DataCacheEngineTestRunner test_runner(100 * 1024 * 1024);
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  EXPECT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);
}

TEST_F(DataCacheEngineSTest, PullDataCache_D2H_C2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;

  auto dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 2};
  dst_cache_desc.placement = 0;
  dst_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.decoder_blocks = {1, 3, 5, 7};

  std::vector<int32_t> pull_result(4 * 32);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[0], &pull_result[8]);
  EXPECT_EQ(actual, (std::vector<int32_t>{0, 0, 1, 2, 0, 0, 3, 4}));
}


TEST_F(DataCacheEngineSTest, PullDataCache_H2D_C2C) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {2, 256 * 1024 * 3};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 0;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {2, 256 * 1024 * 4};
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = 1024 * 1024 * 3;
  pull_cache_param.batch_index = 1;

  std::vector<int32_t> pull_result(2 * 256 * 1024 * 4);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[256 * 1024 * 4], &pull_result[256 * 1024 * 4 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, PullDataCache_H2D_B2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 0;
  src_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  for (int i = 0; i < 64; ++i) {
    pull_cache_param.prompt_blocks.push_back(i);
    pull_cache_param.decoder_blocks.push_back(i);
  }
  std::reverse(pull_cache_param.decoder_blocks.begin(), pull_cache_param.decoder_blocks.end());

  std::vector<int32_t> pull_result(128 * 128);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[128 * 63], &pull_result[128 * 63 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, PullDataCache_D2D_C2C) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {4, 128};

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = 128 * 4;
  pull_cache_param.batch_index = 1;
  std::vector<int32_t> pull_result(4 * 128);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

void PullCacheThread(llm::LLMDataDistV2 &llm_data_dist, llm::CacheKey &cache_key, llm::Cache &dst_cache,
                     llm::PullCacheParam &pull_cache_param) {
  auto ret = llm_data_dist.PullCache(dst_cache.cache_id, cache_key, pull_cache_param);
  EXPECT_TRUE(ret == ge::SUCCESS || ret == ge::LLM_NOT_YET_LINK);
}

void UnlinkThread(llm::LLMDataDistV2 &llm_data_dist, uint64_t comm_id) {
  std::this_thread::sleep_for(std::chrono::microseconds(100));
  EXPECT_EQ(llm_data_dist.Unlink(comm_id), ge::SUCCESS);
}

TEST_F(DataCacheEngineSTest, PullDataCache_D2D_C2C_sync_with_unlink) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {4, 128};

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = 128 * 4;
  pull_cache_param.batch_index = 1;
  std::vector<int32_t> pull_result(4 * 128);
  llm::Cache dst_cache;
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  llm::LLMDataDistV2 &llm_data_dist = data_cache_engine_runner.GetLlmDataDist();
  llm::CacheKey cache_key{};
  cache_key.prompt_cluster_id = 0;
  cache_key.prompt_cache_id = 1;
  std::thread thread2(UnlinkThread, std::ref(llm_data_dist), data_cache_engine_runner.GetCommId());
  std::thread thread1(PullCacheThread, std::ref(llm_data_dist), std::ref(cache_key),
                      std::ref(data_cache_engine_runner.GetDstCache()), std::ref(pull_cache_param));
  if (thread1.joinable()) {
    thread1.join();
  }
  if (thread2.joinable()) {
    thread2.join();
  }
  llm_data_dist.LLMDataDistFinalize();
}

TEST_F(DataCacheEngineSTest, PullDataCache_D2D_B2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0,1,4,5,6};
  pull_cache_param.decoder_blocks = {1,2,4,6,9};

  std::vector<int32_t> pull_result(128 * 128);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, PullCache_D2D_B2B_BatchGet) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0,1,4,5,6};
  pull_cache_param.decoder_blocks = {1,2,4,6,9};

  std::vector<int32_t> pull_result(128 * 128);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param, false, true);

  llm::CacheKey cache_key{};
  cache_key.prompt_cluster_id = 0;
  cache_key.prompt_cache_id = 1;

  llm::RuntimeStub::SetInstance(std::make_shared<MockRuntime>());
  EXPECT_EQ(data_cache_engine_runner.GetLlmDataDist().PullCache(data_cache_engine_runner.GetDstCache().cache_id,
                                                                cache_key,
                                                                pull_cache_param), ge::LLM_TIMEOUT);
  llm::RuntimeStub::Reset();
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, PullDataCache_D2D_C2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;

  auto dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 2};
  dst_cache_desc.placement = 1;
  dst_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.decoder_blocks = {1, 3, 5, 7};

  std::vector<int32_t> pull_result(64 * 2);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[0], &pull_result[8]);
  EXPECT_EQ(actual, (std::vector<int32_t>{0, 0, 1, 2, 0, 0, 3, 4}));
}

TEST_F(DataCacheEngineSTest, PullDataCache_D2D_C2B_with_remaider) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 7};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;

  auto dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 2};
  dst_cache_desc.placement = 1;
  dst_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.decoder_blocks = {1, 3, 5, 7};

  std::vector<int32_t> pull_result(64 * 2);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[0], &pull_result[8]);
  EXPECT_EQ(actual, (std::vector<int32_t>{0, 0, 1, 2, 0, 0, 3, 4}));
}

TEST_F(DataCacheEngineSTest, CopyCache_C2C_Big) {
  llm::CommEntityManager comm_entity_manager;
  llm::CommMemManager comm_mem_manager;
  llm::DataCacheEngine cache_engine;
  cache_engine.SetCommEntityManager(&comm_entity_manager);
  cache_engine.SetCommMemManager(&comm_mem_manager);
  llm::CacheManager cache_manager;
  cache_engine.SetCacheManager(&cache_manager);
  std::map<ge::AscendString, ge::AscendString> options;
  // 4 * 64K
  options[llm::LLM_OPTION_MEM_POOL_CONFIG] = "{\"memory_size\": 10000000}";
  EXPECT_EQ(cache_engine.Initialize(options), ge::SUCCESS);

  int64_t big_dim = 1024 * 1024 + 128;
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 1;
  src_cache_desc.placement = 1;
  src_cache_desc.shape = {1, big_dim};
  src_cache_desc.data_type = ge::DT_INT32;

  llm::CacheDesc dst_cache_desc = src_cache_desc;

  llm::Cache src_cache;
  llm::Cache dst_cache;
  EXPECT_EQ(cache_engine.Allocate(src_cache_desc, {}, src_cache), ge::SUCCESS);
  EXPECT_EQ(cache_engine.Allocate(dst_cache_desc, {}, dst_cache), ge::SUCCESS);

  llm::CopyCacheParam copy_cache_param{};
  copy_cache_param.src_cache_id = src_cache.cache_id;
  copy_cache_param.dst_cache_id = dst_cache.cache_id;

  auto src_tensor_base = reinterpret_cast<int32_t *>(src_cache.per_device_tensor_addrs[0][0]);
  auto dst_tensor_base = reinterpret_cast<int32_t *>(dst_cache.per_device_tensor_addrs[0][0]);
  std::iota(src_tensor_base, src_tensor_base + big_dim, 0);
  EXPECT_EQ(cache_engine.CopyCache(copy_cache_param), ge::SUCCESS);
  EXPECT_EQ(memcmp(src_tensor_base, dst_tensor_base, 4 * big_dim), 0);
  cache_manager.Finalize();
  cache_engine.Finalize();
}

TEST_F(DataCacheEngineSTest, CopyCache_B2B) {
  llm::CommEntityManager comm_entity_manager;
  llm::CommMemManager comm_mem_manager;
  llm::DataCacheEngine cache_engine;
  cache_engine.SetCommEntityManager(&comm_entity_manager);
  cache_engine.SetCommMemManager(&comm_mem_manager);
  llm::CacheManager cache_manager;
  cache_engine.SetCacheManager(&cache_manager);
  std::map<ge::AscendString, ge::AscendString> options;
  // 4 * 64K
  options[llm::LLM_OPTION_MEM_POOL_CONFIG] = "{\"memory_size\": 10000000}";
  EXPECT_EQ(cache_engine.Initialize(options), ge::SUCCESS);

  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 4;
  src_cache_desc.placement = 1;
  src_cache_desc.shape = {128, 4};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 4};

  llm::Cache src_cache;
  llm::Cache dst_cache;
  llm::CacheKey src_cache_key{};
  src_cache_key.is_allocate_blocks = true;
  llm::CacheKey dst_cache_key = src_cache_key;
  dst_cache_key.req_id = 1;

  EXPECT_EQ(cache_engine.Allocate(src_cache_desc, {src_cache_key}, src_cache), ge::SUCCESS);
  EXPECT_EQ(cache_engine.Allocate(dst_cache_desc, {dst_cache_key}, dst_cache), ge::SUCCESS);

  llm::CopyCacheParam copy_cache_param{};
  copy_cache_param.src_cache_id = src_cache.cache_id;
  copy_cache_param.dst_cache_id = dst_cache.cache_id;
  copy_cache_param.copy_block_infos.emplace_back(2, 1);
  copy_cache_param.copy_block_infos.emplace_back(3, 2);
  copy_cache_param.copy_block_infos.emplace_back(4, 0);
  copy_cache_param.copy_block_infos.emplace_back(0, 3);

  auto src_tensor_base = reinterpret_cast<int32_t *>(src_cache.per_device_tensor_addrs[0][0]);
  auto dst_tensor_base = reinterpret_cast<int32_t *>(dst_cache.per_device_tensor_addrs[0][0]);
  std::iota(src_tensor_base, src_tensor_base + 4 * 128, 0);
  EXPECT_EQ(cache_engine.CopyCache(copy_cache_param), ge::SUCCESS);
  std::vector<int32_t> expected{
      16, 17, 18, 19,
      8, 9, 10, 11,
      12, 13, 14, 15,
      0, 1, 2, 3
  };
  std::vector<int32_t> dst_value(dst_tensor_base, dst_tensor_base + 4 * 4);
  EXPECT_EQ(dst_value, expected);
  cache_manager.Finalize();
  cache_engine.Finalize();
}

TEST_F(DataCacheEngineSTest, AllocateOutOfMemory) {
  llm::HcclAdapter::GetInstance().Finalize();
  llm::LLMDataDistV2 llm_data_dist(1);
  std::map<ge::AscendString, ge::AscendString> default_options{
      {llm::LLM_OPTION_ROLE, llm::kDecoder},
      {ge::OPTION_EXEC_DEVICE_ID, "0"},
      {llm::LLM_OPTION_SYNC_KV_CACHE_WAIT_TIME, "600000"},
      {llm::LLM_OPTION_MEM_POOL_CONFIG, "{\"memory_size\": 65536}"}
  };

  EXPECT_EQ(llm_data_dist.LLMDataDistInitialize(default_options), ge::SUCCESS);
  llm::Cache cache;
  llm::CacheDesc cache_desc{};
  cache_desc.num_tensors = 2;
  cache_desc.shape = {4, 128};
  cache_desc.data_type = ge::DT_INT32;
  cache_desc.placement = 1;
  std::vector<llm::CacheKey> cache_keys;
  EXPECT_EQ(llm_data_dist.AllocateCache(cache_desc, cache, cache_keys), ge::LLM_OUT_OF_MEMORY);
  llm_data_dist.LLMDataDistFinalize();
}

TEST_F(DataCacheEngineSTest, TransferDataCache_D2D_C2C) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {4, 128};

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = 128 * 4;
  pull_cache_param.batch_index = 1;
  std::vector<int32_t> pull_result(4 * 128);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  const auto &src_cache = data_cache_engine_runner.GetSrcCache();
  const auto &dst_cache = data_cache_engine_runner.GetDstCache();
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  transfer_cache_config.layer_index = 0;
  uint64_t layer_index = 2U;
  transfer_cache_config.dst_addrs =
      std::vector<uintptr_t>(dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2,
                             dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2 + 2);
  llm::TransferBlockConfig transfer_block_config{};
  data_cache_engine_runner.TransferDataCache(transfer_cache_config, transfer_block_config, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[0], &pull_result[4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, TransferDataCache_D2D_B2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0, 1, 4, 5, 6};
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9};

  std::vector<int32_t> pull_result(128 * 128);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  const auto &src_cache = data_cache_engine_runner.GetSrcCache();
  const auto &dst_cache = data_cache_engine_runner.GetDstCache();
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  transfer_cache_config.layer_index = 0;
  uint64_t layer_index = 2U;
  transfer_cache_config.dst_addrs =
      std::vector<uintptr_t>(dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2,
                             dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2 + 2);
  llm::TransferBlockConfig transfer_block_config{};
  transfer_block_config.src_blocks = {0, 1, 4, 5, 6};
  transfer_block_config.dst_blocks = {1, 2, 4, 6, 9};
  data_cache_engine_runner.TransferDataCache(transfer_cache_config, transfer_block_config, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, TransferDataCache_D2D_C2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;

  auto dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 2};
  dst_cache_desc.placement = 1;
  dst_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.decoder_blocks = {1, 3, 5, 7};

  std::vector<int32_t> pull_result(64 * 2);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  const auto &src_cache = data_cache_engine_runner.GetSrcCache();
  const auto &dst_cache = data_cache_engine_runner.GetDstCache();
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  transfer_cache_config.layer_index = 0;
  uint64_t layer_index = 1U;
  transfer_cache_config.dst_addrs =
      std::vector<uintptr_t>(dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2,
                             dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2 + 2);
  llm::TransferBlockConfig transfer_block_config{};
  transfer_block_config.block_mem_size = 8;
  transfer_block_config.dst_blocks = {1, 3, 5, 7};
  data_cache_engine_runner.TransferDataCache(transfer_cache_config, transfer_block_config, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[0], &pull_result[8]);
  EXPECT_EQ(actual, (std::vector<int32_t>{0, 0, 1, 2, 0, 0, 3, 4}));
}

TEST_F(DataCacheEngineSTest, TransferDataCache_D2D_C2B_remainder) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 7};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;

  auto dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 2};
  dst_cache_desc.placement = 1;
  dst_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.decoder_blocks = {1, 3, 5, 7};

  std::vector<int32_t> pull_result(64 * 2);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  const auto &src_cache = data_cache_engine_runner.GetSrcCache();
  const auto &dst_cache = data_cache_engine_runner.GetDstCache();
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  transfer_cache_config.layer_index = 0;
  uint64_t layer_index = 1U;
  transfer_cache_config.dst_addrs =
      std::vector<uintptr_t>(dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2,
                             dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2 + 2);
  llm::TransferBlockConfig transfer_block_config{};
  transfer_block_config.block_mem_size = 8;
  transfer_block_config.dst_blocks = {1, 3, 5, 7};
  data_cache_engine_runner.TransferDataCache(transfer_cache_config, transfer_block_config, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[0], &pull_result[8]);
  EXPECT_EQ(actual, (std::vector<int32_t>{0, 0, 1, 2, 0, 0, 3, 4}));
}

TEST_F(DataCacheEngineSTest, TransferDataCache_D2H_C2C) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {4, 128};
  dst_cache_desc.placement = 0;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = 128 * 4;
  pull_cache_param.batch_index = 1;
  std::vector<int32_t> pull_result(4 * 128);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param, true);
  const auto &src_cache = data_cache_engine_runner.GetSrcCache();
  const auto &dst_cache = data_cache_engine_runner.GetDstCache();
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  transfer_cache_config.layer_index = 0;
  uint64_t layer_index = 2U;
  transfer_cache_config.dst_addrs =
      std::vector<uintptr_t>(dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2,
                             dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2 + 2);
  llm::TransferBlockConfig transfer_block_config{};
  data_cache_engine_runner.TransferDataCache(transfer_cache_config, transfer_block_config, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[0], &pull_result[4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, TransferDataCache_D2H_B2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 0;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0,1,4,5,6};
  pull_cache_param.decoder_blocks = {1,2,4,6,9};

  std::vector<int32_t> pull_result(128 * 128);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param, true);
  const auto &src_cache = data_cache_engine_runner.GetSrcCache();
  const auto &dst_cache = data_cache_engine_runner.GetDstCache();
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  transfer_cache_config.layer_index = 0;
  uint64_t layer_index = 2U;
  transfer_cache_config.dst_addrs =
      std::vector<uintptr_t>(dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2,
                             dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2 + 2);
  llm::TransferBlockConfig transfer_block_config{};
  transfer_block_config.src_blocks = {0, 1, 4, 5, 6};
  transfer_block_config.dst_blocks = {1, 2, 4, 6, 9};
  data_cache_engine_runner.TransferDataCache(transfer_cache_config, transfer_block_config, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, TransferDataCache_D2H_C2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;

  auto dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 2};
  dst_cache_desc.placement = 0;
  dst_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.decoder_blocks = {1, 3, 5, 7};

  std::vector<int32_t> pull_result(4 * 32);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  const auto &src_cache = data_cache_engine_runner.GetSrcCache();
  const auto &dst_cache = data_cache_engine_runner.GetDstCache();
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  transfer_cache_config.layer_index = 0;
  uint64_t layer_index = 1U;
  transfer_cache_config.dst_addrs =
      std::vector<uintptr_t>(dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2,
                             dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2 + 2);
  llm::TransferBlockConfig transfer_block_config{};
  transfer_block_config.block_mem_size = 8;
  transfer_block_config.dst_blocks = {1, 3, 5, 7};
  data_cache_engine_runner.TransferDataCache(transfer_cache_config, transfer_block_config, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[0], &pull_result[8]);
  EXPECT_EQ(actual, (std::vector<int32_t>{0, 0, 1, 2, 0, 0, 3, 4}));
}

TEST_F(DataCacheEngineSTest, PullDataCache_D2D_C2C_with_layer_range) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {4, 128};

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = 128 * 4;
  pull_cache_param.batch_index = 1;
  pull_cache_param.src_tensor_indices = {0, 1, 2, 3};
  pull_cache_param.dst_tensor_indices = {0, 1, 2, 3};
  std::vector<int32_t> pull_result(4 * 128);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, PullDataCache_D2D_B2B_with_layer_range) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;
  dst_cache_desc.num_tensors = 4;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0,1,4,5,6};
  pull_cache_param.decoder_blocks = {1,2,4,6,9};
  pull_cache_param.src_tensor_indices = {0, 1, 2, 3};

  std::vector<int32_t> pull_result(128 * 128);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, PullDataCache_D2D_C2B_with_layer_range) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 4;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;

  auto dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 2};
  dst_cache_desc.placement = 1;
  dst_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;
  dst_cache_desc.num_tensors = 8;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.decoder_blocks = {1, 3, 5, 7};
  pull_cache_param.dst_tensor_indices = {0, 1, 2, 3};

  std::vector<int32_t> pull_result(64 * 2);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[0], &pull_result[8]);
  EXPECT_EQ(actual, (std::vector<int32_t>{0, 0, 1, 2, 0, 0, 3, 4}));
}

TEST_F(DataCacheEngineSTest, PullDataCache_H2D_B2B_with_layer_range) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 4;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 0;
  src_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;
  dst_cache_desc.num_tensors = 8;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.dst_tensor_indices = {0, 1, 2, 3};
  for (int i = 0; i < 64; ++i) {
    pull_cache_param.prompt_blocks.push_back(i);
    pull_cache_param.decoder_blocks.push_back(i);
  }
  std::reverse(pull_cache_param.decoder_blocks.begin(), pull_cache_param.decoder_blocks.end());

  std::vector<int32_t> pull_result(128 * 128);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[128 * 63], &pull_result[128 * 63 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, PullDataCache_H2D_C2C_with_layer_range) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {2, 256 * 1024 * 3};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 0;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {2, 256 * 1024 * 4};
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = 1024 * 1024 * 3;
  pull_cache_param.batch_index = 1;
  pull_cache_param.src_tensor_indices = {0, 1, 2, 3};
  pull_cache_param.dst_tensor_indices = {0, 1, 2, 3};

  std::vector<int32_t> pull_result(2 * 256 * 1024 * 4);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[256 * 1024 * 4], &pull_result[256 * 1024 * 4 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, PullDataCache_D2H_C2B_with_layer_range) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;

  auto dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 2};
  dst_cache_desc.placement = 0;
  dst_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;
  dst_cache_desc.num_tensors = 4;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.decoder_blocks = {1, 3, 5, 7};
  pull_cache_param.src_tensor_indices = {0, 1, 2, 3};

  std::vector<int32_t> pull_result(4 * 32);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[0], &pull_result[8]);
  EXPECT_EQ(actual, (std::vector<int32_t>{0, 0, 1, 2, 0, 0, 3, 4}));
}

TEST_F(DataCacheEngineSTest, PullDataCache_D2H_C2C_with_layer_range) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {4, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  src_cache_desc.shape = {2, 128};
  dst_cache_desc.placement = 0;
  dst_cache_desc.num_tensors = 8;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = 128 * 4;
  pull_cache_param.batch_index = 1;
  pull_cache_param.src_tensor_indices = {0, 1, 2, 3};
  pull_cache_param.dst_tensor_indices = {0, 1, 2, 3};

  std::vector<int32_t> pull_result(2 * 128);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, PullDataCache_D2H_B2B_with_layer_range) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 4;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = llm::CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 0;
  dst_cache_desc.num_tensors = 8;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0,1,4,5,6};
  pull_cache_param.decoder_blocks = {1,2,4,6,9};
  pull_cache_param.dst_tensor_indices = {0, 1, 2, 3};

  std::vector<int32_t> pull_result(128 * 128);
  DataCacheEngineRunner data_cache_engine_runner;
  data_cache_engine_runner.LlmDatadistInitAndLink(src_cache_desc, dst_cache_desc, pull_cache_param);
  data_cache_engine_runner.PullDataCache(pull_cache_param, pull_result);
  data_cache_engine_runner.ReleaseResource();

  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineSTest, SwapBlocks) {
  std::map<ge::AscendString, ge::AscendString> default_options{
      {llm::LLM_OPTION_ROLE, llm::kDecoder},
      {OPTION_EXEC_DEVICE_ID, "0"},
      {llm::LLM_OPTION_MEM_POOL_CONFIG, "{\"memory_size\": 102428800}"}};
  llm::LLMDataDistV2 llm_data_dist(1);
  EXPECT_EQ(llm_data_dist.LLMDataDistInitialize(default_options), ge::SUCCESS);

  llm::CacheDesc kv_desc{};
  kv_desc.num_tensors = 10;
  kv_desc.data_type = ge::DT_FLOAT16;
  kv_desc.shape = {10, 128};

  llm::CacheKey cache_key{};
  cache_key.prompt_cluster_id = 0;
  cache_key.req_id = 1;
  cache_key.model_id = 0;

  llm::Cache cached_tensors;
  EXPECT_EQ(llm_data_dist.AllocateCache(kv_desc, cached_tensors, {cache_key}), ge::SUCCESS);
  EXPECT_EQ(cached_tensors.cache_id, 1);
  EXPECT_EQ(cached_tensors.per_device_tensor_addrs.size(), 1U);

  llm::Cache cached_tensors_2;
  EXPECT_EQ(llm_data_dist.AllocateCache(kv_desc, cached_tensors_2), ge::SUCCESS);
  EXPECT_EQ(cached_tensors_2.cache_id, 2);

  std::vector<std::pair<int64_t, int64_t>> block_mapping{{3,4}, {0,0}, {1,1}, {2,2}, {5,6}, {6,7}, {9,9}};
  // swap in
  EXPECT_EQ(llm_data_dist.SwapBlocks(cached_tensors, cached_tensors_2, 128, 0, block_mapping), ge::SUCCESS);
  // swap out
  EXPECT_EQ(llm_data_dist.SwapBlocks(cached_tensors_2, cached_tensors, 128, 1, block_mapping), ge::SUCCESS);

  // test deallocate success
  EXPECT_EQ(llm_data_dist.DeallocateCache(cached_tensors.cache_id), ge::SUCCESS);
  EXPECT_EQ(llm_data_dist.DeallocateCache(cached_tensors_2.cache_id), ge::SUCCESS);
}
}  // namespace llm