/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdlib>
#include <cstdio>
#include <limits>
#include <memory>
#include <unistd.h>
#include <vector>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "adxl/adxl_engine.h"
#include "adxl/channel_manager.h"
#include "adxl/statistic_manager.h"
#include "engine/engine_factory.h"
#include "engine/hixl_options.h"
#include "engine/hixl_engine.h"
#include "engine/comm_engine.h"
#include "dlog_pub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "adxl_test_helpers.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"
#include "adxl_test_helpers.h"

using namespace std;
using namespace llm;
using namespace ::testing;
using ::testing::Invoke;
using ::testing::Mock;
namespace adxl {
namespace {
constexpr char kEngine1Id[] = "127.0.0.1";
constexpr char kEngine1PortId[] = "127.0.0.1:28100";
constexpr char kEngine2Id[] = "127.0.0.1:28101";
constexpr char kEngine3Id[] = "127.0.0.1:28102";

std::map<AscendString, AscendString> BuildHixlCsOptions(const std::string &ip) {
  std::map<AscendString, AscendString> options;
  options[OPTION_LOCAL_COMM_RES] = AscendString((R"(
    {
        "net_instance_id": "superpod",
        "endpoint_list": [
            {
                "protocol": "roce",
                "comm_id": ")" + ip + R"(",
                "placement": "host"
            },
            {
                "protocol": "ub_ctp",
                "comm_id": "000000000000000000000000c0a80463",
                "placement": "device",
                "dst_eid": "000000000000000000000000c0a80563"
            }
        ],
        "version": "1.3"
    }
    )")
                                                    .c_str());
  return options;
}
}  // namespace

class AdxlEngineUTest : public ::testing::Test {
 protected:
  void ClearStatisticChannels() {
    auto &sm = StatisticManager::GetInstance();
    for (const char *peer_id : {kEngine1Id, kEngine1PortId, kEngine2Id, kEngine3Id}) {
      sm.RemoveStatisticChannel(peer_id, true);
      sm.RemoveStatisticChannel(peer_id, false);
    }
  }

  // 在测试类中设置一些准备工作，如果需要的话
  void SetUp() override {
    ClearStatisticChannels();
    llm::MockMmpaForHcclApi::Install();
    llm::AutoCommResRuntimeMock::Install();
    llm::HcclAdapter::GetInstance().Initialize();
  }
  // 在测试类中进行清理工作，如果需要的话
  void TearDown() override {
    ClearStatisticChannels();
    llm::HcclAdapter::GetInstance().Finalize();
    llm::AutoCommResRuntimeMock::Reset();
    llm::MockMmpaForHcclApi::Reset();
  }
  // 初始化两个 AdxlEngine 引擎
  void SetupEngines(AdxlEngine &engine1, AdxlEngine &engine2) {
    llm::AutoCommResRuntimeMock::SetDevice(0);
    std::map<AscendString, AscendString> options1;
    options1[OPTION_RDMA_TRAFFIC_CLASS] = "4";
    options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
    options1[OPTION_BUFFER_POOL] = "0:0";
    EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

    llm::AutoCommResRuntimeMock::SetDevice(1);
    std::map<AscendString, AscendString> options2;
    EXPECT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);
  }
  // 注册 int32 类型的内存
  void RegisterInt32Mem(AdxlEngine &engine, int32_t *ptr, MemHandle &handle) {
    adxl::MemDesc mem_desc{};
    mem_desc.addr = reinterpret_cast<uintptr_t>(ptr);
    mem_desc.len = sizeof(int32_t);
    EXPECT_EQ(engine.RegisterMem(mem_desc, MEM_DEVICE, handle), SUCCESS);
  }

  struct Int32MemPair {
    int32_t src = 1;
    int32_t dst = 2;
    MemHandle handle1 = nullptr;
    MemHandle handle2 = nullptr;
  };

  void SetupEnginesOnPorts(AdxlEngine &engine1, AdxlEngine &engine2) {
    llm::AutoCommResRuntimeMock::SetDevice(0);
    std::map<AscendString, AscendString> options1;
    EXPECT_EQ(engine1.Initialize("127.0.0.1:28100", options1), SUCCESS);
    llm::AutoCommResRuntimeMock::SetDevice(1);
    std::map<AscendString, AscendString> options2;
    EXPECT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);
  }

  void SetupBufferPoolEngines(AdxlEngine &engine1, AdxlEngine &engine2) {
    llm::AutoCommResRuntimeMock::SetDevice(0);
    std::map<AscendString, AscendString> options1;
    options1[OPTION_RDMA_TRAFFIC_CLASS] = "4";
    options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
    options1["adxl.BufferPool"] = "4:8";
    EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);
    llm::AutoCommResRuntimeMock::SetDevice(1);
    std::map<AscendString, AscendString> options2;
    options2["adxl.BufferPool"] = "4:8";
    EXPECT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);
  }

  Int32MemPair SetupInt32ConnectedEngines(AdxlEngine &engine1, AdxlEngine &engine2) {
    SetupEngines(engine1, engine2);
    Int32MemPair mem;
    RegisterInt32Mem(engine1, &mem.src, mem.handle1);
    RegisterInt32Mem(engine2, &mem.dst, mem.handle2);
    EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);
    return mem;
  }

  TransferOpDesc MakeInt32TransferDesc(int32_t &src, int32_t &dst) {
    return TransferOpDesc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  }

  std::vector<TransferOpDesc> BuildBlockTransferDescs(uintptr_t src_base, uintptr_t dst_base, size_t block_size,
                                                      size_t block_num) {
    std::vector<TransferOpDesc> descs;
    descs.reserve(block_num);
    for (size_t i = 0; i < block_num; ++i) {
      descs.emplace_back(TransferOpDesc{src_base + i * block_size, dst_base + i * block_size, block_size});
    }
    return descs;
  }

  void ExpectAllElementsEq(const std::vector<int8_t> &data, int8_t expected) {
    for (const auto val : data) {
      EXPECT_EQ(val, expected);
    }
  }

  void SendTestNotifies(AdxlEngine &engine, const char *peer, int count) {
    for (int i = 0; i < count; ++i) {
      NotifyDesc notify;
      notify.name = AscendString(("test_notify" + std::to_string(i)).c_str());
      notify.notify_msg = AscendString(("message " + std::to_string(i)).c_str());
      EXPECT_EQ(engine.SendNotify(peer, notify), SUCCESS);
    }
  }

  void ExpectTestNotifiesContent(const std::vector<NotifyDesc> &notifies) {
    for (size_t i = 0; i < notifies.size(); ++i) {
      EXPECT_EQ(std::string(notifies[i].name.GetString()), "test_notify" + std::to_string(i));
      EXPECT_EQ(std::string(notifies[i].notify_msg.GetString()), "message " + std::to_string(i));
    }
  }

  std::vector<NotifyDesc> GetAndExpectNotifies(AdxlEngine &engine, size_t expected_count) {
    std::vector<NotifyDesc> notifies;
    EXPECT_EQ(engine.GetNotifies(notifies), SUCCESS);
    EXPECT_EQ(notifies.size(), expected_count);
    ExpectTestNotifiesContent(notifies);
    return notifies;
  }
  // 清理资源
  void CleanupEngine(AdxlEngine &engine1, AdxlEngine &engine2, MemHandle &handle1, MemHandle &handle2) {
    EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
    EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
    EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
    engine1.Finalize();
    engine2.Finalize();
  }

  void ExpectConnectStatistic(const std::string &channel_id, bool expect_hccl, bool expect_bind_mem, bool expect_tcp) {
    const auto snapshot = adxl::StatisticManager::GetInstance().GetStatisticInfoSnapshot(channel_id);
    EXPECT_GT(snapshot.connect_statistic_info.connect_total.times, 0UL);
    if (expect_tcp) {
      EXPECT_GT(snapshot.connect_statistic_info.tcp_connect.times, 0UL);
    } else {
      EXPECT_EQ(snapshot.connect_statistic_info.tcp_connect.times, 0UL);
    }
    if (expect_hccl) {
      EXPECT_GT(snapshot.connect_statistic_info.hccl_total.times, 0UL);
      EXPECT_GT(snapshot.connect_statistic_info.hccl_comm_init.times, 0UL);
      EXPECT_GT(snapshot.connect_statistic_info.hccl_comm_prepare.times, 0UL);
      if (expect_bind_mem) {
        EXPECT_GT(snapshot.connect_statistic_info.hccl_comm_bind_mem.times, 0UL);
      }
    } else {
      EXPECT_EQ(snapshot.connect_statistic_info.hccl_total.times, 0UL);
      EXPECT_EQ(snapshot.connect_statistic_info.hccl_comm_init.times, 0UL);
      EXPECT_EQ(snapshot.connect_statistic_info.hccl_comm_bind_mem.times, 0UL);
      EXPECT_EQ(snapshot.connect_statistic_info.hccl_comm_prepare.times, 0UL);
    }
  }
};

TEST_F(AdxlEngineUTest, TestEngineFactoryFallbackToAdxlEngineWithoutLocalCommRes) {
  std::map<AscendString, AscendString> options;
  options[OPTION_RDMA_TRAFFIC_CLASS] = "4";
  options[OPTION_RDMA_SERVICE_LEVEL] = "1";

  hixl::HixlOptions parsed_options;
  auto engine = hixl::EngineFactory::CreateEngine("127.0.0.1", options, parsed_options);
  ASSERT_NE(engine, nullptr);
  EXPECT_NE(dynamic_cast<hixl::CommEngine *>(engine.get()), nullptr);
  EXPECT_EQ(dynamic_cast<hixl::HixlEngine *>(engine.get()), nullptr);
}

TEST_F(AdxlEngineUTest, TestEngineFactoryUseHixlEngineWithLocalCommRes) {
  auto opts = BuildHixlCsOptions("127.0.0.1");
  hixl::HixlOptions parsed_options;
  auto engine = hixl::EngineFactory::CreateEngine("127.0.0.1", opts, parsed_options);
  ASSERT_NE(engine, nullptr);
  EXPECT_NE(dynamic_cast<hixl::HixlEngine *>(engine.get()), nullptr);
  EXPECT_EQ(dynamic_cast<hixl::CommEngine *>(engine.get()), nullptr);
}

TEST_F(AdxlEngineUTest, TestEngineFactoryUseHixlEngineWhenUboeNotFirstInProtocolDesc) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"(
    {
      "comm_resource_config.protocol_desc": ["roce:device", "uboe:device"]
    }
  )";

  hixl::HixlOptions parsed_options;
  auto engine = hixl::EngineFactory::CreateEngine("127.0.0.1", options, parsed_options);
  ASSERT_NE(engine, nullptr);
  EXPECT_NE(dynamic_cast<hixl::HixlEngine *>(engine.get()), nullptr);
  EXPECT_EQ(dynamic_cast<hixl::CommEngine *>(engine.get()), nullptr);
}

TEST_F(AdxlEngineUTest, TestAdxlEngine) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "4";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);

  int32_t src = 1;
  adxl::MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(&src);
  src_mem.len = sizeof(int32_t);
  MemHandle handle1 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(src_mem, MEM_DEVICE, handle1), SUCCESS);

  int32_t dst = 2;
  adxl::MemDesc dst_mem{};
  dst_mem.addr = reinterpret_cast<uintptr_t>(&dst);
  dst_mem.len = sizeof(int32_t);
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(dst_mem, MEM_DEVICE, handle2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", READ, {desc}), SUCCESS);
  EXPECT_EQ(src, 2);
  src = 1;
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", WRITE, {desc}), SUCCESS);
  EXPECT_EQ(dst, 1);
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);

  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineUTest, TestConnectStatisticForDirectTransfer) {
  AdxlEngine engine1;
  AdxlEngine engine2;
  auto mem = SetupInt32ConnectedEngines(engine1, engine2);
  ExpectConnectStatistic(StatisticManager::GetClientStatisticChannelId("127.0.0.1:28101"), true, true, true);
  ExpectConnectStatistic(StatisticManager::GetServerStatisticChannelId("127.0.0.1"), true, true, false);
  CleanupEngine(engine1, engine2, mem.handle1, mem.handle2);
}

TEST_F(AdxlEngineUTest, TestConnectStatisticForBufferMode) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_BUFFER_POOL] = "4:8";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:28100", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);
  int32_t src = 1;
  MemHandle handle1 = nullptr;
  RegisterInt32Mem(engine1, &src, handle1);
  int32_t dst = 2;
  MemHandle handle2 = nullptr;
  RegisterInt32Mem(engine2, &dst, handle2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);
  ExpectConnectStatistic(StatisticManager::GetClientStatisticChannelId("127.0.0.1:28101"), true, true, true);
  ExpectConnectStatistic(StatisticManager::GetServerStatisticChannelId("127.0.0.1:28100"), true, true, false);
  CleanupEngine(engine1, engine2, handle1, handle2);
}

TEST_F(AdxlEngineUTest, TestServerStatisticUsesPeerChannelId) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:28100", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(2);
  AdxlEngine engine3;
  std::map<AscendString, AscendString> options3;
  EXPECT_EQ(engine3.Initialize("127.0.0.1:28102", options3), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);
  EXPECT_EQ(engine3.Connect("127.0.0.1:28101"), SUCCESS);

  const auto server_snapshot1 = StatisticManager::GetInstance().GetStatisticInfoSnapshot(
      StatisticManager::GetServerStatisticChannelId("127.0.0.1:28100"));
  const auto server_snapshot2 = StatisticManager::GetInstance().GetStatisticInfoSnapshot(
      StatisticManager::GetServerStatisticChannelId("127.0.0.1:28102"));
  const auto aggregated_snapshot = StatisticManager::GetInstance().GetStatisticInfoSnapshot(
      StatisticManager::GetServerStatisticChannelId("127.0.0.1:28101"));
  EXPECT_GT(server_snapshot1.connect_statistic_info.connect_total.times, 0UL);
  EXPECT_GT(server_snapshot2.connect_statistic_info.connect_total.times, 0UL);
  EXPECT_EQ(aggregated_snapshot.connect_statistic_info.connect_total.times, 0UL);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
  EXPECT_EQ(engine3.Disconnect("127.0.0.1:28101"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
  engine3.Finalize();
}

TEST_F(AdxlEngineUTest, TestEngineFactoryCreateEngineFailedWhenLocalCommResJsonInvalid) {
  std::map<AscendString, AscendString> options;
  options[OPTION_LOCAL_COMM_RES] = "{invalid json}";

  hixl::HixlOptions parsed_options;
  auto engine = hixl::EngineFactory::CreateEngine("127.0.0.1", options, parsed_options);
  EXPECT_EQ(engine, nullptr);
}

TEST_F(AdxlEngineUTest, TestAdxlEngineApisReturnFailedBeforeInitialize) {
  AdxlEngine engine;
  int32_t src = 1;
  MemDesc mem_desc{};
  mem_desc.addr = reinterpret_cast<uintptr_t>(&src);
  mem_desc.len = sizeof(int32_t);
  MemHandle handle = nullptr;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&src), sizeof(int32_t)};
  TransferReq req = nullptr;
  NotifyDesc notify;
  notify.name = AscendString("test_notify");
  notify.notify_msg = AscendString("message");
  std::vector<NotifyDesc> notifies;

  EXPECT_EQ(engine.RegisterMem(mem_desc, MEM_DEVICE, handle), FAILED);
  EXPECT_EQ(engine.DeregisterMem(reinterpret_cast<MemHandle>(0x1)), FAILED);
  EXPECT_EQ(engine.Connect("127.0.0.1:28101"), FAILED);
  EXPECT_EQ(engine.Disconnect("127.0.0.1:28101"), FAILED);
  EXPECT_EQ(engine.TransferSync("127.0.0.1:28101", WRITE, {desc}), FAILED);
  EXPECT_EQ(engine.TransferAsync("127.0.0.1:28101", WRITE, {desc}, {}, req), FAILED);
  EXPECT_EQ(engine.SendNotify("127.0.0.1:28101", notify), FAILED);
  EXPECT_EQ(engine.GetNotifies(notifies), FAILED);
}

TEST_F(AdxlEngineUTest, TestAdxlEngineInitFailed) {
  AdxlEngine engine;
  std::map<AscendString, AscendString> options;
  // invalid ip
  EXPECT_EQ(engine.Initialize("ad.0.0.1:28100", options), PARAM_INVALID);
}

TEST_F(AdxlEngineUTest, TestConnectNotListenFailed) {
  AdxlEngine engine;
  std::map<AscendString, AscendString> options;
  EXPECT_EQ(engine.Initialize("127.0.0.1:28100", options), SUCCESS);
  // not listen
  EXPECT_EQ(engine.Connect("127.0.0.1:28101"), FAILED);
}

TEST_F(AdxlEngineUTest, TestAlreadyConnectedFailed) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), ALREADY_CONNECTED);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineUTest, TestDeregisterUnregisterMem) {
  AdxlEngine engine;
  std::map<AscendString, AscendString> options;
  EXPECT_EQ(engine.Initialize("127.0.0.1", options), SUCCESS);
  MemHandle handle = (MemHandle)0x100;
  // deregister unregister mem
  EXPECT_EQ(engine.DeregisterMem(handle), SUCCESS);
  engine.Finalize();
}

TEST_F(AdxlEngineUTest, TestHeartbeat) {
  ChannelManager::SetHeartbeatWaitTime(10);  // 10ms
  CommChannel::SetHeartbeatTimeout(50);      // 50ms
  AdxlEngine engine1;
  llm::AutoCommResRuntimeMock::SetDevice(0);
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), ALREADY_CONNECTED);
  std::this_thread::sleep_for(std::chrono::milliseconds(60));  // wait heartbeat process
  int32_t src = 1;
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", READ, {desc}), SUCCESS);
  EXPECT_EQ(src, 2);
  // not disconnect, force finalize
  engine1.Finalize();

  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine3;
  EXPECT_EQ(engine3.Initialize("127.0.0.1", options1), SUCCESS);  // use same key with engine1
  EXPECT_EQ(engine3.Connect("127.0.0.1:28101"), SUCCESS);
  // not disconnect, force finalize
  engine3.Finalize();
  std::this_thread::sleep_for(std::chrono::milliseconds(60));  // wait server:engine2 clear client:engine3
  engine2.Finalize();
}

TEST_F(AdxlEngineUTest, TestAdxlEngineH2HWithBuffer) {
  AdxlEngine engine1;
  AdxlEngine engine2;
  SetupBufferPoolEngines(engine1, engine2);

  size_t size = 16 * 1024 * 1024;
  std::vector<int8_t> src(size, 1);
  std::vector<int8_t> dst(size, 2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(dst.data()), size};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", READ, {desc}), SUCCESS);
  ExpectAllElementsEq(src, 2);
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", WRITE, {desc}), SUCCESS);
  ExpectAllElementsEq(dst, 1);

  dst.assign(size, 2);
  size_t block_size = 256 * 1024;
  size_t block_num = 4 * 16;
  auto descs = BuildBlockTransferDescs(reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(dst.data()),
                                       block_size, block_num);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", READ, descs), SUCCESS);
  ExpectAllElementsEq(src, 2);
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", WRITE, descs), SUCCESS);
  ExpectAllElementsEq(dst, 1);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineUTest, TestAdxlEngineRD2HWithBuffer) {
  AdxlEngine engine1;
  AdxlEngine engine2;
  SetupBufferPoolEngines(engine1, engine2);

  size_t size = 16 * 1024 * 1024;
  std::vector<int8_t> src(size, 1);
  std::vector<int8_t> dst(size, 2);
  MemHandle handle2 = nullptr;
  test_helpers::RegisterDeviceBufferMem(engine2, dst, handle2);

  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(dst.data()), size};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", READ, {desc}), SUCCESS);
  ExpectAllElementsEq(src, 2);

  dst.assign(size, 2);
  size_t block_size = 256 * 1024;
  size_t block_num = 4 * 16;
  auto descs = BuildBlockTransferDescs(reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(dst.data()),
                                       block_size, block_num);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", READ, descs), SUCCESS);
  ExpectAllElementsEq(src, 2);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineUTest, TestAdxlEngineTransferAsync) {
  AdxlEngine engine1;
  AdxlEngine engine2;
  auto mem = SetupInt32ConnectedEngines(engine1, engine2);
  TransferOpDesc desc = MakeInt32TransferDesc(mem.src, mem.dst);
  TransferReq req = nullptr;
  ASSERT_EQ(engine1.TransferAsync("127.0.0.1:28101", READ, {desc}, {}, req), SUCCESS);

  constexpr int kMaxPollTimes = 10;
  constexpr int kPollInterval = 10;
  TransferStatus status = TransferStatus::WAITING;
  for (int i = 0; i < kMaxPollTimes && status == TransferStatus::WAITING; i++) {
    engine1.GetTransferStatus(req, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollInterval));
  }
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(mem.src, 2);
  // 测试多次查找
  EXPECT_EQ(engine1.GetTransferStatus(req, status), PARAM_INVALID);
  EXPECT_EQ(status, TransferStatus::FAILED);

  mem.src = 1;
  ASSERT_EQ(engine1.TransferAsync("127.0.0.1:28101", WRITE, {desc}, {}, req), SUCCESS);
  status = TransferStatus::WAITING;
  for (int i = 0; i < kMaxPollTimes && status == TransferStatus::WAITING; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollInterval));
    EXPECT_EQ(engine1.GetTransferStatus(req, status), SUCCESS);
  }
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(mem.dst, 1);

  CleanupEngine(engine1, engine2, mem.handle1, mem.handle2);
}

TEST_F(AdxlEngineUTest, TestAdxlEngineTransferAsyncWithMultiThread) {
  AdxlEngine engine1;
  AdxlEngine engine2;
  auto mem = SetupInt32ConnectedEngines(engine1, engine2);
  TransferOpDesc desc = MakeInt32TransferDesc(mem.src, mem.dst);
  constexpr int kThreadCount = 20;
  constexpr int kPollInterval = 10;
  constexpr int kMaxWaitTime = 5;  // 5s
  TransferReq req_list[kThreadCount];
  std::vector<std::thread> async_threads;
  for (int i = 0; i < kThreadCount; i++) {
    async_threads.emplace_back(
        [&, i]() { EXPECT_EQ(engine1.TransferAsync("127.0.0.1:28101", WRITE, {desc}, {}, req_list[i]), SUCCESS); });
  }
  for (auto &t : async_threads) {
    t.join();
  }
  test_helpers::WaitForAllAsyncTransfers(engine1, req_list, kThreadCount, kPollInterval, kMaxWaitTime);
  CleanupEngine(engine1, engine2, mem.handle1, mem.handle2);
}

TEST_F(AdxlEngineUTest, TestAdxlEngineGetTransferStatusFalied) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);
  TransferReq req = nullptr;
  TransferStatus status;
  EXPECT_EQ(engine1.GetTransferStatus(req, status), FAILED);
  // 给 req 随机赋值一个地址
  constexpr size_t kFakeReqSize = 64;
  req = malloc(kFakeReqSize);
  EXPECT_EQ(engine1.GetTransferStatus(req, status), PARAM_INVALID);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
  free(req);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineUTest, TestAdxlGetTransferStatusWithInterrupt) {
  AdxlEngine engine1;
  AdxlEngine engine2;
  auto mem = SetupInt32ConnectedEngines(engine1, engine2);
  TransferOpDesc desc = MakeInt32TransferDesc(mem.src, mem.dst);
  TransferReq req = nullptr;
  EXPECT_EQ(engine1.TransferAsync("127.0.0.1:28101", WRITE, {desc}, {}, req), SUCCESS);
  engine1.Disconnect("127.0.0.1:28101");
  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(engine1.GetTransferStatus(req, status), NOT_CONNECTED);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineUTest, TestAdxlGetTransferStatusWithQueryEventFailed) {
  AdxlEngine engine1;
  AdxlEngine engine2;
  auto mem = SetupInt32ConnectedEngines(engine1, engine2);
  TransferOpDesc desc = MakeInt32TransferDesc(mem.src, mem.dst);
  TransferReq req = nullptr;
  EXPECT_EQ(engine1.TransferAsync("127.0.0.1:28101", WRITE, {desc}, {}, req), SUCCESS);
  TransferStatus status = TransferStatus::WAITING;
  TransferAsyncRuntimeMock instance;
  ;
  llm::AclRuntimeStub::Install(&instance);
  EXPECT_EQ(engine1.GetTransferStatus(req, status), FAILED);
  llm::AclRuntimeStub::UnInstall(&instance);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineUTest, TestAdxlEngineSendGetNotifies) {
  AdxlEngine engine1;
  AdxlEngine engine2;
  SetupEnginesOnPorts(engine1, engine2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);
  SendTestNotifies(engine1, "127.0.0.1:28101", 5);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  GetAndExpectNotifies(engine2, 5);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineUTest, TestAdxlEngineMultiGetNotifies) {
  AdxlEngine engine1;
  AdxlEngine engine2;
  SetupEnginesOnPorts(engine1, engine2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);
  SendTestNotifies(engine1, "127.0.0.1:28101", 5);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  GetAndExpectNotifies(engine2, 5);

  std::vector<NotifyDesc> notifies;
  EXPECT_EQ(engine2.GetNotifies(notifies), SUCCESS);
  EXPECT_EQ(notifies.size(), 0U);
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineUTest, TestAdxlEngineMultiSendNotifies) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:28100", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);
  // set mock device 2
  llm::AutoCommResRuntimeMock::SetDevice(2);
  AdxlEngine engine3;
  std::map<AscendString, AscendString> options3;
  EXPECT_EQ(engine3.Initialize("127.0.0.1:28102", options3), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);
  EXPECT_EQ(engine3.Connect("127.0.0.1:28101"), SUCCESS);
  // each engine send 5 notifies
  for (int i = 0; i < 5; ++i) {
    NotifyDesc notify;
    notify.name = AscendString(("test_notify" + std::to_string(i)).c_str());
    notify.notify_msg = AscendString(("message " + std::to_string(i)).c_str());
    EXPECT_EQ(engine1.SendNotify("127.0.0.1:28101", notify), SUCCESS);
    EXPECT_EQ(engine3.SendNotify("127.0.0.1:28101", notify), SUCCESS);
  }
  // sleep 100 ms then get notifies
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::vector<NotifyDesc> notifies;
  EXPECT_EQ(engine2.GetNotifies(notifies), SUCCESS);
  // should get 10 notifies
  EXPECT_EQ(notifies.size(), 10);

  notifies.clear();
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
  EXPECT_EQ(engine3.Disconnect("127.0.0.1:28101"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
  engine3.Finalize();
}

TEST_F(AdxlEngineUTest, TestAdxlEngineSendNotifyTimeout) {
  AdxlEngine engine1;
  AdxlEngine engine2;
  SetupEnginesOnPorts(engine1, engine2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);
  for (int i = 0; i < 5; ++i) {
    NotifyDesc notify;
    notify.name = AscendString(("test_notify" + std::to_string(i)).c_str());
    notify.notify_msg = AscendString(("message " + std::to_string(i)).c_str());
    EXPECT_EQ(engine1.SendNotify("127.0.0.1:28101", notify, 1), TIMEOUT);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::vector<NotifyDesc> notifies;
  EXPECT_EQ(engine2.GetNotifies(notifies), SUCCESS);
  EXPECT_EQ(notifies.size(), 0U);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineUTest, TestAdxlEngineSendNotifyNameTooLong) {
  AdxlEngine engine1;
  AdxlEngine engine2;
  SetupEnginesOnPorts(engine1, engine2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);

  NotifyDesc notify;
  std::string long_name(2000, 'a');
  notify.name = AscendString(long_name.c_str());
  notify.notify_msg = AscendString("short message");

  EXPECT_EQ(engine1.SendNotify("127.0.0.1:28101", notify), PARAM_INVALID);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}
TEST_F(AdxlEngineUTest, TestAdxlGetTransferStatusWithStreamSyncFailed) {
  AdxlEngine engine1;
  AdxlEngine engine2;
  auto mem = SetupInt32ConnectedEngines(engine1, engine2);
  TransferOpDesc desc = MakeInt32TransferDesc(mem.src, mem.dst);
  TransferReq req = nullptr;
  EXPECT_EQ(engine1.TransferAsync("127.0.0.1:28101", WRITE, {desc}, {}, req), SUCCESS);
  TransferStatus status = TransferStatus::WAITING;
  TransferAsyncSteamRuntimeMocak instance;
  ;
  llm::AclRuntimeStub::Install(&instance);
  EXPECT_EQ(engine1.GetTransferStatus(req, status), FAILED);
  llm::AclRuntimeStub::UnInstall(&instance);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineUTest, TestAdxlEngineSendNotifyMsgTooLong) {
  AdxlEngine engine1;
  AdxlEngine engine2;
  SetupEnginesOnPorts(engine1, engine2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);

  NotifyDesc notify;
  notify.name = AscendString("short name");
  std::string long_msg(2000, 'b');
  notify.notify_msg = AscendString(long_msg.c_str());

  EXPECT_EQ(engine1.SendNotify("127.0.0.1:28101", notify), PARAM_INVALID);
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineUTest, TestAdxlEngineAutoConnectEnabled) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_AUTO_CONNECT] = "1";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:28100", options1), SUCCESS);
  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);
  Int32MemPair mem;
  RegisterInt32Mem(engine1, &mem.src, mem.handle1);
  RegisterInt32Mem(engine2, &mem.dst, mem.handle2);
  TransferOpDesc desc = MakeInt32TransferDesc(mem.src, mem.dst);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", READ, {desc}), SUCCESS);
  EXPECT_EQ(mem.src, 2);
  engine1.Disconnect("127.0.0.1:28101");
  EXPECT_EQ(engine1.DeregisterMem(mem.handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(mem.handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

}  // namespace adxl
