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
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nlohmann/json.hpp"

#include "adxl/adxl_engine.h"
#include "adxl/channel_manager.h"
#include "dlog_pub.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"
#include "adxl_test_helpers.h"
#include "hixl/hixl_types.h"

using namespace std;
using namespace llm;
using namespace ::testing;
using ::testing::Invoke;
using ::testing::Mock;

namespace adxl {
namespace {
std::mutex g_rank_table_mutex;
std::vector<std::string> g_captured_rank_tables;

class RankTableCaptureHcclStub : public llm::HcclApiStub {
 public:
  HcclResult HcclCommInitClusterInfoMem(const char *cluster, uint32_t rank, HcclCommConfig *config,
                                        HcclComm *comm) override {
    if (cluster != nullptr) {
      std::lock_guard<std::mutex> lock(g_rank_table_mutex);
      g_captured_rank_tables.emplace_back(cluster);
    }
    return llm::HcclApiStub::HcclCommInitClusterInfoMem(cluster, rank, config, comm);
  }
};

void CollectDevicePorts(const nlohmann::json &rank_table_json, std::set<std::string> &device_ports) {
  for (const auto &server : rank_table_json.at("server_list")) {
    for (const auto &device : server.at("device")) {
      if (!device.contains("device_port")) {
        continue;
      }
      device_ports.emplace(device.at("device_port").get<std::string>());
    }
  }
}

bool RankTableContainsDevicePorts(const std::string &rank_table, const std::string &first_port,
                                  const std::string &second_port) {
  try {
    std::set<std::string> device_ports;
    CollectDevicePorts(nlohmann::json::parse(rank_table), device_ports);
    return device_ports.count(first_port) != 0U && device_ports.count(second_port) != 0U;
  } catch (const nlohmann::json::exception &) {
    return false;
  }
}

bool CapturedRankTableContainsDevicePorts(const std::string &first_port, const std::string &second_port) {
  std::lock_guard<std::mutex> lock(g_rank_table_mutex);
  for (const auto &rank_table : g_captured_rank_tables) {
    if (RankTableContainsDevicePorts(rank_table, first_port, second_port)) {
      return true;
    }
  }
  return false;
}
}  // namespace

class AdxlEngineSTest : public ::testing::Test {
 protected:
  // 在测试类中设置一些准备工作，如果需要的话
  void SetUp() override {
    llm::MockMmpaForHcclApi::Install();
    llm::AutoCommResRuntimeMock::Install();
  }
  // 在测试类中进行清理工作，如果需要的话
  void TearDown() override {
    llm::HcclAdapter::GetInstance().Finalize();
    llm::HcclApiStub::ResetStub();
    llm::MockMmpaForHcclApi::Reset();
    llm::AutoCommResRuntimeMock::Reset();
  }

  static void ExpectVectorFilled(const std::vector<int8_t> &data, int8_t expected) {
    for (size_t i = 0; i < data.size(); ++i) {
      EXPECT_EQ(data[i], expected);
    }
  }

  void RunD2HWithBufferTransfer() {
    llm::AutoCommResRuntimeMock::SetDevice(0);
    AdxlEngine engine1;
    std::map<AscendString, AscendString> options1;
    options1[OPTION_RDMA_TRAFFIC_CLASS] = "4";
    options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
    options1["adxl.BufferPool"] = "4:8";
    EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

    llm::AutoCommResRuntimeMock::SetDevice(1);
    AdxlEngine engine2;
    std::map<AscendString, AscendString> options2;
    options2["adxl.BufferPool"] = "4:8";
    EXPECT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);

    size_t size = 16 * 1024 * 1024;
    std::vector<int8_t> src(size, 1);
    std::vector<int8_t> dst(size, 2);
    MemHandle handle2 = nullptr;
    test_helpers::RegisterDeviceBufferMem(engine2, dst, handle2);

    EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);

    TransferOpDesc desc{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(dst.data()), size};
    EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", READ, {desc}), SUCCESS);
    ExpectVectorFilled(src, 2);
    src.assign(size, 1);
    EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", WRITE, {desc}), SUCCESS);
    ExpectVectorFilled(dst, 1);

    dst.assign(size, 2);
    size_t block_size = 256 * 1024;
    size_t block_num = 4 * 16;
    std::vector<TransferOpDesc> descs;
    for (size_t i = 0; i < block_num; ++i) {
      descs.emplace_back(TransferOpDesc{reinterpret_cast<uintptr_t>(src.data()) + i * block_size,
                                        reinterpret_cast<uintptr_t>(dst.data()) + i * block_size, block_size});
    }
    EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", READ, descs), SUCCESS);
    ExpectVectorFilled(src, 2);
    src.assign(size, 1);
    EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", WRITE, {desc}), SUCCESS);
    ExpectVectorFilled(dst, 1);

    EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
    EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
    engine1.Finalize();
    engine2.Finalize();
  }
};

TEST_F(AdxlEngineSTest, TestGlobalResourceConfigCommResourceListenPortValidation) {
  for (const auto &listen_port : {"0", "65536", "invalid"}) {
    AdxlEngine engine;
    const std::string global_resource_config =
        std::string(R"({"comm_resource_config.listen_port":")") + listen_port + R"("})";
    std::map<AscendString, AscendString> options;
    options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(global_resource_config.c_str());
    EXPECT_EQ(engine.Initialize("127.0.0.1", options), PARAM_INVALID) << "listen_port=" << listen_port;
  }

  AdxlEngine engine;
  std::map<AscendString, AscendString> options;
  options[OPTION_BUFFER_POOL] = "0:0";
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.listen_port":"65535"})";
  EXPECT_EQ(engine.Initialize("127.0.0.1", options), SUCCESS);
  engine.Finalize();
}

TEST_F(AdxlEngineSTest, TestGlobalResourceConfigCommResourceListenPortInRankTable) {
  {
    std::lock_guard<std::mutex> lock(g_rank_table_mutex);
    g_captured_rank_tables.clear();
  }
  llm::HcclApiStub::SetStub(std::make_unique<RankTableCaptureHcclStub>());

  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_BUFFER_POOL] = "0:0";
  options1[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.listen_port":"23456"})";
  ASSERT_EQ(engine1.Initialize("127.0.0.1:28100", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  options2[OPTION_BUFFER_POOL] = "0:0";
  options2[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.listen_port":"23457"})";
  ASSERT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);
  EXPECT_TRUE(CapturedRankTableContainsDevicePorts("23456", "23457"));
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineSTest, TestAdxlEngine) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:28100", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);

  adxl::MemDesc mem{};
  mem.addr = 1234;
  mem.len = 10;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(mem, MEM_DEVICE, handle1), SUCCESS);
  EXPECT_EQ(engine2.RegisterMem(mem, MEM_DEVICE, handle2), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);
  int32_t src = 1;
  int32_t dst = 2;
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

TEST_F(AdxlEngineSTest, TestAdxlEngineH2HWithBuffer) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "4";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  options1["adxl.BufferPool"] = "4:8";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  options2["adxl.BufferPool"] = "4:8";
  EXPECT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);

  size_t size = 16 * 1024 * 1024;
  std::vector<int8_t> src(size, 1);
  std::vector<int8_t> dst(size, 2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(dst.data()), size};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", READ, {desc}), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", WRITE, {desc}), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }

  // still 16M data.
  size_t block_size = 256 * 1024;
  size_t block_num = 4 * 16;
  std::vector<TransferOpDesc> descs;
  for (size_t i = 0; i < block_num; ++i) {
    descs.emplace_back(TransferOpDesc{reinterpret_cast<uintptr_t>(src.data()) + i * block_size,
                                      reinterpret_cast<uintptr_t>(dst.data()) + i * block_size, block_size});
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", WRITE, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }
  dst.assign(size, 2);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineSTest, TestAdxlEngineD2HWithBuffer) {
  RunD2HWithBufferTransfer();
}

TEST_F(AdxlEngineSTest, TestAdxlDefaultBufferPoolD2D) {
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

  size_t size = 16 * 1024 * 1024;
  std::vector<int8_t> src(size, 1);
  std::vector<int8_t> dst(size, 2);
  adxl::MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(src.data());
  src_mem.len = size;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(src_mem, MEM_DEVICE, handle1), SUCCESS);
  adxl::MemDesc dst_mem{};
  dst_mem.addr = reinterpret_cast<uintptr_t>(dst.data());
  dst_mem.len = size;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(dst_mem, MEM_DEVICE, handle2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);

  // still 16M data.
  size_t block_size = 128 * 1024;
  size_t block_num = 8 * 16;
  std::vector<TransferOpDesc> descs;
  for (size_t i = 0; i < block_num; ++i) {
    descs.emplace_back(TransferOpDesc{reinterpret_cast<uintptr_t>(src.data()) + i * block_size,
                                      reinterpret_cast<uintptr_t>(dst.data()) + i * block_size, block_size});
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", WRITE, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }
  dst.assign(size, 2);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineSTest, TestAdxlDisableBufferPoolD2D) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "4";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  options1["adxl.BufferPool"] = "0:0";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  options2["adxl.BufferPool"] = "0:0";
  EXPECT_EQ(engine2.Initialize("127.0.0.1:28101", options2), SUCCESS);

  size_t size = 16 * 1024 * 1024;
  std::vector<int8_t> src(size, 1);
  std::vector<int8_t> dst(size, 2);
  adxl::MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(src.data());
  src_mem.len = size;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(src_mem, MEM_DEVICE, handle1), SUCCESS);
  adxl::MemDesc dst_mem{};
  dst_mem.addr = reinterpret_cast<uintptr_t>(dst.data());
  dst_mem.len = size;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(dst_mem, MEM_DEVICE, handle2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:28101"), SUCCESS);

  // still 16M data.
  size_t block_size = 128 * 1024;
  size_t block_num = 8 * 16;
  std::vector<TransferOpDesc> descs;
  for (size_t i = 0; i < block_num; ++i) {
    descs.emplace_back(TransferOpDesc{reinterpret_cast<uintptr_t>(src.data()) + i * block_size,
                                      reinterpret_cast<uintptr_t>(dst.data()) + i * block_size, block_size});
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", WRITE, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }
  dst.assign(size, 2);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:28101", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:28101"), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineSTest, TestHeartbeat) {
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

}  // namespace adxl
