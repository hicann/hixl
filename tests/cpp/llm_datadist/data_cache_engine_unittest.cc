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
#include <cstdlib>
#include <gtest/gtest.h>
#include "data_transfer/d2h_data_transfer_job.h"
#include "llm_datadist_v2.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"
#include "depends/runtime/src/runtime_stub.h"
#include "utils/task_batcher.h"
#include "rt_error_codes.h"
#include "llm_datadist/llm_engine_types.h"

namespace llm {
class DataCacheEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    llm::MockMmpaForHcclApi::Install();
    llm::RuntimeStub::SetInstance(std::make_shared<DataCacheEngineRuntimeMock>());
    llm::HcclAdapter::GetInstance().Initialize();
  }
  void TearDown() override {
    llm::RuntimeStub::Reset();
    llm::HcclAdapter::GetInstance().Finalize();
    llm::MockMmpaForHcclApi::Reset();
  }
};

namespace {
class MockRuntime : public llm::RuntimeStub {
 public:
  rtError_t rtStreamSynchronizeWithTimeout(rtStream_t stm, int32_t timeout) override {
    return count_++ == 1 ? ACL_ERROR_RT_STREAM_SYNC_TIMEOUT : RT_ERROR_NONE;
  }

  int32_t count_ = 0;
};
}  // namespace

TEST_F(DataCacheEngineTest, CacheOps) {
  CommEntityManager comm_entity_manager;
  CommMemManager comm_mem_manager;
  DataCacheEngine cache_engine;
  cache_engine.SetCommEntityManager(&comm_entity_manager);
  cache_engine.SetCommMemManager(&comm_mem_manager);
  CacheManager cache_manager;
  cache_engine.SetCacheManager(&cache_manager);
  std::map<ge::AscendString, ge::AscendString> options;
  // 4 * 64K
  options[llm::LLM_OPTION_MEM_POOL_CONFIG] = "{\"memory_size\": 262144}";
  EXPECT_EQ(cache_engine.Initialize(options), ge::SUCCESS);

  CacheDesc cache_desc{};
  cache_desc.num_tensors = 4;
  cache_desc.placement = 1;
  cache_desc.shape = {4, 4096};
  cache_desc.data_type = ge::DT_INT32;
  Cache cache{};
  Cache cache_2{};
  std::vector<CacheKey> cache_keys;
  for (int32_t i = 0; i < 3; ++i) {
    CacheKey cache_key{};
    cache_key.req_id = 1000 + i;
    cache_key.model_id = 1;
    cache_key.prefix_id = UINT64_MAX;
    cache_keys.emplace_back(cache_key);
  }
  cache_keys[1].req_id = UINT64_MAX;
  EXPECT_EQ(cache_engine.CheckCapacity(262144), ge::SUCCESS);
  EXPECT_EQ(cache_engine.Allocate(cache_desc, cache_keys, cache), ge::SUCCESS);
  EXPECT_EQ(cache_engine.CheckCapacity(1), ge::LLM_OUT_OF_MEMORY);
  EXPECT_EQ(cache_engine.Allocate(cache_desc, {}, cache_2), ge::LLM_OUT_OF_MEMORY);

  CacheEntry cache_entry;
  EXPECT_TRUE(cache_manager.GetCacheEntry(cache.cache_id, cache_entry));
  EXPECT_EQ(cache_entry.id_to_batch_index_and_size.size(), 2);
  EXPECT_FALSE(cache_manager.GetCacheEntry(666, cache_entry));
  DataCacheKey data_cache_key;
  // 测试延迟释放
  EXPECT_EQ(cache_engine.Deallocate(cache.cache_id), ge::SUCCESS);
  EXPECT_TRUE(cache_manager.GetCacheKey(std::make_pair(cache.cache_id, 0UL), data_cache_key));
  EXPECT_TRUE(cache_manager.GetCacheEntry(data_cache_key, false, cache_entry));
  EXPECT_FALSE(cache_manager.GetCacheKey(std::make_pair(cache.cache_id, 1UL), data_cache_key));
  EXPECT_TRUE(cache_manager.GetCacheKey(std::make_pair(cache.cache_id, 2UL), data_cache_key));
  EXPECT_TRUE(cache_manager.GetCacheEntry(data_cache_key, false, cache_entry));
  EXPECT_FALSE(cache_manager.GetCacheKey(std::make_pair(cache.cache_id, 3UL), data_cache_key));

  EXPECT_EQ(cache_engine.RemoveCacheKey(cache_keys[0]), ge::SUCCESS);
  EXPECT_TRUE(cache_manager.GetCacheEntry(cache.cache_id, cache_entry));
  EXPECT_EQ(cache_entry.id_to_batch_index_and_size.size(), 1);
  cache_entry = {};

  EXPECT_EQ(cache_engine.RemoveCacheKey(cache_keys[1]), ge::SUCCESS);
  EXPECT_EQ(cache_engine.RemoveCacheKey(cache_keys[2]), ge::SUCCESS);
  EXPECT_EQ(cache_engine.RemoveCacheKey(cache_keys[3]), ge::SUCCESS);
  EXPECT_FALSE(cache_manager.GetCacheKey(std::make_pair(cache.cache_id, 0UL), data_cache_key));
  EXPECT_FALSE(cache_manager.GetCacheKey(std::make_pair(cache.cache_id, 2UL), data_cache_key));
  EXPECT_FALSE(cache_manager.GetCacheEntry(cache.cache_id, cache_entry));

  EXPECT_EQ(cache_engine.CheckCapacity(262144), ge::SUCCESS);
  EXPECT_EQ(cache_engine.Allocate(cache_desc, {}, cache_2), ge::SUCCESS);
  EXPECT_EQ(cache_engine.Deallocate(cache_2.cache_id), ge::SUCCESS);
  cache_engine.Finalize();
}

TEST_F(DataCacheEngineTest, CopyCache_C2C) {
  CommEntityManager comm_entity_manager;
  CommMemManager comm_mem_manager;
  DataCacheEngine cache_engine;
  cache_engine.SetCommEntityManager(&comm_entity_manager);
  cache_engine.SetCommMemManager(&comm_mem_manager);
  CacheManager cache_manager;
  cache_engine.SetCacheManager(&cache_manager);
  std::map<ge::AscendString, ge::AscendString> options;
  // 4 * 64K
  options[llm::LLM_OPTION_MEM_POOL_CONFIG] = "{\"memory_size\": 10000000}";
  EXPECT_EQ(cache_engine.Initialize(options), ge::SUCCESS);

  CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 4;
  src_cache_desc.placement = 1;
  src_cache_desc.shape = {4, 4};
  src_cache_desc.data_type = ge::DT_INT32;

  CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {4, 8};

  Cache src_cache;
  Cache dst_cache;
  EXPECT_EQ(cache_engine.Allocate(src_cache_desc, {}, src_cache), ge::SUCCESS);
  EXPECT_EQ(cache_engine.Allocate(dst_cache_desc, {}, dst_cache), ge::SUCCESS);

  CopyCacheParam copy_cache_param{};
  copy_cache_param.src_cache_id = src_cache.cache_id;
  copy_cache_param.dst_cache_id = dst_cache.cache_id;
  copy_cache_param.src_batch_index = 1;
  copy_cache_param.dst_batch_index = 2;

  auto src_tensor_base = reinterpret_cast<int32_t *>(src_cache.per_device_tensor_addrs[0][0]);
  auto dst_tensor_base = reinterpret_cast<int32_t *>(dst_cache.per_device_tensor_addrs[0][0]);
  std::iota(src_tensor_base, src_tensor_base + 4 * 4, 0);
  EXPECT_EQ(cache_engine.CopyCache(copy_cache_param), ge::SUCCESS);
  std::vector<int32_t> expected(4 * 8);
  std::iota(expected.begin() + 8 * 2, expected.begin() + 8 * 2 + 4, 4);
  std::vector<int32_t> dst_value(dst_tensor_base, dst_tensor_base + 4 * 8);
  EXPECT_EQ(dst_value, expected);

  copy_cache_param.size = 8;
  copy_cache_param.src_batch_index = 0;
  copy_cache_param.dst_batch_index = 1;
  EXPECT_EQ(cache_engine.CopyCache(copy_cache_param), ge::SUCCESS);
  dst_value = std::vector<int32_t>(dst_tensor_base + 8, dst_tensor_base + 12);
  expected = {0, 1, 0, 0};
  EXPECT_EQ(dst_value, expected);

  copy_cache_param.offset = 4;
  copy_cache_param.src_batch_index = 2;
  copy_cache_param.dst_batch_index = 0;
  EXPECT_EQ(cache_engine.CopyCache(copy_cache_param), ge::SUCCESS);
  dst_value = std::vector<int32_t>(dst_tensor_base, dst_tensor_base + 4);
  expected = {0, 9, 10, 0};
  EXPECT_EQ(dst_value, expected);
  cache_manager.Finalize();
  cache_engine.Finalize();
}

TEST_F(DataCacheEngineTest, CopyCache_C2C_Big) {
  CommEntityManager comm_entity_manager;
  CommMemManager comm_mem_manager;
  DataCacheEngine cache_engine;
  cache_engine.SetCommEntityManager(&comm_entity_manager);
  cache_engine.SetCommMemManager(&comm_mem_manager);
  CacheManager cache_manager;
  cache_engine.SetCacheManager(&cache_manager);
  std::map<ge::AscendString, ge::AscendString> options;
  // 4 * 64K
  options[llm::LLM_OPTION_MEM_POOL_CONFIG] = "{\"memory_size\": 10000000}";
  EXPECT_EQ(cache_engine.Initialize(options), ge::SUCCESS);

  int64_t big_dim = 1024 * 1024 + 128;
  CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 1;
  src_cache_desc.placement = 1;
  src_cache_desc.shape = {1, big_dim};
  src_cache_desc.data_type = ge::DT_INT32;

  CacheDesc dst_cache_desc = src_cache_desc;

  Cache src_cache;
  Cache dst_cache;
  EXPECT_EQ(cache_engine.Allocate(src_cache_desc, {}, src_cache), ge::SUCCESS);
  EXPECT_EQ(cache_engine.Allocate(dst_cache_desc, {}, dst_cache), ge::SUCCESS);

  CopyCacheParam copy_cache_param{};
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

TEST_F(DataCacheEngineTest, CopyCache_B2B) {
  CommEntityManager comm_entity_manager;
  CommMemManager comm_mem_manager;
  DataCacheEngine cache_engine;
  cache_engine.SetCommEntityManager(&comm_entity_manager);
  cache_engine.SetCommMemManager(&comm_mem_manager);
  CacheManager cache_manager;
  cache_engine.SetCacheManager(&cache_manager);
  std::map<ge::AscendString, ge::AscendString> options;
  // 4 * 64K
  options[llm::LLM_OPTION_MEM_POOL_CONFIG] = "{\"memory_size\": 10000000}";
  EXPECT_EQ(cache_engine.Initialize(options), ge::SUCCESS);

  CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 4;
  src_cache_desc.placement = 1;
  src_cache_desc.shape = {128, 4};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 4};

  Cache src_cache;
  Cache dst_cache;
  CacheKey src_cache_key;
  src_cache_key.is_allocate_blocks = true;
  CacheKey dst_cache_key = src_cache_key;
  dst_cache_key.req_id = 1;

  EXPECT_EQ(cache_engine.Allocate(src_cache_desc, {src_cache_key}, src_cache), ge::SUCCESS);
  EXPECT_EQ(cache_engine.Allocate(dst_cache_desc, {dst_cache_key}, dst_cache), ge::SUCCESS);

  CopyCacheParam copy_cache_param{};
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
  std::vector<int32_t> expected {
    16,17,18,19,
    8,9,10,11,
    12,13,14,15,
    0,1,2,3
  };
  std::vector<int32_t> dst_value(dst_tensor_base, dst_tensor_base + 4 * 4);
  EXPECT_EQ(dst_value, expected);
  cache_manager.Finalize();
  cache_engine.Finalize();
}

TEST_F(DataCacheEngineTest, InitializeMemoryPool_Failed) {
  DataCacheEngine cache_engine;
  CacheManager cache_manager;
  cache_engine.SetCacheManager(&cache_manager);
  std::map<ge::AscendString, ge::AscendString> options;
  options[llm::LLM_OPTION_MEM_POOL_CONFIG] = "{}";
  EXPECT_EQ(cache_engine.Initialize(options), ge::LLM_PARAM_INVALID);
  options[llm::LLM_OPTION_MEM_POOL_CONFIG] = "{memory_size}";
  EXPECT_EQ(cache_engine.Initialize(options), ge::LLM_PARAM_INVALID);
  {
    ScalableConfig config{};
    config.page_idem_num = 9;
    config.page_mem_size_total_threshold = 20UL * 1024 * 1024 * 1024;
    llm::LlmMemPool llm_mem_pool(config);
    EXPECT_EQ(llm_mem_pool.Initialize((void *) 0x1000000000, config.page_mem_size_total_threshold),
              ge::LLM_PARAM_INVALID);
  }
  {
    ScalableConfig config{};
    config.page_idem_num = 16;
    config.page_mem_size_total_threshold = (1UL << 16) - 1;
    llm::LlmMemPool llm_mem_pool(config);
    EXPECT_EQ(llm_mem_pool.Initialize((void *) 0x1000000000, config.page_mem_size_total_threshold),
              ge::LLM_PARAM_INVALID);
  }
}

TEST_F(DataCacheEngineTest, PullCache_D2H_C2C) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {4, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {2, 128};
  dst_cache_desc.placement = 0;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.batch_index = 1;

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  std::vector<int32_t> pull_result(2 * 128);
  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_D2H_C2C_with_layer_range) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {4, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {2, 128};
  dst_cache_desc.placement = 0;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.batch_index = 1;
  pull_cache_param.src_tensor_indices = {0, 1, 2, 3};
  pull_cache_param.dst_tensor_indices = {0, 1, 2, 3};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  std::vector<int32_t> pull_result(2 * 128);
  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, TransferCache_D2H_C2C) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {4, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {2, 128};
  dst_cache_desc.placement = 0;

  llm::PullCacheParam pull_cache_param{};

  DataCacheEngineTestRunner test_runner(2 * 1024 * 1024, true);
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);

  const auto &src_cache = test_runner.GetSrcCache();
  const auto &dst_cache = test_runner.GetDstCache();
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  transfer_cache_config.layer_index = 0;
  uint64_t layer_index = 2U;
  transfer_cache_config.dst_addrs =
      std::vector<uintptr_t>(dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2,
                             dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2 + 2);
  llm::TransferBlockConfig transfer_block_config{};
  ASSERT_EQ(test_runner.RunTransfer(transfer_cache_config, transfer_block_config), ge::SUCCESS);

  std::vector<int32_t> pull_result(2 * 128);
  test_runner.GetTransferCacheData(transfer_cache_config, pull_result);
  std::vector<int32_t> actual(&pull_result[0], &pull_result[4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_D2H_C2C_CheckFailed) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {4, 64};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {2, 128};
  dst_cache_desc.placement = 0;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.batch_index = 1;
  pull_cache_param.size = 128 * 4 + 1;

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);

  pull_cache_param.size = -1;
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);

  pull_cache_param.size = 64 * 4;
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  pull_cache_param.batch_index = 2;
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);
}

TEST_F(DataCacheEngineTest, PullCache_D2H_B2B_CheckFailed) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 0;
  dst_cache_desc.shape = {64, 128};

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0, 1, 4, 5, 6, 128};
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9, 63};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);

  pull_cache_param.prompt_blocks = {0, 1, 4, 5, 6, 127};
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9, 64};
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9, 63, 62};
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);
}

TEST_F(DataCacheEngineTest, PullCache_D2H_C2C_ByKey) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {4, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {2, 128};
  dst_cache_desc.placement = 0;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = 128 * 4;
  pull_cache_param.batch_index = 1;

  DataCacheEngineTestRunner test_runner;
  std::vector<CacheKey> src_cache_keys;
  for (int32_t i = 0; i < 4; ++i) {
    CacheKey cache_key{};
    cache_key.req_id = 1000 + i;
    cache_key.model_id = 1;
    cache_key.prefix_id = UINT64_MAX;
    src_cache_keys.emplace_back(cache_key);
  }
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param, &src_cache_keys);
  ASSERT_EQ(test_runner.Run(pull_cache_param, &src_cache_keys[1]), ge::SUCCESS);
  pull_cache_param.batch_index = 0;
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ASSERT_EQ(test_runner.Run(pull_cache_param, &src_cache_keys[2]), ge::SUCCESS);

  std::vector<int32_t> pull_result(2 * 128);
  test_runner.GetCacheData(pull_result);

  std::vector<int32_t> expected_value(2 * 128);
  std::iota(expected_value.begin(), expected_value.begin() + 128, 128 * 2 + 1);
  std::iota(expected_value.begin() + 128, expected_value.end(), 128 + 1);
  EXPECT_EQ(pull_result, expected_value);

  ASSERT_EQ(test_runner.Run(pull_cache_param, &src_cache_keys[1]), ge::LLM_KV_CACHE_NOT_EXIST);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ASSERT_EQ(test_runner.Run(pull_cache_param, &src_cache_keys[2]), ge::LLM_KV_CACHE_NOT_EXIST);
}

TEST_F(DataCacheEngineTest, PullCache_D2H_B2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 0;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0, 1, 4, 5, 6};
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  std::vector<int32_t> pull_result(128 * 128);
  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_D2H_B2B_with_layer_range) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 4;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 0;
  dst_cache_desc.num_tensors = 8;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0, 1, 4, 5, 6};
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9};
  pull_cache_param.dst_tensor_indices = {0, 1, 2, 3};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  std::vector<int32_t> pull_result(128 * 128);
  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, TransferCache_D2H_B2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 0;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0, 1, 4, 5, 6};
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9};

  DataCacheEngineTestRunner test_runner(2 * 1024 * 1024, true);
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  const auto &src_cache = test_runner.GetSrcCache();
  const auto &dst_cache = test_runner.GetDstCache();
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
  ASSERT_EQ(test_runner.RunTransfer(transfer_cache_config, transfer_block_config), ge::SUCCESS);

  std::vector<int32_t> pull_result(128 * 128);
  test_runner.GetTransferCacheData(transfer_cache_config, pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_D2H_B2B_BigBlock) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 1;
  src_cache_desc.shape = {1, 10L * 1024 * 1024};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 0;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks.resize(32);
  pull_cache_param.decoder_blocks.resize(32);

  DataCacheEngineTestRunner test_runner(100 * 1024 * 1024);
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);
}

TEST_F(DataCacheEngineTest, PullCache_D2H_B2B_MoreBlocks) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 1;
  src_cache_desc.shape = {256, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 0;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks.resize(256);
  pull_cache_param.decoder_blocks.resize(256);
  std::iota(pull_cache_param.prompt_blocks.begin(), pull_cache_param.prompt_blocks.end(), 0);
  std::iota(pull_cache_param.decoder_blocks.begin(), pull_cache_param.decoder_blocks.end(), 0);

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);
}

TEST_F(DataCacheEngineTest, PullCache_D2H_C2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  auto dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 2};
  dst_cache_desc.placement = 0;
  dst_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.decoder_blocks = {1, 3, 5, 7};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);
  std::vector<int32_t> pull_result(4 * 32);
  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[0], &pull_result[8]);
  EXPECT_EQ(actual, (std::vector<int32_t>{0, 0, 1, 2, 0, 0, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_D2H_C2B_with_layer_range) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  auto dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 2};
  dst_cache_desc.placement = 0;
  dst_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.decoder_blocks = {1, 3, 5, 7};
  pull_cache_param.src_tensor_indices = {0, 1, 2, 3};
  pull_cache_param.dst_tensor_indices = {0, 1, 2, 3};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);
  std::vector<int32_t> pull_result(4 * 32);
  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[0], &pull_result[8]);
  EXPECT_EQ(actual, (std::vector<int32_t>{0, 0, 1, 2, 0, 0, 3, 4}));
}

TEST_F(DataCacheEngineTest, TransferCache_D2H_C2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  auto dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 2};
  dst_cache_desc.placement = 0;
  dst_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::PullCacheParam pull_cache_param{};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  const auto &src_cache = test_runner.GetSrcCache();
  const auto &dst_cache = test_runner.GetDstCache();
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
  ASSERT_EQ(test_runner.RunTransfer(transfer_cache_config, transfer_block_config), ge::SUCCESS);

  std::vector<int32_t> pull_result(4 * 32);
  test_runner.GetTransferCacheData(transfer_cache_config, pull_result);
  std::vector<int32_t> actual(&pull_result[0], &pull_result[8]);
  EXPECT_EQ(actual, (std::vector<int32_t>{0, 0, 1, 2, 0, 0, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullDataCache_D2H_C2B_with_remaider) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 7};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  auto dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 2};
  dst_cache_desc.placement = 0;
  dst_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.decoder_blocks = {1, 3, 5, 7};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);
  std::vector<int32_t> pull_result(64 * 2);
  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[0], &pull_result[8]);
  EXPECT_EQ(actual, (std::vector<int32_t>{0, 0, 1, 2, 0, 0, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_D2H_B2C_Failed) {
  CacheEntry cache_entry;
  CacheKey cache_key;
  PullCacheParam pull_cache_param{};
  pull_cache_param.decoder_blocks = {};
  pull_cache_param.prompt_blocks = {1, 2, 3};

  DataCacheEngineTestContext context;
  context.Initialize(false);
  D2HDataTransferClient client(context.GetCommEntry(), nullptr);
  EXPECT_EQ(client.PullCache(cache_entry, cache_key,  pull_cache_param), ge::LLM_PARAM_INVALID);
  context.Finalize();
}

TEST_F(DataCacheEngineTest, PullCache_H2D_C2C) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {4, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 0;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {2, 128};
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.batch_index = 1;

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  std::vector<int32_t> pull_result(2 * 128);
  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_H2D_C2C_with_layer_range) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {4, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 0;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {2, 128};
  dst_cache_desc.placement = 1;
  dst_cache_desc.num_tensors = 4;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.batch_index = 1;
  pull_cache_param.src_tensor_indices = {0, 1, 2, 3};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  std::vector<int32_t> pull_result(2 * 128);
  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_H2D_C2C_CheckFailed) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {4, 64};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 0;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {2, 128};
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.batch_index = 1;

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);

  pull_cache_param.size = -1;
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);

  pull_cache_param.size = 64 * 4;
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  pull_cache_param.batch_index = 2;
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);
}

TEST_F(DataCacheEngineTest, PullCache_H2D_C2B_CheckFailed) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {4, 64};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 0;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {2, 128};
  dst_cache_desc.cache_mem_type = CacheMemType::BLOCKS;
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.decoder_blocks = {0};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);
}

TEST_F(DataCacheEngineTest, PullCache_H2D_B2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 0;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0, 1, 4, 5, 6};
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);

  std::vector<int32_t> pull_result(128 * 128);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_H2D_B2B_2) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 4;
  src_cache_desc.shape = {2, 4};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 0;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {1, 0};
  pull_cache_param.decoder_blocks = {1, 0};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  std::vector<int32_t> pull_result(2 * 4);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  test_runner.GetCacheData(pull_result, 3);
  EXPECT_EQ(pull_result, (std::vector<int32_t>{1, 2, 3, 4, 5, 6, 7, 8}));
}

TEST_F(DataCacheEngineTest, PullCache_H2D_B2B_with_layer_range) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 0;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0, 1, 4, 5, 6};
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9};
  pull_cache_param.src_tensor_indices = {0, 1, 2, 3};
  pull_cache_param.dst_tensor_indices = {0, 1, 2, 3};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);

  std::vector<int32_t> pull_result(128 * 128);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_H2D_B2B_MoreBlocks) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 2;
  src_cache_desc.shape = {100, 128, 512};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 0;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  for (int i = 0; i < 64; ++i) {
    pull_cache_param.prompt_blocks.push_back(i);
    pull_cache_param.decoder_blocks.push_back(i);
  }
  std::reverse(pull_cache_param.decoder_blocks.begin(), pull_cache_param.decoder_blocks.end());

  DataCacheEngineTestRunner test_runner(100 * 1024 * 1024);
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);
}

TEST_F(DataCacheEngineTest, PullCache_H2D_B2B_BigBlock) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 1;
  src_cache_desc.shape = {2, 10L * 1024 * 1024};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 0;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0, 1};
  pull_cache_param.decoder_blocks = {1, 0};

  DataCacheEngineTestRunner test_runner(100 * 1024 * 1024);
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);
}

TEST_F(DataCacheEngineTest, PullCache_H2D_B2B_CheckFailed) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 0;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;
  dst_cache_desc.shape = {64, 128};

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0, 1, 4, 5, 6, 128};
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9, 63};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);

  pull_cache_param.prompt_blocks = {0, 1, 4, 5, 6, 127};
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9, 64};
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9, 63, 62};
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);
}

TEST_F(DataCacheEngineTest, SyncFlagTimeout) {
  uint8_t flag = 0;
  SyncFlag sync_flag(&flag);
  auto tp = std::chrono::steady_clock::now();
  EXPECT_EQ(sync_flag.Wait(&tp), 0);
}

TEST_F(DataCacheEngineTest, PullCache_D2D_C2C) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {4, 128};

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.batch_index = 1;

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  std::vector<int32_t> pull_result(4 * 128);
  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_D2D_C2C_with_layer_range) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {4, 128};

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.batch_index = 1;
  pull_cache_param.src_tensor_indices = {0, 1, 2, 3};
  pull_cache_param.dst_tensor_indices = {0, 1, 2, 3};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  std::vector<int32_t> pull_result(4 * 128);
  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, TransferCache_D2D_C2C) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {4, 128};

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.batch_index = 1;

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  const auto &src_cache = test_runner.GetSrcCache();
  const auto &dst_cache = test_runner.GetDstCache();
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  transfer_cache_config.layer_index = 0;
  uint64_t layer_index = 2U;
  transfer_cache_config.dst_addrs =
      std::vector<uintptr_t>(dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2,
                             dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2 + 2);
  llm::TransferBlockConfig transfer_block_config{};
  ASSERT_EQ(test_runner.RunTransfer(transfer_cache_config, transfer_block_config), ge::SUCCESS);

  std::vector<int32_t> pull_result(4 * 128);
  test_runner.GetTransferCacheData(transfer_cache_config, pull_result);
  std::vector<int32_t> actual(&pull_result[0], &pull_result[4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_D2D_B2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0, 1, 4, 5, 6};
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);

  std::vector<int32_t> pull_result(128 * 128);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_D2D_B2B_BatchGet) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0, 1, 4, 5, 6};
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9};

  DataCacheEngineTestRunner test_runner(2 * 1024 * 1024, false, true);
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);

  llm::RuntimeStub::SetInstance(std::make_shared<MockRuntime>());
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_TIMEOUT);
  llm::RuntimeStub::Reset();
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  std::vector<int32_t> pull_result(128 * 128);
  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_D2D_B2B_CheckFailed) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;
  dst_cache_desc.shape = {64, 128};

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0, 1, 4, 5, 127, 128};
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 62, 63};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);

  pull_cache_param.prompt_blocks = {0, 1, 4, 5, 6, 127};
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 63, 64};
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9, 63, 62};
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::LLM_PARAM_INVALID);
}

TEST_F(DataCacheEngineTest, PullCache_D2D_B2B_over_1024_task) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 1;
  src_cache_desc.shape = {2048, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  std::vector<uint64_t> block_table(1024);
  std::iota(block_table.begin(), block_table.end(), 0);
  std::reverse(block_table.begin(), block_table.end());

  pull_cache_param.prompt_blocks = block_table;
  pull_cache_param.decoder_blocks = block_table;

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);

  std::vector<int32_t> pull_result(2048 * 128);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);

  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[0], &pull_result[4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, TransferCache_D2D_B2B_over_1024_task) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 2;
  src_cache_desc.shape = {2048, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  std::vector<uint64_t> block_table(1024);
  std::iota(block_table.begin(), block_table.end(), 0);
  std::reverse(block_table.begin(), block_table.end());

  pull_cache_param.prompt_blocks = block_table;
  pull_cache_param.decoder_blocks = block_table;

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);

  const auto &src_cache = test_runner.GetSrcCache();
  const auto &dst_cache = test_runner.GetDstCache();
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  transfer_cache_config.layer_index = 0;
  uint64_t layer_index = 0U;
  transfer_cache_config.dst_addrs =
      std::vector<uintptr_t>(dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2,
                             dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2 + 2);
  llm::TransferBlockConfig transfer_block_config{};
  transfer_block_config.src_blocks = block_table;
  transfer_block_config.dst_blocks = block_table;
  ASSERT_EQ(test_runner.RunTransfer(transfer_cache_config, transfer_block_config), ge::SUCCESS);

  std::vector<int32_t> pull_result(2048 * 128);
  test_runner.GetTransferCacheData(transfer_cache_config, pull_result);
  std::vector<int32_t> actual(&pull_result[0], &pull_result[4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_D2D_B2B_REG) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.prompt_blocks = {0, 1, 4, 5, 6};
  pull_cache_param.decoder_blocks = {1, 2, 4, 6, 9};

  std::vector<CacheKey> src_cache_keys;
  CacheKey cache_key{};
  cache_key.req_id = UINT64_MAX;
  cache_key.model_id = 1;
  cache_key.is_allocate_blocks = true;
  src_cache_keys.emplace_back(cache_key);

  DataCacheEngineTestRunner test_runner;
  test_runner.SetRegisterDevMem(true);
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param, &src_cache_keys);

  std::vector<int32_t> pull_result(128 * 128);
  ASSERT_EQ(test_runner.Run(pull_cache_param, &cache_key), ge::SUCCESS);

  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_D2D_C2B) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  auto dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 2};
  dst_cache_desc.placement = 1;
  dst_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.decoder_blocks = {1, 3, 5, 7};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);
  std::vector<int32_t> pull_result(64 * 2);
  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[0], &pull_result[8]);
  EXPECT_EQ(actual, (std::vector<int32_t>{0, 0, 1, 2, 0, 0, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullDataCache_D2D_C2B_with_remaider) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 7};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  auto dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 2};
  dst_cache_desc.placement = 1;
  dst_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = -1;
  pull_cache_param.decoder_blocks = {1, 3, 5, 7};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  ASSERT_EQ(test_runner.Run(pull_cache_param), ge::SUCCESS);
  std::vector<int32_t> pull_result(64 * 2);
  test_runner.GetCacheData(pull_result);
  std::vector<int32_t> actual(&pull_result[0], &pull_result[8]);
  EXPECT_EQ(actual, (std::vector<int32_t>{0, 0, 1, 2, 0, 0, 3, 4}));
}

TEST_F(DataCacheEngineTest, TransferDataCache_D2D_C2B_with_remaider) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {1, 7};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  auto dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {64, 2};
  dst_cache_desc.placement = 1;
  dst_cache_desc.cache_mem_type = CacheMemType::BLOCKS;

  llm::PullCacheParam pull_cache_param{};

  DataCacheEngineTestRunner test_runner;
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param);
  const auto &src_cache = test_runner.GetSrcCache();
  const auto &dst_cache = test_runner.GetDstCache();
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  transfer_cache_config.layer_index = 0;
  uint64_t layer_index = 2U;
  transfer_cache_config.dst_addrs =
      std::vector<uintptr_t>(dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2,
                             dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2 + 2);
  llm::TransferBlockConfig transfer_block_config{};
  transfer_block_config.block_mem_size = 8;
  transfer_block_config.dst_blocks = {1, 3, 5, 7};
  ASSERT_EQ(test_runner.RunTransfer(transfer_cache_config, transfer_block_config), ge::SUCCESS);

  std::vector<int32_t> pull_result(64 * 2);
  test_runner.GetTransferCacheData(transfer_cache_config, pull_result);
  std::vector<int32_t> actual(&pull_result[0], &pull_result[8]);
  EXPECT_EQ(actual, (std::vector<int32_t>{0, 0, 1, 2, 0, 0, 3, 4}));
}

TEST_F(DataCacheEngineTest, PullCache_D2D_C2C_ByKey) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 8;
  src_cache_desc.shape = {4, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;

  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.shape = {2, 128};
  dst_cache_desc.placement = 1;

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = 128 * 4;
  pull_cache_param.batch_index = 1;

  DataCacheEngineTestRunner test_runner;
  std::vector<CacheKey> src_cache_keys;
  for (int32_t i = 0; i < 4; ++i) {
    CacheKey cache_key{};
    cache_key.req_id = 1000 + i;
    cache_key.model_id = 1;
    cache_key.prefix_id = UINT64_MAX;
    src_cache_keys.emplace_back(cache_key);
  }
  test_runner.Initialize(src_cache_desc, dst_cache_desc, pull_cache_param, &src_cache_keys);
  ASSERT_EQ(test_runner.Run(pull_cache_param, &src_cache_keys[1]), ge::SUCCESS);
  pull_cache_param.batch_index = 0;
  ASSERT_EQ(test_runner.Run(pull_cache_param, &src_cache_keys[2]), ge::SUCCESS);

  std::vector<int32_t> pull_result(2 * 128);
  test_runner.GetCacheData(pull_result);

  std::vector<int32_t> expected_value(2 * 128);
  std::iota(expected_value.begin(), expected_value.begin() + 128, 128 * 2 + 1);
  std::iota(expected_value.begin() + 128, expected_value.end(), 128 + 1);
  EXPECT_EQ(pull_result, expected_value);

  ASSERT_EQ(test_runner.Run(pull_cache_param, &src_cache_keys[1]), ge::LLM_KV_CACHE_NOT_EXIST);
  ASSERT_EQ(test_runner.Run(pull_cache_param, &src_cache_keys[2]), ge::LLM_KV_CACHE_NOT_EXIST);
}

TEST_F(DataCacheEngineTest, LlmDataDistV2ApiTest) {
  llm::HcclAdapter::GetInstance().Finalize();
  llm::LLMDataDistV2 llm_data_dist(2);
  std::map<ge::AscendString, ge::AscendString> default_options{
      {llm::LLM_OPTION_ROLE, llm::kDecoder},
      {ge::OPTION_EXEC_DEVICE_ID, "0"},
      {llm::LLM_OPTION_SYNC_KV_CACHE_WAIT_TIME, "600000"},
      {llm::LLM_OPTION_MEM_POOL_CONFIG, "{\"memory_size\": 102428800}"}
  };

  EXPECT_EQ(llm_data_dist.LLMDataDistInitialize(default_options), ge::SUCCESS);
  llm::Cache cache;
  llm::CacheDesc cache_desc{};
  cache_desc.num_tensors = 8;
  cache_desc.shape = {4, 128};
  cache_desc.data_type = ge::DT_INT32;
  cache_desc.placement = 1;
  std::vector<llm::CacheKey> cache_keys;
  EXPECT_EQ(llm_data_dist.AllocateCache(cache_desc, cache, cache_keys), ge::SUCCESS);

  cache_desc.shape = {1, 10 * 1024 * 1024};
  EXPECT_EQ(llm_data_dist.AllocateCache(cache_desc, cache, cache_keys), ge::LLM_OUT_OF_MEMORY);

  llm::CacheKey cache_key{};
  cache_key.prompt_cluster_id = 0;
  cache_key.prompt_cache_id = 1;
  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.size = 128 * 4;
  pull_cache_param.batch_index = 1;
  EXPECT_EQ(llm_data_dist.PullCache(cache.cache_id, cache_key, pull_cache_param), ge::LLM_NOT_YET_LINK);
  EXPECT_EQ(llm_data_dist.DeallocateCache(cache.cache_id), ge::SUCCESS);
  EXPECT_EQ(llm_data_dist.RemoveCacheKey(cache_key), ge::SUCCESS);

  EXPECT_EQ(llm_data_dist.CheckCapacity(1024), ge::SUCCESS);
}

TEST_F(DataCacheEngineTest, TaskBatcher_Cont) {
  llm::TaskBatcher generator(10 * 1024 * 1024);
  std::vector<llm::TransferInfo> transfer_infos(1);
  transfer_infos[0].buffer_info.block_start_index = 0;
  transfer_infos[0].buffer_info.buffer_len = 3 * 1024 * 1024;
  generator.Initialize(8, 1024 * 1024, transfer_infos.size(), transfer_infos.data());
  std::vector<llm::BufferSlice> buffer_slices;
  while (true) {
    EXPECT_NO_THROW(buffer_slices = generator.NextBatch());
    if (buffer_slices.empty()) {
      break;
    }
    std::cout << "batch start" << std::endl;
    for (auto &task : buffer_slices) {
      std::cout << "buffer offset = " << task.buffer_offset
                << ", data_index = " << task.data_index
                << ", data_offset = " << task.data_offset
                << ", data_size = " << task.data_size << std::endl;
    }
    std::cout << "batch end" << std::endl;
  }
}

TEST_F(DataCacheEngineTest, TaskBatcher_Blocks) {
  std::vector<llm::TransferInfo> transfer_infos(2);
  transfer_infos[0].buffer_info.block_start_index = 1;
  transfer_infos[0].buffer_info.buffer_len = 2 * 128;
  transfer_infos[1].buffer_info.block_start_index = 4;
  transfer_infos[1].buffer_info.buffer_len = 3 * 128;
  llm::TaskBatcher generator(12 * 128);
  generator.Initialize(8, 128, transfer_infos.size(), transfer_infos.data());

  std::vector<llm::BufferSlice> buffer_slices;
  while (true) {
    EXPECT_NO_THROW(buffer_slices = generator.NextBatch());
    if (buffer_slices.empty()) {
      break;
    }
    std::cout << "batch start" << std::endl;
    for (auto &task : buffer_slices) {
      std::cout << "buffer offset = " << task.buffer_offset
                << ", data_index = " << task.data_index
                << ", data_offset = " << task.data_offset
                << ", data_size = " << task.data_size << std::endl;
    }
    std::cout << "batch end" << std::endl;
  }
}

TEST_F(DataCacheEngineTest, SwapBlocks) {
  llm::HcclAdapter::GetInstance().Finalize();
  llm::LLMDataDistV2 llm_data_dist(1);
  std::map<ge::AscendString, ge::AscendString> default_options{
      {llm::LLM_OPTION_ROLE, llm::kDecoder},
      {ge::OPTION_EXEC_DEVICE_ID, "0"},
      {llm::LLM_OPTION_SYNC_KV_CACHE_WAIT_TIME, "600000"},
      {llm::LLM_OPTION_MEM_POOL_CONFIG, "{\"memory_size\": 102428800}"}
  };

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

TEST_F(DataCacheEngineTest, TransferCache_D2D_B2B_with_cache_key) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 2;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;
  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  DataCacheEngineTestRunner test_runner(2 * 1024 * 1024, false, true);
  llm::PullCacheParam pull_cache_param{};
  CacheKey cache_key{};
  cache_key.prompt_cluster_id = 0;
  cache_key.req_id = UINT64_MAX;
  cache_key.model_id = 0;
  std::vector<llm::CacheKey> dst_cache_keys{cache_key};
  test_runner.InitializeV2(src_cache_desc, dst_cache_desc, pull_cache_param, dst_cache_keys);

  const auto &src_cache = test_runner.GetSrcCache();
  const auto &dst_cache = test_runner.GetDstCache();
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  auto layer_index = 0;
  transfer_cache_config.layer_index = layer_index;
  transfer_cache_config.type = 1U;
  transfer_cache_config.model_id_or_cache_id = 0;
  llm::TransferBlockConfig transfer_block_config{};
  transfer_block_config.src_blocks = {0};
  transfer_block_config.dst_blocks = {1};
  ASSERT_EQ(test_runner.RunTransfer(transfer_cache_config, transfer_block_config), ge::SUCCESS);

  std::vector<int32_t> pull_result(128 * 128);
  llm::TransferCacheConfig ret_config{};
  ret_config.dst_addrs =
  std::vector<uintptr_t>(dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2,
                       dst_cache.per_device_tensor_addrs[0].begin() + layer_index * 2 + 2);
  test_runner.GetTransferCacheData(ret_config, pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
}

TEST_F(DataCacheEngineTest, TransferCache_D2D_B2B_with_cache_key_by_index) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 4;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;
  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  DataCacheEngineTestRunner test_runner(2 * 1024 * 1024, false, true);
  llm::PullCacheParam pull_cache_param{};
  std::vector<llm::CacheKey> dst_cache_keys{};
  test_runner.InitializeV2(src_cache_desc, dst_cache_desc, pull_cache_param, dst_cache_keys);

  const auto &src_cache = test_runner.GetSrcCache();
  const auto &dst_cache = test_runner.GetDstCache();
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  auto layer_index = 0;
  transfer_cache_config.layer_index = layer_index;
  transfer_cache_config.type = 2U;
  transfer_cache_config.model_id_or_cache_id = dst_cache.cache_id;
  llm::TransferBlockConfig transfer_block_config{};
  transfer_block_config.src_blocks = {0};
  transfer_block_config.dst_blocks = {1};
  transfer_cache_config.dst_layer_index = 1;
  // set val
  auto dst_layer_k_tensor_addr_ptr = reinterpret_cast<int32_t *>(dst_cache.per_device_tensor_addrs[0][2]);
  for (int i = 0; i < 2 * 128; ++i) {
    *(dst_layer_k_tensor_addr_ptr + i) = 0;
  }
  ASSERT_EQ(test_runner.RunTransfer(transfer_cache_config, transfer_block_config), ge::SUCCESS);

  std::vector<int32_t> pull_result(128 * 128);
  llm::TransferCacheConfig ret_config{};
  ret_config.dst_addrs =
  std::vector<uintptr_t>(dst_cache.per_device_tensor_addrs[0].begin() + 1 * 2,
                       dst_cache.per_device_tensor_addrs[0].begin() + 1 * 2 + 2);
  test_runner.GetTransferCacheData(ret_config, pull_result);
  std::vector<int32_t> actual(&pull_result[128], &pull_result[128 + 4]);
  EXPECT_EQ(actual, (std::vector<int32_t>{1, 2, 3, 4}));
  std::vector<int32_t> actual1(&pull_result[0], &pull_result[4]);
  EXPECT_EQ(actual1, (std::vector<int32_t>{0, 0, 0, 0}));
}

TEST_F(DataCacheEngineTest, TransferCache_D2D_B2B_invalid_dst_layer_index) {
  llm::CacheDesc src_cache_desc{};
  src_cache_desc.num_tensors = 4;
  src_cache_desc.shape = {128, 128};
  src_cache_desc.data_type = ge::DT_INT32;
  src_cache_desc.placement = 1;
  src_cache_desc.cache_mem_type = CacheMemType::BLOCKS;
  llm::CacheDesc dst_cache_desc = src_cache_desc;
  dst_cache_desc.placement = 1;

  DataCacheEngineTestRunner test_runner(2 * 1024 * 1024, false, true);
  llm::PullCacheParam pull_cache_param{};
  std::vector<llm::CacheKey> dst_cache_keys{};
  test_runner.InitializeV2(src_cache_desc, dst_cache_desc, pull_cache_param, dst_cache_keys);

  const auto &src_cache = test_runner.GetSrcCache();
  const auto &dst_cache = test_runner.GetDstCache();
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  auto layer_index = 0;
  transfer_cache_config.layer_index = layer_index;
  transfer_cache_config.type = 2U;
  transfer_cache_config.model_id_or_cache_id = dst_cache.cache_id;
  llm::TransferBlockConfig transfer_block_config{};
  transfer_block_config.src_blocks = {0};
  transfer_block_config.dst_blocks = {1};
  transfer_cache_config.dst_layer_index = 2;
  ASSERT_EQ(test_runner.RunTransfer(transfer_cache_config, transfer_block_config), ge::LLM_PARAM_INVALID);
}

TEST_F(DataCacheEngineTest, ConvertHcclErrorCode) {
  ASSERT_EQ(HcclUtils::ConvertHcclErrorCode(HCCL_E_PARA), ge::LLM_PARAM_INVALID);
  ASSERT_EQ(HcclUtils::ConvertHcclErrorCode(HCCL_E_TIMEOUT), ge::LLM_TIMEOUT);
  ASSERT_EQ(HcclUtils::ConvertHcclErrorCode(HCCL_E_INTERNAL, ge::LLM_PARAM_INVALID), ge::LLM_PARAM_INVALID);
}
}  // namespace llm