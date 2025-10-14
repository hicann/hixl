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
#include <vector>
#include <cstdlib>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "llm_datadist/llm_datadist.h"
#include "slog/toolchain/slog.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "external/runtime/rt_error_codes.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"

using namespace std;
using namespace ge;
using namespace ::testing;
using ::testing::Invoke;
using ::testing::Mock;
namespace llm_datadist {
class LlmDataDistUTest : public ::testing::Test {
 protected:
  // 在测试类中设置一些准备工作，如果需要的话
  void SetUp() override {
    llm::MockMmpaForHcclApi::Install();
    llm::AutoCommResRuntimeMock::Install();
    llm::HcclAdapter::GetInstance().Initialize();
  }
  // 在测试类中进行清理工作，如果需要的话
  void TearDown() override {
    llm::HcclAdapter::GetInstance().Finalize();
    llm::MockMmpaForHcclApi::Reset();
    llm::AutoCommResRuntimeMock::Reset();
  }
};

TEST_F(LlmDataDistUTest, TestLocalCommResA2) {
  LlmDataDist llm_datadist_p(1U, LlmRole::kPrompt);
  std::map<AscendString, AscendString> options_p;
  options_p[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26000";
  options_p[llm_datadist::OPTION_DEVICE_ID] = "0";
  options_p["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "0",
          "device_ip": "1.1.1.1"
        }],
        "server_id": "127.0.0.1"
      }],
      "status": "completed",
      "version": "1.0"
    }
    )";

  EXPECT_EQ(llm_datadist_p.Initialize(options_p), SUCCESS);

  LlmDataDist llm_datadist_d(2U, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options_d;
  options_d[llm_datadist::OPTION_DEVICE_ID] = "1";
  options_d["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "1",
          "device_ip": "1.1.1.2"
        }],
        "server_id": "127.0.0.1"
      }],
      "status": "completed",
      "version": "1.0"
    }
    )";

  EXPECT_EQ(llm_datadist_d.Initialize(options_d), SUCCESS);

  // register d kv cache
  CacheDesc kv_desc{};
  kv_desc.num_tensors = 10;
  kv_desc.data_type = DT_INT32;
  kv_desc.shape = {4, 16};
  std::vector<uint64_t> d_tensor_addrs;
  auto d_buffers = std::vector<std::vector<int32_t>>(10, std::vector<int32_t>(4 * 16));
  for (uint32_t i = 0; i < kv_desc.num_tensors; ++i) {
    d_tensor_addrs.emplace_back(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(d_buffers[i].data())));
  }

  std::iota(d_buffers[0].begin(), d_buffers[0].end(), 0);
  RegisterCfg cfg{};
  int64_t d_cache_id = 0;
  EXPECT_EQ(llm_datadist_d.RegisterKvCache(kv_desc, d_tensor_addrs, cfg, d_cache_id), ge::SUCCESS);

  std::vector<uint64_t> p_tensor_addrs;
  auto p_buffers = std::vector<std::vector<int32_t>>(10, std::vector<int32_t>(4 * 16));
  for (uint32_t i = 0; i < kv_desc.num_tensors; ++i) {
    p_tensor_addrs.emplace_back(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p_buffers[i].data())));
  }

  std::iota(p_buffers[0].begin(), p_buffers[0].end(), 0);
  int64_t p_cache_id = 0;
  EXPECT_EQ(llm_datadist_p.RegisterKvCache(kv_desc, p_tensor_addrs, cfg, p_cache_id), ge::SUCCESS);

  // link
  ClusterInfo cluster_info;
  IpInfo ip_info;
  ip_info.ip = "127.0.0.1";
  ip_info.port = 26000;
  cluster_info.local_ip_infos = {ip_info};
  cluster_info.remote_ip_infos = {ip_info};
  std::vector<ge::Status> rets;
  EXPECT_EQ(llm_datadist_d.LinkLlmClusters({cluster_info}, rets), ge::SUCCESS);

  // pull cache
  CacheIndex cache_index{};
  cache_index.cluster_id = 1U;
  cache_index.cache_id = p_cache_id;
  Cache dst_cache{};
  dst_cache.cache_id = d_cache_id;
  EXPECT_EQ(llm_datadist_d.PullKvCache(cache_index, dst_cache), ge::SUCCESS);
  // push cache
  CacheIndex dst_cache_index{};
  dst_cache_index.cluster_id = 2U;
  dst_cache_index.cache_id = d_cache_id;
  Cache src_cache{};
  src_cache.cache_id = p_cache_id;
  KvCacheExtParam ext_param{};
  ext_param.src_layer_range = std::make_pair(0, 0);
  ext_param.dst_layer_range = std::make_pair(0, 0);
  EXPECT_EQ(llm_datadist_p.PushKvCache(src_cache, dst_cache_index, 0, -1, ext_param), ge::SUCCESS);

  EXPECT_EQ(llm_datadist_d.UnlinkLlmClusters({cluster_info}, rets), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_p.UnregisterKvCache(p_cache_id), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_d.UnregisterKvCache(d_cache_id), ge::SUCCESS);
  llm_datadist_p.Finalize();
  llm_datadist_d.Finalize();
}

TEST_F(LlmDataDistUTest, TestLocalCommResA3) {
  LlmDataDist llm_datadist_p(1U, LlmRole::kPrompt);
  std::map<AscendString, AscendString> options_p;
  options_p[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26000";
  options_p[llm_datadist::OPTION_DEVICE_ID] = "0";
  options_p["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "0",
          "super_device_id": "0",
          "device_ip": "1.1.1.1"
        }],
        "server_id": "127.0.0.1"
      }],
      "super_pod_list": [
      {
          "super_pod_id": "0",
          "server_list": [
          {"server_id": "127.0.0.1"}]
      }],
      "status": "completed",
      "version": "1.2"
    }
    )";

  EXPECT_EQ(llm_datadist_p.Initialize(options_p), SUCCESS);

  LlmDataDist llm_datadist_d(2U, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options_d;
  options_d[llm_datadist::OPTION_DEVICE_ID] = "1";
  options_d["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "1",
          "super_device_id": "1",
          "device_ip": "1.1.1.2"
        }],
        "server_id": "127.0.0.1"
      }],
      "super_pod_list": [
      {
          "super_pod_id": "0",
          "server_list": [
          {"server_id": "127.0.0.1"}]
      }],
      "status": "completed",
      "version": "1.2"
    }
    )";

  EXPECT_EQ(llm_datadist_d.Initialize(options_d), SUCCESS);

  // register d kv cache
  CacheDesc kv_desc{};
  kv_desc.num_tensors = 10;
  kv_desc.data_type = DT_INT32;
  kv_desc.shape = {4, 16};
  std::vector<uint64_t> d_tensor_addrs;
  auto d_buffers = std::vector<std::vector<int32_t>>(10, std::vector<int32_t>(4 * 16));
  for (uint32_t i = 0; i < kv_desc.num_tensors; ++i) {
    d_tensor_addrs.emplace_back(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(d_buffers[i].data())));
  }

  std::iota(d_buffers[0].begin(), d_buffers[0].end(), 0);
  RegisterCfg cfg{};
  int64_t d_cache_id = 0;
  EXPECT_EQ(llm_datadist_d.RegisterKvCache(kv_desc, d_tensor_addrs, cfg, d_cache_id), ge::SUCCESS);

  std::vector<uint64_t> p_tensor_addrs;
  auto p_buffers = std::vector<std::vector<int32_t>>(10, std::vector<int32_t>(4 * 16));
  for (uint32_t i = 0; i < kv_desc.num_tensors; ++i) {
    p_tensor_addrs.emplace_back(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p_buffers[i].data())));
  }

  std::iota(p_buffers[0].begin(), p_buffers[0].end(), 0);
  int64_t p_cache_id = 0;
  EXPECT_EQ(llm_datadist_p.RegisterKvCache(kv_desc, p_tensor_addrs, cfg, p_cache_id), ge::SUCCESS);

  // link
  ClusterInfo cluster_info;
  IpInfo ip_info;
  ip_info.ip = "127.0.0.1";
  ip_info.port = 26000;
  cluster_info.local_ip_infos = {ip_info};
  cluster_info.remote_ip_infos = {ip_info};
  std::vector<ge::Status> rets;
  EXPECT_EQ(llm_datadist_d.LinkLlmClusters({cluster_info}, rets), ge::SUCCESS);

  // pull cache
  CacheIndex cache_index{};
  cache_index.cluster_id = 1U;
  cache_index.cache_id = p_cache_id;
  Cache dst_cache{};
  dst_cache.cache_id = d_cache_id;
  EXPECT_EQ(llm_datadist_d.PullKvBlocks(cache_index, dst_cache, {0}, {0}), ge::SUCCESS);
  // push cache
  CacheIndex dst_cache_index{};
  dst_cache_index.cluster_id = 2U;
  dst_cache_index.cache_id = d_cache_id;
  Cache src_cache{};
  src_cache.cache_id = p_cache_id;
  KvCacheExtParam ext_param{};
  ext_param.src_layer_range = std::make_pair(0, 0);
  ext_param.dst_layer_range = std::make_pair(0, 0);
  EXPECT_EQ(llm_datadist_p.PushKvBlocks(src_cache, dst_cache_index, {0}, {0}, ext_param), ge::SUCCESS);

  EXPECT_EQ(llm_datadist_d.UnlinkLlmClusters({cluster_info}, rets), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_p.UnregisterKvCache(p_cache_id), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_d.UnregisterKvCache(d_cache_id), ge::SUCCESS);
  llm_datadist_p.Finalize();
  llm_datadist_d.Finalize();
}

TEST_F(LlmDataDistUTest, TestAutoLocalCommResA2) {
  class AutoCommResV1RuntimeMock : public llm::AutoCommResRuntimeMock {
   public:
    rtError_t rtGetSocVersion(char *version, const uint32_t maxLen) override {
      (void)strcpy_s(version, maxLen, "Ascend910B1");
      return RT_ERROR_NONE;
    }
  };
  llm::RuntimeStub::SetInstance(std::make_shared<AutoCommResV1RuntimeMock>());
  LlmDataDist llm_datadist_p(1U, LlmRole::kPrompt);
  std::map<AscendString, AscendString> options_p;
  options_p[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26000";
  options_p[llm_datadist::OPTION_DEVICE_ID] = "0";

  EXPECT_EQ(llm_datadist_p.Initialize(options_p), SUCCESS);

  LlmDataDist llm_datadist_d(2U, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options_d;
  options_d[llm_datadist::OPTION_DEVICE_ID] = "1";

  EXPECT_EQ(llm_datadist_d.Initialize(options_d), SUCCESS);

  // link
  ClusterInfo cluster_info;
  IpInfo ip_info;
  ip_info.ip = "127.0.0.1";
  ip_info.port = 26000;
  cluster_info.local_ip_infos = {ip_info};
  cluster_info.remote_ip_infos = {ip_info};
  std::vector<ge::Status> rets;
  EXPECT_EQ(llm_datadist_d.LinkLlmClusters({cluster_info}, rets), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_d.UnlinkLlmClusters({cluster_info}, rets), ge::SUCCESS);
  llm_datadist_p.Finalize();
  llm_datadist_d.Finalize();
}

TEST_F(LlmDataDistUTest, TestAutoLocalCommResA3) {
  LlmDataDist llm_datadist_p(1U, LlmRole::kPrompt);
  std::map<AscendString, AscendString> options_p;
  options_p[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26000";
  options_p[llm_datadist::OPTION_DEVICE_ID] = "0";

  EXPECT_EQ(llm_datadist_p.Initialize(options_p), SUCCESS);

  LlmDataDist llm_datadist_d(2U, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options_d;
  options_d[llm_datadist::OPTION_DEVICE_ID] = "1";

  EXPECT_EQ(llm_datadist_d.Initialize(options_d), SUCCESS);

  // link
  ClusterInfo cluster_info;
  IpInfo ip_info;
  ip_info.ip = "127.0.0.1";
  ip_info.port = 26000;
  cluster_info.local_ip_infos = {ip_info};
  cluster_info.remote_ip_infos = {ip_info};
  std::vector<ge::Status> rets;
  EXPECT_EQ(llm_datadist_d.LinkLlmClusters({cluster_info}, rets), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_d.UnlinkLlmClusters({cluster_info}, rets), ge::SUCCESS);
  llm_datadist_p.Finalize();
  llm_datadist_d.Finalize();
}

TEST_F(LlmDataDistUTest, TestAutoLocalCommResWithoutDeviceIp) {
  LlmDataDist llm_datadist_p(1U, LlmRole::kPrompt);
  std::map<AscendString, AscendString> options_p;
  options_p[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26000";
  options_p[llm_datadist::OPTION_DEVICE_ID] = "9";
  EXPECT_EQ(llm_datadist_p.Initialize(options_p), SUCCESS);
}

TEST_F(LlmDataDistUTest, TestLocalCommResA3LinkFailed) {
  LlmDataDist llm_datadist_d(2U, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options_d;
  options_d[llm_datadist::OPTION_DEVICE_ID] = "1";
  options_d["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "1",
          "super_device_id": "1",
          "device_ip": "1.1.1.2"
        }],
        "server_id": "127.0.0.1"
      }],
      "super_pod_list": [
      {
          "super_pod_id": "0",
          "server_list": [
          {"server_id": "127.0.0.1"}]
      }],
      "status": "completed",
      "version": "1.2"
    }
    )";
  EXPECT_EQ(llm_datadist_d.Initialize(options_d), SUCCESS);

  // link
  ClusterInfo cluster_info;
  IpInfo ip_info;
  ip_info.ip = "127.0.0.1";
  ip_info.port = 26000;
  cluster_info.local_ip_infos = {ip_info};
  cluster_info.remote_ip_infos = {ip_info};
  std::vector<ge::Status> rets;
  // p is not listen
  EXPECT_EQ(llm_datadist_d.LinkLlmClusters({cluster_info}, rets), ge::LLM_LINK_FAILED);
  llm_datadist_d.Finalize();
}

TEST_F(LlmDataDistUTest, TestLocalCommResInvalidJson) {
  LlmDataDist llm_datadist_p(1U, LlmRole::kPrompt);
  std::map<AscendString, AscendString> options_p;
  options_p[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26000";
  options_p[llm_datadist::OPTION_DEVICE_ID] = "0";
  // invalid json
  options_p["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "0",xxxxxxxxxxxxxxx
          "device_ip": "1.1.1.1"
        }],
        "server_id": "127.0.0.1"
      }],
      "status": "completed",
      "version": "1.0"
    }
    )";

  EXPECT_EQ(llm_datadist_p.Initialize(options_p), SUCCESS);

  LlmDataDist llm_datadist_d(2U, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options_d;
  options_d[llm_datadist::OPTION_DEVICE_ID] = "1";
  options_d["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "1",
          "device_ip": "1.1.1.2"
        }],
        "server_id": "127.0.0.1"
      }],
      "status": "completed",
      "version": "1.0"
    }
    )";

  EXPECT_EQ(llm_datadist_d.Initialize(options_d), SUCCESS);

  // link
  ClusterInfo cluster_info;
  IpInfo ip_info;
  ip_info.ip = "127.0.0.1";
  ip_info.port = 26000;
  cluster_info.local_ip_infos = {ip_info};
  cluster_info.remote_ip_infos = {ip_info};
  std::vector<ge::Status> rets;
  EXPECT_NE(llm_datadist_d.LinkLlmClusters({cluster_info}, rets), ge::SUCCESS);

  llm_datadist_p.Finalize();
  llm_datadist_d.Finalize();
}

TEST_F(LlmDataDistUTest, TestLocalCommResInvalidVersion) {
  LlmDataDist llm_datadist_p(1U, LlmRole::kPrompt);
  std::map<AscendString, AscendString> options_p;
  options_p[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26000";
  options_p[llm_datadist::OPTION_DEVICE_ID] = "0";
  // invalid version != 1.0 && != 1.2
  options_p["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "0",
          "device_ip": "1.1.1.1"
        }],
        "server_id": "127.0.0.1"
      }],
      "status": "completed",
      "version": "1.9"
    }
    )";

  EXPECT_EQ(llm_datadist_p.Initialize(options_p), SUCCESS);

  LlmDataDist llm_datadist_d(2U, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options_d;
  options_d[llm_datadist::OPTION_DEVICE_ID] = "1";
  options_d["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "1",
          "device_ip": "1.1.1.2"
        }],
        "server_id": "127.0.0.1"
      }],
      "status": "completed",
      "version": "1.9"
    }
    )";

  EXPECT_EQ(llm_datadist_d.Initialize(options_d), SUCCESS);

  // link
  ClusterInfo cluster_info;
  IpInfo ip_info;
  ip_info.ip = "127.0.0.1";
  ip_info.port = 26000;
  cluster_info.local_ip_infos = {ip_info};
  cluster_info.remote_ip_infos = {ip_info};
  std::vector<ge::Status> rets;
  EXPECT_NE(llm_datadist_d.LinkLlmClusters({cluster_info}, rets), ge::SUCCESS);

  llm_datadist_p.Finalize();
  llm_datadist_d.Finalize();
}

TEST_F(LlmDataDistUTest, RegisterKvCache) {
  uint64_t prompt_cluster_id = 0U;
  LlmDataDist llm_datadist(prompt_cluster_id, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options;
  options[llm_datadist::OPTION_DEVICE_ID] = "1";
  options["llm.LocalCommRes"] = "mock";

  EXPECT_EQ(llm_datadist.Initialize(options), SUCCESS);

  CacheDesc kv_desc{};
  kv_desc.num_tensors = 10;
  kv_desc.data_type = DT_INT32;
  kv_desc.shape = {4, 16};
  std::vector<uint64_t> tensor_addrs;

  auto buffers = std::vector<std::vector<int32_t>>(10, std::vector<int32_t>(4 * 16));
  for (uint32_t i = 0; i < kv_desc.num_tensors; ++i) {
    tensor_addrs.emplace_back(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buffers[i].data())));
  }

  std::iota(buffers[0].begin(), buffers[0].end(), 0);
  RegisterCfg cfg{};
  int64_t cache_id = 0;
  EXPECT_EQ(llm_datadist.RegisterKvCache(kv_desc, tensor_addrs, cfg, cache_id), ge::SUCCESS);
  llm_datadist.Finalize();
}

TEST_F(LlmDataDistUTest, UnregisterKvCache) {
  uint64_t prompt_cluster_id = 0U;
  LlmDataDist llm_datadist(prompt_cluster_id, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options;
  options[llm_datadist::OPTION_DEVICE_ID] = "1";
  options["llm.LocalCommRes"] = "mock";

  EXPECT_EQ(llm_datadist.Initialize(options), SUCCESS);

  int64_t cache_id = 0;
  EXPECT_EQ(llm_datadist.UnregisterKvCache(cache_id), ge::SUCCESS);
  llm_datadist.Finalize();
}

TEST_F(LlmDataDistUTest, LocalCommResSwitchRoleSuccess) {
  LlmDataDist llm_datadist(0, LlmRole::kMix);
  std::map<AscendString, AscendString> options;
  options[llm_datadist::OPTION_DEVICE_ID] = "1";
  options["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "0",
          "device_ip": "1.1.1.1"
        }],
        "server_id": "127.0.0.1"
      }],
      "status": "completed",
      "version": "1.0"
    }
    )";
  EXPECT_EQ(llm_datadist.Initialize(options), ge::SUCCESS);
  EXPECT_EQ(llm_datadist.SetRole(LlmRole::kDecoder, {}), ge::SUCCESS);
  std::map<AscendString, AscendString> switch_options;
  switch_options[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26001";
  EXPECT_EQ(llm_datadist.SetRole(LlmRole::kPrompt, switch_options), ge::SUCCESS);
  // listen same port
  EXPECT_EQ(llm_datadist.SetRole(LlmRole::kDecoder, switch_options), ge::SUCCESS);
  EXPECT_EQ(llm_datadist.SetRole(LlmRole::kMix, {}), ge::SUCCESS);
  EXPECT_EQ(llm_datadist.SetRole(LlmRole::kPrompt, switch_options), ge::SUCCESS);
  EXPECT_EQ(llm_datadist.SetRole(LlmRole::kPrompt), ge::SUCCESS);  // close listen
  EXPECT_EQ(llm_datadist.SetRole(LlmRole::kDecoder, switch_options), ge::SUCCESS); // decode listen
  EXPECT_EQ(llm_datadist.SetRole(LlmRole::kDecoder), ge::SUCCESS); // close listen
  llm_datadist.Finalize();
}

TEST_F(LlmDataDistUTest, AllocateCacheNotSupported) {
  uint64_t prompt_cluster_id = 0U;
  LlmDataDist llm_datadist(prompt_cluster_id, LlmRole::kPrompt);
  std::map<AscendString, AscendString> options;
  options[llm_datadist::OPTION_DEVICE_ID] = "1";
  EXPECT_EQ(llm_datadist.Initialize(options), SUCCESS);

  CacheDesc kv_desc{};
  kv_desc.num_tensors = 80;
  kv_desc.data_type = DT_FLOAT16;
  kv_desc.shape = {1, 256};
  Cache cached_tensors;
  EXPECT_EQ(llm_datadist.AllocateCache(kv_desc, cached_tensors), LLM_FEATURE_NOT_ENABLED);
  EXPECT_EQ(llm_datadist.DeallocateCache(0), LLM_FEATURE_NOT_ENABLED);
  llm_datadist.Finalize();
}

TEST_F(LlmDataDistUTest, CopyCacheNotSupported) {
  uint64_t prompt_cluster_id = 0U;
  LlmDataDist llm_datadist(prompt_cluster_id, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options;
  options[llm_datadist::OPTION_DEVICE_ID] = "1";
  options["llm.LocalCommRes"] = "mock";

  EXPECT_EQ(llm_datadist.Initialize(options), SUCCESS);

  CacheDesc kv_desc{};
  kv_desc.num_tensors = 10;
  kv_desc.data_type = DT_INT32;
  kv_desc.shape = {4, 16};
  std::vector<uint64_t> tensor_addrs;

  auto buffers = std::vector<std::vector<int32_t>>(10, std::vector<int32_t>(4 * 16));
  for (uint32_t i = 0; i < kv_desc.num_tensors; ++i) {
    tensor_addrs.emplace_back(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buffers[i].data())));
  }

  std::iota(buffers[0].begin(), buffers[0].end(), 0);
  RegisterCfg cfg{};
  int64_t cache_id1 = 0;
  int64_t cache_id2 = 0;
  EXPECT_EQ(llm_datadist.RegisterKvCache(kv_desc, tensor_addrs, cfg, cache_id1), ge::SUCCESS);
  EXPECT_EQ(llm_datadist.RegisterKvCache(kv_desc, tensor_addrs, cfg, cache_id2), ge::SUCCESS);
  Cache src_cache{};
  src_cache.cache_id = cache_id1;
  src_cache.cache_desc = kv_desc;
  src_cache.tensor_addrs = tensor_addrs;
  Cache dst_cache{};
  dst_cache.cache_id = cache_id2;
  dst_cache.cache_desc = kv_desc;
  dst_cache.tensor_addrs = tensor_addrs;
  EXPECT_EQ(llm_datadist.CopyKvCache(src_cache, dst_cache, 0, 1), LLM_FEATURE_NOT_ENABLED);
  EXPECT_EQ(llm_datadist.CopyKvBlocks(src_cache, dst_cache, {0}, {{0}}), LLM_FEATURE_NOT_ENABLED);
  llm_datadist.Finalize();
}

ge::Status LlmDataDistUTestV2TestChkAclRt(int32_t ret) {
  LLM_CHK_ACL_RET(ret);
  return ge::SUCCESS;
}

TEST_F(LlmDataDistUTest, TestChkAclRt) {
  int32_t test_ret = static_cast<int32_t>(ACL_ERROR_RT_STREAM_SYNC_TIMEOUT);
  EXPECT_EQ(LlmDataDistUTestV2TestChkAclRt(test_ret), ge::LLM_TIMEOUT);
  test_ret = 0;
  EXPECT_EQ(LlmDataDistUTestV2TestChkAclRt(test_ret), ge::SUCCESS);
}

TEST_F(LlmDataDistUTest, TestPushWithRangeAndTensorNumV2) {
  uint64_t prompt_cluster_id = 0U;
  uint64_t decoder_cluster_id = 1U;
  LlmDataDist llm_datadist_p(prompt_cluster_id, LlmRole::kPrompt);
  std::map<AscendString, AscendString> options_p;
  options_p[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26000";
  options_p[llm_datadist::OPTION_DEVICE_ID] = "0";
  options_p["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "0",
          "device_ip": "1.1.1.1"
        }],
        "server_id": "127.0.0.1"
      }],
      "status": "completed",
      "version": "1.0"
    }
    )";

  EXPECT_EQ(llm_datadist_p.Initialize(options_p), SUCCESS);

  LlmDataDist llm_datadist_d(decoder_cluster_id, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options_d;
  options_d[llm_datadist::OPTION_DEVICE_ID] = "1";
  options_d["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "1",
          "device_ip": "1.1.1.2"
        }],
        "server_id": "127.0.0.1"
      }],
      "status": "completed",
      "version": "1.0"
    }
    )";

  EXPECT_EQ(llm_datadist_d.Initialize(options_d), SUCCESS);

  // register d kv cache
  CacheDesc kv_desc{};
  kv_desc.num_tensors = 10;
  kv_desc.data_type = DT_INT32;
  kv_desc.shape = {4, 16};
  std::vector<uint64_t> d_tensor_addrs;
  auto d_buffers = std::vector<std::vector<int32_t>>(10, std::vector<int32_t>(4 * 16));
  for (uint32_t i = 0; i < kv_desc.num_tensors; ++i) {
    d_tensor_addrs.emplace_back(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(d_buffers[i].data())));
  }

  std::iota(d_buffers[0].begin(), d_buffers[0].end(), 0);
  RegisterCfg cfg{};
  int64_t d_cache_id = 0;
  EXPECT_EQ(llm_datadist_d.RegisterKvCache(kv_desc, d_tensor_addrs, cfg, d_cache_id), ge::SUCCESS);

  std::vector<uint64_t> p_tensor_addrs;
  auto p_buffers = std::vector<std::vector<int32_t>>(10, std::vector<int32_t>(4 * 16));
  for (uint32_t i = 0; i < kv_desc.num_tensors; ++i) {
    p_tensor_addrs.emplace_back(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p_buffers[i].data())));
  }

  std::iota(p_buffers[0].begin(), p_buffers[0].end(), 0);
  int64_t p_cache_id = 0;
  EXPECT_EQ(llm_datadist_p.RegisterKvCache(kv_desc, p_tensor_addrs, cfg, p_cache_id), ge::SUCCESS);

  // link
  ClusterInfo cluster_info;
  IpInfo ip_info;
  ip_info.ip = "127.0.0.1";
  ip_info.port = 26000;
  cluster_info.local_ip_infos = {ip_info};
  cluster_info.remote_ip_infos = {ip_info};
  std::vector<ge::Status> rets;
  EXPECT_EQ(llm_datadist_d.LinkLlmClusters({cluster_info}, rets), ge::SUCCESS);

  // prompt push cache to decoder
  CacheIndex dst_cache_index{decoder_cluster_id, d_cache_id};

  Cache src_cache{};
  src_cache.cache_id = p_cache_id;
  src_cache.cache_desc = kv_desc;
  KvCacheExtParam ext_param = {};

  // test default range and tensor num
  EXPECT_EQ(llm_datadist_p.PushKvBlocks(src_cache,
                                      dst_cache_index,
                                      {0U, 1U},
                                      {0U, 1U},
                                      ext_param), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_p.PushKvCache(src_cache, dst_cache_index, 0, -1, ext_param), ge::SUCCESS);

  // test appointed range and tensor num
  ext_param.src_layer_range = std::make_pair(0, 1);
  ext_param.dst_layer_range = std::make_pair(0, 1);

  ext_param.tensor_num_per_layer = 1;
  EXPECT_EQ(llm_datadist_p.PushKvBlocks(src_cache,
                                      dst_cache_index,
                                      {0U, 1U},
                                      {0U, 1U},
                                      ext_param), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_p.PushKvCache(src_cache, dst_cache_index, 0, -1, ext_param), ge::SUCCESS);

  ext_param.tensor_num_per_layer = 2;
  EXPECT_EQ(llm_datadist_p.PushKvBlocks(src_cache,
                                      dst_cache_index,
                                      {0U, 1U},
                                      {0U, 1U},
                                      ext_param), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_p.PushKvCache(src_cache, dst_cache_index, 0, -1, ext_param), ge::SUCCESS);

  ext_param.tensor_num_per_layer = 7;
  EXPECT_NE(llm_datadist_p.PushKvBlocks(src_cache,
                                      dst_cache_index,
                                      {0U, 1U},
                                      {0U, 1U},
                                      ext_param), ge::SUCCESS);
  EXPECT_NE(llm_datadist_p.PushKvCache(src_cache, dst_cache_index, 0, -1, ext_param), ge::SUCCESS);

  ext_param.tensor_num_per_layer = 17;
  EXPECT_NE(llm_datadist_p.PushKvBlocks(src_cache,
                                      dst_cache_index,
                                      {0U, 1U},
                                      {0U, 1U},
                                      ext_param), ge::SUCCESS);
  EXPECT_NE(llm_datadist_p.PushKvCache(src_cache, dst_cache_index, 0, -1, ext_param), ge::SUCCESS);

  // test invalid range and tensor num
  ext_param.tensor_num_per_layer = 2;
  ext_param.dst_layer_range = std::make_pair(0, 1);
  ext_param.src_layer_range = std::make_pair(-2, 0);
  EXPECT_NE(llm_datadist_p.PushKvBlocks(src_cache,
                                      dst_cache_index,
                                      {0U, 1U},
                                      {0U, 1U},
                                      ext_param), ge::SUCCESS);
  EXPECT_NE(llm_datadist_p.PushKvCache(src_cache, dst_cache_index, 0, -1, ext_param), ge::SUCCESS);

  // the range not same
  ext_param.src_layer_range = std::make_pair(0, 1);
  ext_param.dst_layer_range = std::make_pair(0, 3);
  EXPECT_NE(llm_datadist_p.PushKvBlocks(src_cache,
                                      dst_cache_index,
                                      {0U, 1U},
                                      {0U, 1U},
                                      ext_param), ge::SUCCESS);
  EXPECT_NE(llm_datadist_p.PushKvCache(src_cache, dst_cache_index, 0, -1, ext_param), ge::SUCCESS);

  // the src range over range
  ext_param.src_layer_range = std::make_pair(4, 5);
  ext_param.dst_layer_range = std::make_pair(0, 1);
  EXPECT_NE(llm_datadist_p.PushKvBlocks(src_cache,
                                      dst_cache_index,
                                      {0U, 1U},
                                      {0U, 1U},
                                      ext_param), ge::SUCCESS);
  EXPECT_NE(llm_datadist_p.PushKvCache(src_cache, dst_cache_index, 0, -1, ext_param), ge::SUCCESS);

  // the dst range over range
  ext_param.src_layer_range = std::make_pair(0, 1);
  ext_param.dst_layer_range = std::make_pair(4, 5);
  EXPECT_NE(llm_datadist_p.PushKvBlocks(src_cache,
                                      dst_cache_index,
                                      {0U, 1U},
                                      {0U, 1U},
                                      ext_param), ge::SUCCESS);
  EXPECT_NE(llm_datadist_p.PushKvCache(src_cache, dst_cache_index, 0, -1, ext_param), ge::SUCCESS);

  EXPECT_EQ(llm_datadist_d.UnlinkLlmClusters({cluster_info}, rets), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_p.UnregisterKvCache(p_cache_id), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_d.UnregisterKvCache(d_cache_id), ge::SUCCESS);
  llm_datadist_p.Finalize();
  llm_datadist_d.Finalize();
}

TEST_F(LlmDataDistUTest, TestPullWithRangeAndTensorNumV2) {
  uint64_t prompt_cluster_id = 0U;
  uint64_t decoder_cluster_id = 1U;
  LlmDataDist llm_datadist_p(prompt_cluster_id, LlmRole::kPrompt);
  std::map<AscendString, AscendString> options_p;
  options_p[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26000";
  options_p[llm_datadist::OPTION_DEVICE_ID] = "0";
  options_p["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "0",
          "device_ip": "1.1.1.1"
        }],
        "server_id": "127.0.0.1"
      }],
      "status": "completed",
      "version": "1.0"
    }
    )";

  EXPECT_EQ(llm_datadist_p.Initialize(options_p), SUCCESS);

  LlmDataDist llm_datadist_d(decoder_cluster_id, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options_d;
  options_d[llm_datadist::OPTION_DEVICE_ID] = "1";
  options_d["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "1",
          "device_ip": "1.1.1.2"
        }],
        "server_id": "127.0.0.1"
      }],
      "status": "completed",
      "version": "1.0"
    }
    )";

  EXPECT_EQ(llm_datadist_d.Initialize(options_d), SUCCESS);

  // register d kv cache
  CacheDesc kv_desc{};
  kv_desc.num_tensors = 10;
  kv_desc.data_type = DT_INT32;
  kv_desc.shape = {4, 16};
  std::vector<uint64_t> d_tensor_addrs;
  auto d_buffers = std::vector<std::vector<int32_t>>(10, std::vector<int32_t>(4 * 16));
  for (uint32_t i = 0; i < kv_desc.num_tensors; ++i) {
    d_tensor_addrs.emplace_back(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(d_buffers[i].data())));
  }

  std::iota(d_buffers[0].begin(), d_buffers[0].end(), 0);
  RegisterCfg cfg{};
  int64_t d_cache_id = 0;
  EXPECT_EQ(llm_datadist_d.RegisterKvCache(kv_desc, d_tensor_addrs, cfg, d_cache_id), ge::SUCCESS);

  std::vector<uint64_t> p_tensor_addrs;
  auto p_buffers = std::vector<std::vector<int32_t>>(10, std::vector<int32_t>(4 * 16));
  for (uint32_t i = 0; i < kv_desc.num_tensors; ++i) {
    p_tensor_addrs.emplace_back(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p_buffers[i].data())));
  }

  std::iota(p_buffers[0].begin(), p_buffers[0].end(), 0);
  int64_t p_cache_id = 0;
  EXPECT_EQ(llm_datadist_p.RegisterKvCache(kv_desc, p_tensor_addrs, cfg, p_cache_id), ge::SUCCESS);

  // link
  ClusterInfo cluster_info;
  IpInfo ip_info;
  ip_info.ip = "127.0.0.1";
  ip_info.port = 26000;
  cluster_info.local_ip_infos = {ip_info};
  cluster_info.remote_ip_infos = {ip_info};
  std::vector<ge::Status> rets;
  EXPECT_EQ(llm_datadist_d.LinkLlmClusters({cluster_info}, rets), ge::SUCCESS);

  // pull cache
  CacheIndex cache_index{};
  cache_index.cluster_id = prompt_cluster_id;
  cache_index.cache_id = p_cache_id;
  Cache dst_cache{};
  dst_cache.cache_id = d_cache_id;
  dst_cache.cache_desc = kv_desc;
  KvCacheExtParam ext_param = {};

  EXPECT_EQ(llm_datadist_d.PullKvCache(cache_index, dst_cache, 0, -1, ext_param), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_d.PullKvBlocks(cache_index, dst_cache, {0U, 1U}, {0U, 1U}, ext_param), ge::SUCCESS);

  ext_param.src_layer_range = std::make_pair(0, 1);
  ext_param.dst_layer_range = std::make_pair(0, 1);
  EXPECT_EQ(llm_datadist_d.PullKvCache(cache_index, dst_cache, 0, -1, ext_param), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_d.PullKvBlocks(cache_index, dst_cache, {0U, 1U}, {0U, 1U}, ext_param), ge::SUCCESS);

  ext_param.tensor_num_per_layer = 1;
  EXPECT_EQ(llm_datadist_d.PullKvCache(cache_index, dst_cache, 0, -1, ext_param), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_d.PullKvBlocks(cache_index, dst_cache, {0U, 1U}, {0U, 1U}, ext_param), ge::SUCCESS);

  ext_param.tensor_num_per_layer = 2;
  EXPECT_EQ(llm_datadist_d.PullKvCache(cache_index, dst_cache, 0, -1, ext_param), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_d.PullKvBlocks(cache_index, dst_cache, {0U, 1U}, {0U, 1U}, ext_param), ge::SUCCESS);

  ext_param.tensor_num_per_layer = 7;
  EXPECT_NE(llm_datadist_d.PullKvCache(cache_index, dst_cache, 0, -1, ext_param), ge::SUCCESS);
  EXPECT_NE(llm_datadist_d.PullKvBlocks(cache_index, dst_cache, {0U, 1U}, {0U, 1U}, ext_param), ge::SUCCESS);

  ext_param.tensor_num_per_layer = 17;
  EXPECT_NE(llm_datadist_d.PullKvCache(cache_index, dst_cache, 0, -1, ext_param), ge::SUCCESS);
  EXPECT_NE(llm_datadist_d.PullKvBlocks(cache_index, dst_cache, {0U, 1U}, {0U, 1U}, ext_param), ge::SUCCESS);

  ext_param.tensor_num_per_layer = 2;
  ext_param.src_layer_range = std::make_pair(0, 1);
  ext_param.dst_layer_range = std::make_pair(0, 2);
  EXPECT_NE(llm_datadist_d.PullKvCache(cache_index, dst_cache, 0, -1, ext_param), ge::SUCCESS);
  EXPECT_NE(llm_datadist_d.PullKvBlocks(cache_index, dst_cache, {0U, 1U}, {0U, 1U}, ext_param), ge::SUCCESS);

  ext_param.dst_layer_range = std::make_pair(0, 1);
  ext_param.src_layer_range = std::make_pair(-2, 1);
  EXPECT_NE(llm_datadist_d.PullKvCache(cache_index, dst_cache, 0, -1, ext_param), ge::SUCCESS);
  EXPECT_NE(llm_datadist_d.PullKvBlocks(cache_index, dst_cache, {0U, 1U}, {0U, 1U}, ext_param), ge::SUCCESS);

  EXPECT_EQ(llm_datadist_d.UnlinkLlmClusters({cluster_info}, rets), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_p.UnregisterKvCache(p_cache_id), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_d.UnregisterKvCache(d_cache_id), ge::SUCCESS);
  llm_datadist_p.Finalize();
  llm_datadist_d.Finalize();
}
}  // namespace llm_datadist
