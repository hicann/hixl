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
#include <cstdlib>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "hixl/hixl.h"
#include "adxl/channel_manager.h"
#include "dlog_pub.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"
#include "hixl_test_helpers.h"

using namespace std;
using namespace llm;
using namespace ::testing;
using ::testing::Invoke;
using ::testing::Mock;

namespace hixl {
namespace {
void HixlWaitDisconnectAsyncDone(Hixl &engine, int32_t timeout_in_millis,
                                 std::map<AscendString, AsyncConnectStatus> &statuses) {
  const auto expire_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_in_millis);
  while (std::chrono::steady_clock::now() <= expire_time) {
    engine.GetAsyncConnectStatus(statuses);
    if (statuses.empty()) {
      return;
    }
    bool is_all_disconnected = true;
    for (const auto &status : statuses) {
      if (status.second == AsyncConnectStatus::DISCONNECT_PENDING ||
          status.second == AsyncConnectStatus::DISCONNECTING) {
        is_all_disconnected = false;
        break;
      }
    }
    if (is_all_disconnected) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}
}  // namespace
class HixlSTest : public ::testing::Test {
 protected:
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

TEST_F(HixlSTest, TestHixl) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26200", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26201", options2), SUCCESS);

  hixl::MemDesc mem{};
  mem.addr = 1234;
  mem.len = 10;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(mem, MEM_DEVICE, handle1), SUCCESS);
  EXPECT_EQ(engine2.RegisterMem(mem, MEM_DEVICE, handle2), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26201"), SUCCESS);
  int32_t src = 1;
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26201", READ, {desc}), SUCCESS);
  EXPECT_EQ(src, 2);
  src = 1;
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26201", WRITE, {desc}), SUCCESS);
  EXPECT_EQ(dst, 1);
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26201"), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlSTest, TestHixlWithIpv6) {
  auto support_ipv6 = test_helpers::CheckIpv6Supported();
  if (support_ipv6) {
    std::cout << "support ipv6" << std::endl;
    llm::AutoCommResRuntimeMock::SetDevice(0);
    Hixl engine1;
    std::map<AscendString, AscendString> options1;
    EXPECT_EQ(engine1.Initialize("[::1]:26202", options1), SUCCESS);

    llm::AutoCommResRuntimeMock::SetDevice(1);
    Hixl engine2;
    std::map<AscendString, AscendString> options2;
    EXPECT_EQ(engine2.Initialize("[::1]:26203", options2), SUCCESS);

    hixl::MemDesc mem{};
    mem.addr = 1;
    mem.len = 1;
    MemHandle handle1 = nullptr;
    MemHandle handle2 = nullptr;
    EXPECT_EQ(engine1.RegisterMem(mem, MEM_DEVICE, handle1), SUCCESS);
    EXPECT_EQ(engine2.RegisterMem(mem, MEM_DEVICE, handle2), SUCCESS);
    EXPECT_EQ(engine1.Connect("[::1]:26203"), SUCCESS);
    int32_t src = 1;
    int32_t dst = 2;
    TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
    EXPECT_EQ(engine1.TransferSync("[::1]:26203", READ, {desc}), SUCCESS);
    EXPECT_EQ(src, dst);
    src = 1;
    EXPECT_EQ(engine1.TransferSync("[::1]:26203", WRITE, {desc}), SUCCESS);
    EXPECT_EQ(dst, src);
    EXPECT_EQ(engine1.Disconnect("[::1]:26203"), SUCCESS);
    EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
    EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
    engine1.Finalize();
    engine2.Finalize();
  } else {
    std::cout << "not support ipv6" << std::endl;
    Hixl engine1;
    std::map<AscendString, AscendString> options1;
    EXPECT_NE(engine1.Initialize("[::1]:26202", options1), SUCCESS);
  }
}

TEST_F(HixlSTest, TestHixlH2HWithBuffer) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "4";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  options1["BufferPool"] = "4:8";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  options2["BufferPool"] = "4:8";
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26201", options2), SUCCESS);

  size_t size = 16 * 1024 * 1024;
  std::vector<int8_t> src(size, 1);
  std::vector<int8_t> dst(size, 2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26201"), SUCCESS);
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(dst.data()), size};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26201", READ, {desc}), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26201", WRITE, {desc}), SUCCESS);
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
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26201", WRITE, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }
  dst.assign(size, 2);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26201", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26201"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlSTest, TestHixlD2HWithBuffer) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "4";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  options1["BufferPool"] = "4:8";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  options2["BufferPool"] = "4:8";
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26201", options2), SUCCESS);

  size_t size = 16 * 1024 * 1024;
  std::vector<int8_t> src(size, 1);
  std::vector<int8_t> dst(size, 2);
  hixl::MemDesc dst_mem{};
  dst_mem.addr = reinterpret_cast<uintptr_t>(dst.data());
  dst_mem.len = size;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(dst_mem, MEM_DEVICE, handle2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26201"), SUCCESS);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(dst.data()), size};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26201", READ, {desc}), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26201", WRITE, {desc}), SUCCESS);
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
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26201", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26201", WRITE, {desc}), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26201"), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlSTest, TestHixlDefaultBufferPoolD2D) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "4";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26201", options2), SUCCESS);

  size_t size = 16 * 1024 * 1024;
  std::vector<int8_t> src(size, 1);
  std::vector<int8_t> dst(size, 2);
  hixl::MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(src.data());
  src_mem.len = size;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(src_mem, MEM_DEVICE, handle1), SUCCESS);
  hixl::MemDesc dst_mem{};
  dst_mem.addr = reinterpret_cast<uintptr_t>(dst.data());
  dst_mem.len = size;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(dst_mem, MEM_DEVICE, handle2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26201"), SUCCESS);

  // still 16M data.
  size_t block_size = 128 * 1024;
  size_t block_num = 8 * 16;
  std::vector<TransferOpDesc> descs;
  for (size_t i = 0; i < block_num; ++i) {
    descs.emplace_back(TransferOpDesc{reinterpret_cast<uintptr_t>(src.data()) + i * block_size,
                                      reinterpret_cast<uintptr_t>(dst.data()) + i * block_size, block_size});
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26201", WRITE, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }
  dst.assign(size, 2);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26201", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26201"), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlSTest, TestHixlDisableBufferPoolD2D) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "4";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  options1["BufferPool"] = "0:0";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  options2["BufferPool"] = "0:0";
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26201", options2), SUCCESS);

  size_t size = 16 * 1024 * 1024;
  std::vector<int8_t> src(size, 1);
  std::vector<int8_t> dst(size, 2);
  hixl::MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(src.data());
  src_mem.len = size;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(src_mem, MEM_DEVICE, handle1), SUCCESS);
  hixl::MemDesc dst_mem{};
  dst_mem.addr = reinterpret_cast<uintptr_t>(dst.data());
  dst_mem.len = size;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(dst_mem, MEM_DEVICE, handle2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26201"), SUCCESS);

  // still 16M data.
  size_t block_size = 128 * 1024;
  size_t block_num = 8 * 16;
  std::vector<TransferOpDesc> descs;
  for (size_t i = 0; i < block_num; ++i) {
    descs.emplace_back(TransferOpDesc{reinterpret_cast<uintptr_t>(src.data()) + i * block_size,
                                      reinterpret_cast<uintptr_t>(dst.data()) + i * block_size, block_size});
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26201", WRITE, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }
  dst.assign(size, 2);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26201", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26201"), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlSTest, TestHeartbeat) {
  adxl::ChannelManager::SetHeartbeatWaitTime(10);  // 10ms
  adxl::CommChannel::SetHeartbeatTimeout(50);      // 50ms
  Hixl engine1;
  llm::AutoCommResRuntimeMock::SetDevice(0);
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26201", options2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26201"), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26201"), ALREADY_CONNECTED);
  std::this_thread::sleep_for(std::chrono::milliseconds(60));  // wait heartbeat process
  int32_t src = 1;
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26201", READ, {desc}), SUCCESS);
  EXPECT_EQ(src, 2);
  // not disconnect, force finalize
  engine1.Finalize();

  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine3;
  EXPECT_EQ(engine3.Initialize("127.0.0.1", options1), SUCCESS);  // use same key with engine1
  EXPECT_EQ(engine3.Connect("127.0.0.1:26201"), SUCCESS);
  // not disconnect, force finalize
  engine3.Finalize();
  std::this_thread::sleep_for(std::chrono::milliseconds(60));  // wait server:engine2 clear client:engine3
  engine2.Finalize();
}

TEST_F(HixlSTest, TestHixlServerDown) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26200", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26201", options2), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26201"), SUCCESS);
  engine2.Finalize();
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26201"), SUCCESS);
  engine1.Finalize();
}

TEST_F(HixlSTest, TestHixlAutoClearChannel) {
  adxl::ChannelManager::SetHeartbeatWaitTime(10);  // 10ms
  adxl::CommChannel::SetHeartbeatTimeout(50);      // 50ms
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_AUTO_CONNECT] = "1";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26200", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26201", options2), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26201"), SUCCESS);
  engine2.Finalize();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26201"), NOT_CONNECTED);
  engine1.Finalize();
}

TEST_F(HixlSTest, TestHixlConnectAsync) {
  constexpr int32_t ENGINE_NUM = 4;
  constexpr int32_t SLEEP_IN_MILLIS = 10;
  constexpr int32_t TIMEOUT_IN_MILLIS = 1000;
  const std::string IP = "127.0.0.1:";
  const std::map<AscendString, AscendString> OPTIONS = {
      {"GlobalResourceConfig", R"({"connect_pool.thread_num":"2","connect_pool.task_queue_capacity":"8"})"}};

  std::vector<Hixl> engine_list(ENGINE_NUM);
  for (int32_t i = 0; i < ENGINE_NUM; ++i) {
    llm::AutoCommResRuntimeMock::SetDevice(i);
    EXPECT_EQ(engine_list[i].Initialize((IP + std::to_string(26200 + i)).c_str(), OPTIONS), SUCCESS);
  }

  for (int32_t i = 1; i < ENGINE_NUM; ++i) {
    auto ret = engine_list[0].ConnectAsync((IP + std::to_string(26200 + i)).c_str(), TIMEOUT_IN_MILLIS);
    while (ret == RESOURCE_EXHAUSTED) {
      std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_IN_MILLIS));
      ret = engine_list[0].ConnectAsync((IP + std::to_string(26200 + i)).c_str(), TIMEOUT_IN_MILLIS);
    }
    EXPECT_EQ(ret, SUCCESS);
  }

  for (int32_t i = 1; i < ENGINE_NUM; ++i) {
    AsyncConnectStatus status = AsyncConnectStatus::NOT_CONNECT;
    const auto expire_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(TIMEOUT_IN_MILLIS);
    while (std::chrono::steady_clock::now() <= expire_time) {
      std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_IN_MILLIS));
      engine_list[0].GetAsyncConnectStatus((IP + std::to_string(26200 + i)).c_str(), status);
      if (status != AsyncConnectStatus::CONNECT_PENDING && status != AsyncConnectStatus::CONNECTING) {
        break;
      }
    }
    EXPECT_EQ(status, AsyncConnectStatus::CONNECTED);
  }

  for (int32_t i = 1; i < ENGINE_NUM; ++i) {
    auto ret = engine_list[0].DisconnectAsync((IP + std::to_string(26200 + i)).c_str(), TIMEOUT_IN_MILLIS);
    while (ret == RESOURCE_EXHAUSTED) {
      std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_IN_MILLIS));
      ret = engine_list[0].DisconnectAsync((IP + std::to_string(26200 + i)).c_str(), TIMEOUT_IN_MILLIS);
    }
    EXPECT_EQ(ret, SUCCESS);
  }

  std::map<AscendString, AsyncConnectStatus> statuses;
  HixlWaitDisconnectAsyncDone(engine_list[0], TIMEOUT_IN_MILLIS, statuses);
  EXPECT_EQ(statuses.empty(), true);

  for (int32_t i = 0; i < ENGINE_NUM; ++i) {
    engine_list[i].Finalize();
  }
}

TEST_F(HixlSTest, TestHixlConnectAsyncFailed) {
  constexpr int32_t SLEEP_IN_MILLIS = 10;
  constexpr int32_t TIMEOUT_IN_MILLIS = 1000;
  Hixl engine;
  std::map<AscendString, AscendString> options;
  EXPECT_EQ(engine.Initialize("127.0.0.1:26200", options), SUCCESS);
  // not listen
  EXPECT_EQ(engine.ConnectAsync("127.0.0.1:26201"), SUCCESS);

  AsyncConnectStatus status = AsyncConnectStatus::NOT_CONNECT;
  const auto expire_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(TIMEOUT_IN_MILLIS);
  while (std::chrono::steady_clock::now() <= expire_time) {
    std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_IN_MILLIS));
    engine.GetAsyncConnectStatus("127.0.0.1:26201", status);
    if (status != AsyncConnectStatus::CONNECT_PENDING && status != AsyncConnectStatus::CONNECTING) {
      break;
    }
  }
  EXPECT_EQ(status, AsyncConnectStatus::CONNECT_FAILED);
}

TEST_F(HixlSTest, TestHixlDisconnectAsyncFailed) {
  constexpr int32_t SLEEP_IN_MILLIS = 10;
  constexpr int32_t TIMEOUT_IN_MILLIS = 1000;
  Hixl engine;
  std::map<AscendString, AscendString> options;
  EXPECT_EQ(engine.Initialize("127.0.0.1:26200", options), SUCCESS);
  // not listen
  EXPECT_EQ(engine.DisconnectAsync("127.0.0.1:26201"), SUCCESS);

  AsyncConnectStatus status = AsyncConnectStatus::NOT_CONNECT;
  const auto expire_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(TIMEOUT_IN_MILLIS);
  while (std::chrono::steady_clock::now() <= expire_time) {
    std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_IN_MILLIS));
    engine.GetAsyncConnectStatus("127.0.0.1:26201", status);
    if (status != AsyncConnectStatus::DISCONNECT_PENDING && status != AsyncConnectStatus::DISCONNECTING) {
      break;
    }
  }
  EXPECT_EQ(status, AsyncConnectStatus::NOT_CONNECT);
}

}  // namespace hixl
