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

#include "hixl/hixl.h"
#include "adxl/channel_manager.h"
#include "common/rank_table_generator.h"
#include "dlog_pub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"

using namespace std;
using namespace llm;
using namespace ::testing;
using ::testing::Invoke;
using ::testing::Mock;
namespace hixl {
class HixlUTest : public ::testing::Test {
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
  //初始化两个 Hixl 引擎
  void SetupEngines(Hixl &engine1, Hixl &engine2) {
    llm::AutoCommResRuntimeMock::SetDevice(0);
    std::map<AscendString, AscendString> options1;
    options1[OPTION_RDMA_TRAFFIC_CLASS] = "1";
    options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
    options1[OPTION_BUFFER_POOL] = "0:0";
    EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

    llm::AutoCommResRuntimeMock::SetDevice(1);
    std::map<AscendString, AscendString> options2;
    EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);
  }
  //注册 int32 类型的内存
  void RegisterInt32Mem(Hixl &engine, int32_t *ptr, MemHandle &handle) {
    MemDesc mem_desc{};
    mem_desc.addr = reinterpret_cast<uintptr_t>(ptr);
    mem_desc.len = sizeof(int32_t);
    EXPECT_EQ(engine.RegisterMem(mem_desc, MEM_DEVICE, handle), SUCCESS);
  }
  //清理资源
  void CleanupEngine(Hixl &engine1, Hixl &engine2, MemHandle &handle1, MemHandle &handle2) {
    EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
    EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
    EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
    engine1.Finalize();
    engine2.Finalize();
  }
};

class HccnToolTest : public ::testing::Test {
  protected:
  void SetUp() override {
    llm::MockMmpaForHcclApi::Install();
    llm::AutoCommResRuntimeMock::InstallWithoutHccnConfFile();
    llm::AutoCommResRuntimeMock::DeleteHccnConfIfExist();
    llm::HcclAdapter::GetInstance().Initialize();
  }

  void TearDown() override {
    llm::HcclAdapter::GetInstance().Finalize();
    llm::MockMmpaForHcclApi::Reset();
    llm::AutoCommResRuntimeMock::Reset();
  }
};

class HccnGetIpTest : public ::testing::Test {
  protected:
  void SetUp() override {
    llm::MockHccnTool::Install();
    llm::AutoCommResRuntimeMock::Install();
    llm::HcclAdapter::GetInstance().Initialize();
  }

  void TearDown() override {
    llm::HcclAdapter::GetInstance().Finalize();
    llm::AutoCommResRuntimeMock::Reset();
    llm::MockHccnTool::Reset();
  }
};

class HccnGetOutputTest : public ::testing::Test {
  protected:
  void SetUp() override {
    llm::MockGetHccnResult::Install();
    llm::AutoCommResRuntimeMock::Install();
    llm::HcclAdapter::GetInstance().Initialize();
  }

  void TearDown() override {
    llm::HcclAdapter::GetInstance().Finalize();
    llm::AutoCommResRuntimeMock::Reset();
    llm::MockGetHccnResult::Reset();
  }
};

TEST_F(HixlUTest, TestHixl) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "1";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  int32_t src = 1;
  hixl::MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(&src);
  src_mem.len = sizeof(int32_t);
  MemHandle handle1 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(src_mem, MEM_DEVICE, handle1), SUCCESS);

  int32_t dst = 2;
  hixl::MemDesc dst_mem{};
  dst_mem.addr = reinterpret_cast<uintptr_t>(&dst);
  dst_mem.len = sizeof(int32_t);
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(dst_mem, MEM_DEVICE, handle2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, {desc}), SUCCESS);
  EXPECT_EQ(src, 2);
  src = 1;
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", WRITE, {desc}), SUCCESS);
  EXPECT_EQ(dst, 1);
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);

  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HccnToolTest, TestExtractIp) {
  std::string output = "ipaddr:127.0.0.1";
  std::string ip = "";
  llm::LocalCommResGenerator::ExtractIpAddress(output, ip);
  EXPECT_EQ(ip, "127.0.0.1");
}

TEST_F(HccnToolTest, TestHccnOutput) {
  std::string cmd = "hccn_tool";
  std::string result = "";
  EXPECT_EQ(llm::LocalCommResGenerator::GetHccnOutput(cmd, result), ge::SUCCESS);
}

TEST_F(HccnGetIpTest, TestGetIpFromHccnTool) {
  std::string ip = "";
  EXPECT_EQ(llm::LocalCommResGenerator::GetIpAddressFromHccnTool(0, ip), ge::SUCCESS);
}

TEST_F(HccnGetOutputTest, TestGetOutput) {
  std::string ip = "";
  EXPECT_EQ(llm::LocalCommResGenerator::GetIpAddressFromHccnTool(0, ip), ge::SUCCESS);
}

TEST_F(HccnGetOutputTest, TestGetIp) {
  std::string device_ip = "";
  EXPECT_EQ(llm::LocalCommResGenerator::GetDeviceIp(0, device_ip), ge::SUCCESS);
}

TEST_F(HixlUTest, TestHixlInitFailed) {
  Hixl engine;
  std::map<AscendString, AscendString> options;
  // invalid ip
  EXPECT_EQ(engine.Initialize("ad.0.0.1:26000", options), PARAM_INVALID);
}

TEST_F(HixlUTest, TestConnectNotListenFailed) {
  Hixl engine;
  std::map<AscendString, AscendString> options;
  EXPECT_EQ(engine.Initialize("127.0.0.1:26000", options), SUCCESS);
  // not listen
  EXPECT_EQ(engine.Connect("127.0.0.1:26001"), FAILED);
}

TEST_F(HixlUTest, TestAlreadyConnectedFailed) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), ALREADY_CONNECTED);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlUTest, TestDeregisterUnregisterMem) {
  Hixl engine;
  std::map<AscendString, AscendString> options;
  EXPECT_EQ(engine.Initialize("127.0.0.1", options), SUCCESS);
  MemHandle handle = (MemHandle)0x100;
  // deregister unregister mem
  EXPECT_EQ(engine.DeregisterMem(handle), SUCCESS);
  engine.Finalize();
}

TEST_F(HixlUTest, TestHeartbeat) {
  adxl::ChannelManager::SetHeartbeatWaitTime(10);  // 10ms
  adxl::Channel::SetHeartbeatTimeout(50);  // 50ms
  Hixl engine1;
  llm::AutoCommResRuntimeMock::SetDevice(0);
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), ALREADY_CONNECTED);
  std::this_thread::sleep_for(std::chrono::milliseconds(60));  // wait heartbeat process
  int32_t src = 1;
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, {desc}), SUCCESS);
  EXPECT_EQ(src, 2);
  // not disconnet, force finalize
  engine1.Finalize();

  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine3;
  EXPECT_EQ(engine3.Initialize("127.0.0.1", options1), SUCCESS);  // use same key with engine1
  EXPECT_EQ(engine3.Connect("127.0.0.1:26001"), SUCCESS);
  // not disconnet, force finalize
  engine3.Finalize();
  std::this_thread::sleep_for(std::chrono::milliseconds(60));  // wait server:engine2 clear client:engine3 
  engine2.Finalize();
}

TEST_F(HixlUTest, TestHixlH2HWithBuffer) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "1";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  options1["BufferPool"] = "4:8";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  options2["BufferPool"] = "4:8";
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  size_t size = 16 * 1024 * 1024;
  std::vector<int8_t> src(size, 1);
  std::vector<int8_t> dst(size, 2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(dst.data()), size};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, {desc}), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", WRITE, {desc}), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }

  // still 16M data.
  dst.assign(size, 2);
  size_t block_size = 256 * 1024;
  size_t block_num = 4 * 16;
  std::vector<TransferOpDesc> descs;
  for (size_t i = 0; i < block_num; ++i) {
    descs.emplace_back(TransferOpDesc{reinterpret_cast<uintptr_t>(src.data()) + i * block_size,
                                      reinterpret_cast<uintptr_t>(dst.data()) + i * block_size, block_size});
  }
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", WRITE, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlUTest, TestHixlRD2HWithBuffer) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "1";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  options1["BufferPool"] = "4:8";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  options2["BufferPool"] = "4:8";
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  size_t size = 16 * 1024 * 1024;
  std::vector<int8_t> src(size, 1);
  std::vector<int8_t> dst(size, 2);
  hixl::MemDesc dst_mem{};
  dst_mem.addr = reinterpret_cast<uintptr_t>(dst.data());
  dst_mem.len = size;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(dst_mem, MEM_DEVICE, handle2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(dst.data()), size};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, {desc}), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }

  // still 16M data.
  dst.assign(size, 2);
  size_t block_size = 256 * 1024;
  size_t block_num = 4 * 16;
  std::vector<TransferOpDesc> descs;
  for (size_t i = 0; i < block_num; ++i) {
    descs.emplace_back(TransferOpDesc{reinterpret_cast<uintptr_t>(src.data()) + i * block_size,
                                      reinterpret_cast<uintptr_t>(dst.data()) + i * block_size, block_size});
  }
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}
 
TEST_F(HixlUTest, TestHixlTransferAsync) {
  Hixl engine1;
  Hixl engine2;
  SetupEngines(engine1, engine2);
  int32_t src = 1;
  MemHandle handle1 = nullptr;
  RegisterInt32Mem(engine1, &src, handle1);
  int32_t dst = 2;
  MemHandle handle2 = nullptr;
  RegisterInt32Mem(engine2, &dst, handle2);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  TransferReq req = nullptr;
  ASSERT_EQ(engine1.TransferAsync("127.0.0.1:26001", READ, {desc}, {}, req), SUCCESS);

  constexpr int kMaxPollTimes = 10;
  constexpr int kPollInterval = 10;
  TransferStatus status = TransferStatus::WAITING;
  for (int i = 0; i < kMaxPollTimes && status == TransferStatus::WAITING; i++) {
    engine1.GetTransferStatus(req, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollInterval));
  }
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(src, 2);
  //测试多次查找
  EXPECT_EQ(engine1.GetTransferStatus(req, status), PARAM_INVALID);
  EXPECT_EQ(status, TransferStatus::FAILED);
  
  src = 1;
  ASSERT_EQ(engine1.TransferAsync("127.0.0.1:26001", WRITE, {desc}, {}, req), SUCCESS);
  status = TransferStatus::WAITING;
  for (int i = 0; i < kMaxPollTimes && status == TransferStatus::WAITING; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollInterval));
    EXPECT_EQ(engine1.GetTransferStatus(req, status), SUCCESS);
  }
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(dst, 1); 

  CleanupEngine(engine1, engine2, handle1, handle2);
}

TEST_F(HixlUTest, TestHixlTransferAsyncWithMultiThread) {
  Hixl engine1;
  Hixl engine2;
  SetupEngines(engine1, engine2);
  int32_t src = 1;
  MemHandle handle1 = nullptr;
  RegisterInt32Mem(engine1, &src, handle1);
  int32_t dst = 2;
  MemHandle handle2 = nullptr;
  RegisterInt32Mem(engine2, &dst, handle2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  constexpr int kThreadCount = 20;
  constexpr int kPollInterval = 10;
  constexpr int kMaxWaitTime = 5; //5s
  TransferReq req_list[kThreadCount];
  std::vector<std::thread> async_threads;
  for(int i = 0; i< kThreadCount; i++) {
    async_threads.emplace_back([&, i]() {
      EXPECT_EQ(engine1.TransferAsync("127.0.0.1:26001", WRITE, {desc}, {}, req_list[i]), SUCCESS);
    });
  }
  for (auto& t : async_threads) { t.join();} 
  std::vector<std::thread> poll_threads;
  std::atomic<int> completed{0};
  std::atomic<bool> stop{false};
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kMaxWaitTime);
  for(int i = 0; i <kThreadCount; i++) {
    poll_threads.emplace_back([&, i]() {
      TransferStatus status = TransferStatus::WAITING;
      while (!stop.load() && status == TransferStatus::WAITING) {
        engine1.GetTransferStatus(req_list[i], status);
        if(status == TransferStatus::COMPLETED) {
          completed.fetch_add(1);
          break;
        }else if(status == TransferStatus::FAILED) { break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollInterval));
      }
    });
  }
  while(std::chrono::steady_clock::now() < deadline) {
    if(completed.load() == kThreadCount) { break;}
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollInterval));
  }
  stop = true;
  for (auto &t : poll_threads){
    if(t.joinable()) { t.join();}
  }
  EXPECT_EQ(completed.load(), kThreadCount);
  CleanupEngine(engine1, engine2, handle1, handle2);
}

TEST_F(HixlUTest, TestHixlGetTransferStatusFalied) {
  llm:AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  TransferReq req = nullptr;
  TransferStatus status;
  EXPECT_EQ(engine1.GetTransferStatus(req, status), FAILED);
  //给 req 随机赋值一个地址
  constexpr size_t kFakeReqSize = 64;
  req = malloc(kFakeReqSize);
  EXPECT_EQ(engine1.GetTransferStatus(req, status), PARAM_INVALID);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  free(req);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlUTest, TestHixlGetTransferStatusWithInterrupt) {
  Hixl engine1;
  Hixl engine2;
  SetupEngines(engine1, engine2);
  int32_t src = 1;
  MemHandle handle1 = nullptr;
  RegisterInt32Mem(engine1, &src, handle1);
  int32_t dst = 2;
  MemHandle handle2 = nullptr;
  RegisterInt32Mem(engine2, &dst, handle2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  TransferReq req = nullptr;
  EXPECT_EQ(engine1.TransferAsync("127.0.0.1:26001", WRITE, {desc}, {}, req), SUCCESS);
  engine1.Disconnect("127.0.0.1:26001");
  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(engine1.GetTransferStatus(req, status), NOT_CONNECTED);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlUTest, TestHixlGetTransferStatusWithQueryEventFailed) {
  Hixl engine1;
  Hixl engine2;
  SetupEngines(engine1, engine2);
  int32_t src = 1;
  MemHandle handle1 = nullptr;
  RegisterInt32Mem(engine1, &src, handle1);
  int32_t dst = 2;
  MemHandle handle2 = nullptr;
  RegisterInt32Mem(engine2, &dst, handle2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  TransferReq req = nullptr;
  EXPECT_EQ(engine1.TransferAsync("127.0.0.1:26001", WRITE, {desc}, {}, req), SUCCESS);
  TransferStatus status = TransferStatus::WAITING;
  TransferAsyncRuntimeMock instance;
  llm::AclRuntimeStub::Install(&instance);
  EXPECT_EQ(engine1.GetTransferStatus(req, status), FAILED);
  llm::AclRuntimeStub::UnInstall(&instance);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlUTest, TestHixlSendGetNotifies) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  
  // send 5 notify messages
  for (int i = 0; i < 5; ++i) {
    NotifyDesc notify;
    notify.name = AscendString(("test_notify" + std::to_string(i)).c_str());
    notify.notify_msg = AscendString(("message " + std::to_string(i)).c_str());
    EXPECT_EQ(engine1.SendNotify("127.0.0.1:26001", notify), SUCCESS);
  }
  
  // sleep 100 ms then get notifies
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  std::vector<NotifyDesc> notifies;
  EXPECT_EQ(engine2.GetNotifies(notifies), SUCCESS);
  // should get 5 notifies
  EXPECT_EQ(notifies.size(), 5);
  
  // check 5 notifies
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(std::string(notifies[i].name.GetString()), "test_notify" + std::to_string(i));
    EXPECT_EQ(std::string(notifies[i].notify_msg.GetString()), "message " + std::to_string(i));
  }
  
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlUTest, TestHixlMultiGetNotifies) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  // send 5 notify messages
  for (int i = 0; i < 5; ++i) {
    NotifyDesc notify;
    notify.name = AscendString(("test_notify" + std::to_string(i)).c_str());
    notify.notify_msg = AscendString(("message " + std::to_string(i)).c_str());
    EXPECT_EQ(engine1.SendNotify("127.0.0.1:26001", notify), SUCCESS);
  }
  // sleep 100 ms then get notifies
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  std::vector<NotifyDesc> notifies;
  EXPECT_EQ(engine2.GetNotifies(notifies), SUCCESS);
  // should get 5 notifies
  EXPECT_EQ(notifies.size(), 5);
  // check 5 notifies
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(std::string(notifies[i].name.GetString()), "test_notify" + std::to_string(i));
    EXPECT_EQ(std::string(notifies[i].notify_msg.GetString()), "message " + std::to_string(i));
  }
  
  notifies.clear();
  EXPECT_EQ(engine2.GetNotifies(notifies), SUCCESS);
  // should get 0 notify
  EXPECT_EQ(notifies.size(), 0);
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlUTest, TestHixlMultiSendNotifies) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);
  // set device 2
  llm::AutoCommResRuntimeMock::SetDevice(2);
  Hixl engine3;
  std::map<AscendString, AscendString> options3;
  EXPECT_EQ(engine3.Initialize("127.0.0.1:26002", options3), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine3.Connect("127.0.0.1:26001"), SUCCESS);
  // each engine send 5 notifies
  for (int i = 0; i < 5; ++i) {
    NotifyDesc notify;
    notify.name = AscendString(("test_notify" + std::to_string(i)).c_str());
    notify.notify_msg = AscendString(("message " + std::to_string(i)).c_str());
    EXPECT_EQ(engine1.SendNotify("127.0.0.1:26001", notify), SUCCESS);
    EXPECT_EQ(engine3.SendNotify("127.0.0.1:26001", notify), SUCCESS);
  }
  // sleep 100 ms then get notifies
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  std::vector<NotifyDesc> notifies;
  EXPECT_EQ(engine2.GetNotifies(notifies), SUCCESS);
  // should get 10 notifies
  EXPECT_EQ(notifies.size(), 10);
  
  notifies.clear();
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine3.Disconnect("127.0.0.1:26001"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
  engine3.Finalize();
}

TEST_F(HixlUTest, TestHixlSendNotifyTimeout) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  // send 5 notifies
  for (int i = 0; i < 5; ++i) {
    NotifyDesc notify;
    notify.name = AscendString(("test_notify" + std::to_string(i)).c_str());
    notify.notify_msg = AscendString(("message " + std::to_string(i)).c_str());
    // set timeout param to 1 ms
    EXPECT_EQ(engine1.SendNotify("127.0.0.1:26001", notify, 1), TIMEOUT);
  }
  // sleep 100 ms then get notifies
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  std::vector<NotifyDesc> notifies;
  EXPECT_EQ(engine2.GetNotifies(notifies), SUCCESS);
  // should get 0 notifies
  EXPECT_EQ(notifies.size(), 0);
  
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlUTest, TestHixlSendNotifyNameTooLong) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);

  NotifyDesc notify;
  // send notify name consist of 2000 'a'
  std::string long_name(2000, 'a');
  notify.name = AscendString(long_name.c_str());
  notify.notify_msg = AscendString("short message");
  
  EXPECT_EQ(engine1.SendNotify("127.0.0.1:26001", notify), PARAM_INVALID);
  
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlUTest, TestHixlSendNotifyMsgTooLong) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  
  NotifyDesc notify;
  notify.name = AscendString("short name");
  // send notify msg consist of 2000 'b'
  std::string long_msg(2000, 'b');
  notify.notify_msg = AscendString(long_msg.c_str());

  EXPECT_EQ(engine1.SendNotify("127.0.0.1:26001", notify), PARAM_INVALID);
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlUTest, TestHixlGetTransferStatusWithStreamSyncFailed) {
  Hixl engine1;
  Hixl engine2;
  SetupEngines(engine1, engine2);
  int32_t src = 1;
  MemHandle handle1 = nullptr;
  RegisterInt32Mem(engine1, &src, handle1);
  int32_t dst = 2;
  MemHandle handle2 = nullptr;
  RegisterInt32Mem(engine2, &dst, handle2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  TransferReq req = nullptr;
  EXPECT_EQ(engine1.TransferAsync("127.0.0.1:26001", WRITE, {desc}, {}, req), SUCCESS);
  TransferStatus status = TransferStatus::WAITING;
  TransferAsyncSteamRuntimeMocak instance;
  llm::AclRuntimeStub::Install(&instance);
  EXPECT_EQ(engine1.GetTransferStatus(req, status), FAILED);
  llm::AclRuntimeStub::UnInstall(&instance);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlUTest, TestHixlEngineAutoConnectEnabled) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_AUTO_CONNECT] = "1";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options1), SUCCESS);
  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);
  int32_t src = 1;
  MemHandle handle1 = nullptr;
  RegisterInt32Mem(engine1, &src, handle1);
  int32_t dst = 2;
  MemHandle handle2 = nullptr;
  RegisterInt32Mem(engine2, &dst, handle2);
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, {desc}), SUCCESS);
  EXPECT_EQ(src, 2);
  engine1.Disconnect("127.0.0.1:26001");
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}
}  // namespace llm_datadist
