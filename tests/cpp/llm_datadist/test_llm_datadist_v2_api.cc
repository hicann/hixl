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
#include "llm_datadist/llm_error_codes.h"
#include "dlog_pub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"

using namespace std;
using namespace ge;
using namespace ::testing;
using ::testing::Invoke;
using ::testing::Mock;

namespace llm {
class HcclApiPrepareStub : public HcclApiStub {
  public:
  MOCK_METHOD3(HcclCommPrepare, HcclResult(HcclComm, HcclPrepareConfig *, int32_t));
};
}
namespace llm_datadist {
class LlmDataDistSTest : public ::testing::Test {
 protected:
  // 在测试类中设置一些准备工作，如果需要的话
  void SetUp() override {
    llm::MockMmpaForHcclApi::Install();
    llm::AutoCommResRuntimeMock::Install();
  }
  void TearDown() override {
    llm::HcclAdapter::GetInstance().Finalize();
    llm::AutoCommResRuntimeMock::Reset();
    llm::MockMmpaForHcclApi::Reset();
  }
};

namespace {
void TestPullKv(LlmDataDist &llm_datadist_p, LlmDataDist &llm_datadist_d) {
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
  ClusterInfo cluster_info{1, 0, {{"127.0.0.1", 26000}}, {{"127.0.0.1", 26000}}};
  std::vector<ge::Status> rets;
  EXPECT_EQ(llm_datadist_d.LinkLlmClusters({cluster_info}, rets), ge::SUCCESS);

  // pull cache
  CacheIndex cache_index{};
  cache_index.cluster_id = 1U;
  cache_index.cache_id = p_cache_id;
  Cache dst_cache{};
  dst_cache.cache_id = d_cache_id;
  EXPECT_EQ(llm_datadist_d.PullKvCache(cache_index, dst_cache), ge::SUCCESS);

  EXPECT_EQ(llm_datadist_d.UnlinkLlmClusters({cluster_info}, rets), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_p.UnregisterKvCache(p_cache_id), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_d.UnregisterKvCache(d_cache_id), ge::SUCCESS);
}
}

TEST_F(LlmDataDistSTest, RegisterKvCache) {
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

TEST_F(LlmDataDistSTest, UnregisterKvCache) {
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

TEST_F(LlmDataDistSTest, LinkAndUnlinkLlmClustersA2) {
  uint64_t prompt_cluster_id = 1U;
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

  uint64_t decode_cluster_id = 2U;
  LlmDataDist llm_datadist_d(decode_cluster_id, LlmRole::kDecoder);
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

TEST_F(LlmDataDistSTest, LinkAndUnlinkLlmClustersA3) {
  uint64_t prompt_cluster_id = 1U;
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

  uint64_t decode_cluster_id = 2U;
  LlmDataDist llm_datadist_d(decode_cluster_id, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options_d;
  options_d[llm_datadist::OPTION_DEVICE_ID] = "1";
  options_d["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "1",
          "super_device_id": "1",
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

  EXPECT_EQ(llm_datadist_d.Initialize(options_d), SUCCESS);
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

TEST_F(LlmDataDistSTest, MultiLinkAndUnlink) {
  uint64_t prompt_cluster_id = 1U;
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

  uint64_t decode_cluster_id = 2U;
  LlmDataDist llm_datadist_d(decode_cluster_id, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options_d;
  options_d[llm_datadist::OPTION_DEVICE_ID] = "1";
  options_d[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26001";
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

  uint64_t prompt2_cluster_id = 3U;
  LlmDataDist llm_datadist_p2(prompt2_cluster_id, LlmRole::kPrompt);
  std::map<AscendString, AscendString> options_p2;
  options_p2[llm_datadist::OPTION_DEVICE_ID] = "2";
  options_p2["llm.LocalCommRes"] = R"(
    {
      "server_count": "1",
      "server_list": [{
        "device": [{
          "device_id": "2",
          "device_ip": "1.1.1.3"
        }],
        "server_id": "127.0.0.1"
      }],
      "status": "completed",
      "version": "1.0"
    }
    )";
  EXPECT_EQ(llm_datadist_p2.Initialize(options_p2), SUCCESS);

  // d1 -> p0
  ClusterInfo cluster_info;
  IpInfo ip_info;
  ip_info.ip = "127.0.0.1";
  ip_info.port = 26000;
  cluster_info.local_ip_infos = {ip_info};
  cluster_info.remote_ip_infos = {ip_info};
  std::vector<ge::Status> rets;
  EXPECT_EQ(llm_datadist_d.LinkLlmClusters({cluster_info}, rets), ge::SUCCESS);
  // p2 -> d1
  ClusterInfo cluster_info2;
  IpInfo ip_info2;
  ip_info2.ip = "127.0.0.1";
  ip_info2.port = 26001;
  cluster_info2.local_ip_infos = {ip_info2};
  cluster_info2.remote_ip_infos = {ip_info2};
  EXPECT_EQ(llm_datadist_p2.LinkLlmClusters({cluster_info2}, rets), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_d.UnlinkLlmClusters({cluster_info}, rets), ge::SUCCESS);
  EXPECT_EQ(llm_datadist_p2.UnlinkLlmClusters({cluster_info2}, rets), ge::SUCCESS);
  llm_datadist_p.Finalize();
  llm_datadist_p2.Finalize();
  llm_datadist_d.Finalize();
}

TEST_F(LlmDataDistSTest, TestAutoLocalCommResA2) {
  class AutoCommResV1RuntimeMock : public llm::AutoCommResRuntimeMock {
   public:
    const char* aclrtGetSocName() override {
      return "Ascend910B1";
    }
  };
  llm::AclRuntimeStub::SetInstance(std::make_shared<AutoCommResV1RuntimeMock>());
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

TEST_F(LlmDataDistSTest, TestAutoLocalCommResA3) {
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

TEST_F(LlmDataDistSTest, TestUseHixlBackendA3) {
  LlmDataDist llm_datadist_p(1U, LlmRole::kPrompt);
  std::map<AscendString, AscendString> options_p;
  options_p[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26000";
  options_p[llm_datadist::OPTION_DEVICE_ID] = "0";
  options_p[llm_datadist::OPTION_TRANSFER_BACKEND] = "hixl";

  llm::AutoCommResRuntimeMock::SetDevice(0);
  EXPECT_EQ(llm_datadist_p.Initialize(options_p), SUCCESS);

  LlmDataDist llm_datadist_d(2U, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options_d;
  options_d[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26001";
  options_d[llm_datadist::OPTION_DEVICE_ID] = "1";
  options_d[llm_datadist::OPTION_TRANSFER_BACKEND] = "hixl";

  llm::AutoCommResRuntimeMock::SetDevice(1);
  EXPECT_EQ(llm_datadist_d.Initialize(options_d), SUCCESS);

  TestPullKv(llm_datadist_p, llm_datadist_d);
  llm_datadist_p.Finalize();
  llm_datadist_d.Finalize();
}

TEST_F(LlmDataDistSTest, TestUseHixlBackendA5) {
  LlmDataDist llm_datadist_p(1U, LlmRole::kPrompt);
  std::map<AscendString, AscendString> options_p;
  options_p[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26000";
  options_p[llm_datadist::OPTION_DEVICE_ID] = "0";
  options_p[llm_datadist::OPTION_TRANSFER_BACKEND] = "hixl";
  options_p[llm_datadist::OPTION_LOCAL_COMM_RES] = R"(
  {
      "net_instance_id": "superpod1_1",
      "endpoint_list": [
          {
              "protocol": "roce",
              "comm_id": "1.0.0.1",
              "placement": "host"
          }
      ],
      "version": "1.3"
  }
  )";

  llm::AutoCommResRuntimeMock::SetDevice(0);
  EXPECT_EQ(llm_datadist_p.Initialize(options_p), SUCCESS);
  LlmDataDist llm_datadist_d(2U, LlmRole::kDecoder);
  std::map<AscendString, AscendString> options_d;
  options_d[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26001";
  options_d[llm_datadist::OPTION_DEVICE_ID] = "1";
  options_d[llm_datadist::OPTION_TRANSFER_BACKEND] = "hixl";
  options_d[llm_datadist::OPTION_LOCAL_COMM_RES] = R"(
  {
      "net_instance_id": "superpod2_1",
      "endpoint_list": [
          {
              "protocol": "roce",
              "comm_id": "1.0.0.2",
              "placement": "host"
          }
      ],
      "version": "1.3"
  }
  )";

  llm::AutoCommResRuntimeMock::SetDevice(1);
  EXPECT_EQ(llm_datadist_d.Initialize(options_d), SUCCESS);
  TestPullKv(llm_datadist_p, llm_datadist_d);
  llm_datadist_p.Finalize();
  llm_datadist_d.Finalize();
}

TEST_F(LlmDataDistSTest, TestAutoLocalCommResMix) {
  LlmDataDist llm_datadist_p(1U, LlmRole::kPrompt);
  std::map<AscendString, AscendString> options_p;
  options_p[llm_datadist::OPTION_LISTEN_IP_INFO] = "127.0.0.1:26000";
  options_p[llm_datadist::OPTION_DEVICE_ID] = "0";

  EXPECT_EQ(llm_datadist_p.Initialize(options_p), SUCCESS);

  class AutoCommResV1RuntimeMock : public llm::AutoCommResRuntimeMock {
   public:
    const char* aclrtGetSocName() override {
      return "Ascend910B1";
    }
  };
  llm::AclRuntimeStub::SetInstance(std::make_shared<AutoCommResV1RuntimeMock>());

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

TEST_F(LlmDataDistSTest, TestLocalCommResInvalidVersion) {
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

TEST_F(LlmDataDistSTest, LinkLlmClustersFailed) {
  auto hccl_stub = std::make_unique<llm::HcclApiPrepareStub>();
  EXPECT_CALL(*hccl_stub, HcclCommPrepare).WillRepeatedly(Return(HCCL_E_TIMEOUT));
  llm::HcclApiStub::SetStub(std::move(hccl_stub));

  uint64_t prompt_cluster_id = 1U;
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

  uint64_t decode_cluster_id = 2U;
  LlmDataDist llm_datadist_d(decode_cluster_id, LlmRole::kDecoder);
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
  ClusterInfo cluster_info;
  IpInfo ip_info;
  ip_info.ip = "127.0.0.1";
  ip_info.port = 26000;
  cluster_info.local_ip_infos = {ip_info};
  cluster_info.remote_ip_infos = {ip_info};
  std::vector<ge::Status> rets;
  EXPECT_EQ(llm_datadist_d.LinkLlmClusters({cluster_info}, rets), ge::LLM_TIMEOUT);
  llm_datadist_p.Finalize();
  llm_datadist_d.Finalize();
}

TEST_F(LlmDataDistSTest, LocalCommResSwitchRoleSuccess) {
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

TEST_F(LlmDataDistSTest, CacheNotSupported) {
  uint64_t prompt_cluster_id = 0U;
  LlmDataDist llm_datadist(prompt_cluster_id, LlmRole::kPrompt);
  std::map<AscendString, AscendString> options;
  options[llm_datadist::OPTION_DEVICE_ID] = "1";
  options["llm.LocalCommRes"] = "mock";
  EXPECT_EQ(llm_datadist.Initialize(options), SUCCESS);

  CacheDesc kv_desc{};
  kv_desc.num_tensors = 80;
  kv_desc.data_type = DT_FLOAT16;
  kv_desc.shape = {1, 256};
  Cache cached_tensors;
  EXPECT_EQ(llm_datadist.AllocateCache(kv_desc, cached_tensors), LLM_FEATURE_NOT_ENABLED);
  EXPECT_EQ(llm_datadist.DeallocateCache(0), LLM_FEATURE_NOT_ENABLED);
  EXPECT_EQ(llm_datadist.CopyKvCache({}, {}, 0, 1), LLM_FEATURE_NOT_ENABLED);
  EXPECT_EQ(llm_datadist.CopyKvBlocks({}, {}, {0}, {{0}}), LLM_FEATURE_NOT_ENABLED);
  llm_datadist.Finalize();
}

TEST_F(LlmDataDistSTest, TestPushWithRangeAndTensorNumV2) {
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

  Cache src_cache{};
  src_cache.cache_id = p_cache_id;
  src_cache.cache_desc = kv_desc;

  // prompt push cache to decoder
  CacheIndex dst_cache_index{decoder_cluster_id, d_cache_id};

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

  ext_param.tensor_num_per_layer = 3;
  EXPECT_NE(llm_datadist_p.PushKvBlocks(src_cache,
                                      dst_cache_index,
                                      {0U, 1U},
                                      {0U, 1U},
                                      ext_param), ge::SUCCESS);
  EXPECT_NE(llm_datadist_p.PushKvCache(src_cache, dst_cache_index, 0, -1, ext_param), ge::SUCCESS);

  ext_param.tensor_num_per_layer = 2;
  // test invalid range and tensor num
  ext_param.src_layer_range = std::make_pair(-2, 0);
  ext_param.dst_layer_range = std::make_pair(-2, 0);
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

TEST_F(LlmDataDistSTest, TestPullWithRangeAndTensorNumV2) {
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

  ext_param.tensor_num_per_layer = 3;
  EXPECT_NE(llm_datadist_d.PullKvCache(cache_index, dst_cache, 0, -1, ext_param), ge::SUCCESS);
  EXPECT_NE(llm_datadist_d.PullKvBlocks(cache_index, dst_cache, {0U, 1U}, {0U, 1U}, ext_param), ge::SUCCESS);

  ext_param.tensor_num_per_layer = 2;
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
