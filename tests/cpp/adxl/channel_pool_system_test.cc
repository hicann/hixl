/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include "adxl/comm_channel.h"
#include "hixl/hixl.h"
#include "adxl/channel_manager.h"
#include "adxl/channel_msg_handler.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"
#include "depends/mmpa/src/mmpa_stub.h"
namespace hixl {
namespace {
constexpr char kClientPoolClientAddr[] = "127.0.0.1:26110";
constexpr char kClientPoolServer0Addr[] = "127.0.0.1:26111";
constexpr char kClientPoolServer1Addr[] = "127.0.0.1:26112";
constexpr char kClientPoolServer2Addr[] = "127.0.0.1:26113";
constexpr char kClientPoolServer3Addr[] = "127.0.0.1:26114";
constexpr char kServerPoolServerAddr[] = "127.0.0.1:27100";
constexpr char kServerPoolClient0Addr[] = "127.0.0.1:27101";
constexpr char kServerPoolClient1Addr[] = "127.0.0.1:27102";
constexpr char kServerPoolClient2Addr[] = "127.0.0.1:27103";
constexpr char kServerPoolClient3Addr[] = "127.0.0.1:27104";
constexpr char kTransferEvictionServerAddr[] = "127.0.0.1:27105";
constexpr char kTransferEvictionClient0Addr[] = "127.0.0.1:27106";
constexpr char kTransferEvictionClient1Addr[] = "127.0.0.1:27107";
constexpr char kTransferEvictionClient2Addr[] = "127.0.0.1:27108";
constexpr char kTransferEvictionClient3Addr[] = "127.0.0.1:27110";
constexpr char kWaterlineTestAddr[] = "127.0.0.1:27111";
constexpr char kConcurrentSyncClientAddr[] = "127.0.0.1:26120";
constexpr char kConcurrentSyncServer0Addr[] = "127.0.0.1:26121";
constexpr char kConcurrentSyncServer1Addr[] = "127.0.0.1:26122";
constexpr char kConcurrentSyncServer2Addr[] = "127.0.0.1:26123";
constexpr char kConcurrentSyncServer3Addr[] = "127.0.0.1:26124";
constexpr char kConcurrentAsyncClientAddr[] = "127.0.0.1:26130";
constexpr char kConcurrentAsyncServer0Addr[] = "127.0.0.1:26131";
constexpr char kConcurrentAsyncServer1Addr[] = "127.0.0.1:26132";
constexpr char kConcurrentAsyncServer2Addr[] = "127.0.0.1:26133";
constexpr char kConcurrentAsyncServer3Addr[] = "127.0.0.1:26134";
constexpr char kConcurrentSyncSingleClientAddr[] = "127.0.0.1:26140";
constexpr char kConcurrentSyncSingleServerAddr[] = "127.0.0.1:26141";
constexpr char kConcurrentAsyncSingleClientAddr[] = "127.0.0.1:26150";
constexpr char kConcurrentAsyncSingleServerAddr[] = "127.0.0.1:26151";
constexpr char kResourceExhaustedClientAddr[] = "127.0.0.1:26160";
constexpr char kResourceExhaustedServer0Addr[] = "127.0.0.1:26161";
constexpr char kResourceExhaustedServer1Addr[] = "127.0.0.1:26162";
constexpr char kResourceExhaustedServer2Addr[] = "127.0.0.1:26163";
constexpr char kResourceExhaustedServer3Addr[] = "127.0.0.1:26164";
}  // namespace

class ChannelPoolSystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    llm::MockMmpaForHcclApi::Install();
    llm::AutoCommResRuntimeMock::Install();
    llm::HcclAdapter::GetInstance().Initialize();
    options_["GlobalResourceConfig"] =
        R"({"channel_pool.max_channel":"10","channel_pool.high_waterline":"0.3","channel_pool.low_waterline":"0.1"})";
  }

  void TearDown() override {
    llm::HcclAdapter::GetInstance().Finalize();
    llm::MockMmpaForHcclApi::Reset();
    llm::AutoCommResRuntimeMock::Reset();
  }

  void InitFourServers(const char *const addrs[4], Hixl servers[4]) {
    for (int i = 0; i < 4; ++i) {
      llm::AutoCommResRuntimeMock::SetDevice(i + 1);
      EXPECT_EQ(servers[i].Initialize(addrs[i], options_), SUCCESS);
    }
  }

  static void JoinThread(std::thread &thread) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  void StartTransferThreads(Hixl &client, const TransferOpDesc &desc, const AscendString &busy_addr0,
                            const AscendString &busy_addr1, bool use_async, std::thread &transfer_thread1,
                            std::thread &transfer_thread2) {
    if (use_async) {
      auto transfer_async_func = [&client, &desc](const AscendString &server_addr) {
        TransferReq req = nullptr;
        client.TransferAsync(server_addr, READ, {desc}, {}, req);
      };
      transfer_thread1 = std::thread(transfer_async_func, busy_addr0);
      transfer_thread2 = std::thread(transfer_async_func, busy_addr1);
      return;
    }
    auto transfer_sync_func = [&client, &desc](const AscendString &server_addr) {
      client.TransferSync(server_addr, READ, {desc});
      std::this_thread::sleep_for(std::chrono::microseconds(5000));
    };
    transfer_thread1 = std::thread(transfer_sync_func, busy_addr0);
    transfer_thread2 = std::thread(transfer_sync_func, busy_addr1);
  }

  void RunConcurrentTransferEvictionTest(const char *client_addr, const char *const server_addrs[4],
                                         const AscendString &idle_addr0, const AscendString &idle_addr1,
                                         const AscendString &busy_addr0, const AscendString &busy_addr1,
                                         uint64_t mock_mem_len, bool use_async) {
    llm::AutoCommResRuntimeMock::SetDevice(0);
    Hixl client;
    EXPECT_EQ(client.Initialize(client_addr, options_), SUCCESS);
    Hixl servers[4];
    InitFourServers(server_addrs, servers);
    EXPECT_EQ(client.Connect(idle_addr0), SUCCESS);
    EXPECT_EQ(client.Connect(idle_addr1), SUCCESS);

    hixl::MemDesc mem{};
    mem.addr = 1134;
    mem.len = mock_mem_len;
    MemHandle client_handle = nullptr;
    EXPECT_EQ(client.RegisterMem(mem, MEM_DEVICE, client_handle), SUCCESS);
    MemHandle server0_handle = nullptr;
    MemHandle server1_handle = nullptr;
    EXPECT_EQ(servers[0].RegisterMem(mem, MEM_DEVICE, server0_handle), SUCCESS);
    EXPECT_EQ(servers[1].RegisterMem(mem, MEM_DEVICE, server1_handle), SUCCESS);

    int32_t src = 1;
    int32_t dst = use_async ? 12 : 2;
    TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};

    std::thread transfer_thread1;
    std::thread transfer_thread2;
    StartTransferThreads(client, desc, busy_addr0, busy_addr1, use_async, transfer_thread1, transfer_thread2);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(client.Disconnect(idle_addr0), NOT_CONNECTED);
    EXPECT_EQ(client.Disconnect(idle_addr1), NOT_CONNECTED);
    EXPECT_EQ(client.Connect(busy_addr0), ALREADY_CONNECTED);
    EXPECT_EQ(client.Connect(busy_addr1), ALREADY_CONNECTED);
    JoinThread(transfer_thread1);
    JoinThread(transfer_thread2);

    EXPECT_EQ(client.DeregisterMem(client_handle), SUCCESS);
    EXPECT_EQ(servers[0].DeregisterMem(server0_handle), SUCCESS);
    EXPECT_EQ(servers[1].DeregisterMem(server1_handle), SUCCESS);
    client.Finalize();
    for (int i = 0; i < 4; ++i) {
      servers[i].Finalize();
    }
  }

  void RunResourceExhaustedTest() {
    options_["GlobalResourceConfig"] =
        R"({"channel_pool.max_channel":"3","channel_pool.high_waterline":"0.67","channel_pool.low_waterline":"0.33"})";
    llm::AutoCommResRuntimeMock::SetDevice(0);
    Hixl client_exhausted;
    EXPECT_EQ(client_exhausted.Initialize(kResourceExhaustedClientAddr, options_), SUCCESS);
    const char *server_addrs[] = {
        kResourceExhaustedServer0Addr,
        kResourceExhaustedServer1Addr,
        kResourceExhaustedServer2Addr,
        kResourceExhaustedServer3Addr,
    };
    Hixl servers_async[4];
    InitFourServers(server_addrs, servers_async);

    hixl::MemDesc mem{};
    mem.addr = 1134;
    mem.len = 8;
    MemHandle client_handle = nullptr;
    EXPECT_EQ(client_exhausted.RegisterMem(mem, MEM_DEVICE, client_handle), SUCCESS);
    MemHandle server1_handle = nullptr;
    MemHandle server2_handle = nullptr;
    MemHandle server3_handle = nullptr;
    EXPECT_EQ(servers_async[0].RegisterMem(mem, MEM_DEVICE, server1_handle), SUCCESS);
    EXPECT_EQ(servers_async[1].RegisterMem(mem, MEM_DEVICE, server2_handle), SUCCESS);
    EXPECT_EQ(servers_async[2].RegisterMem(mem, MEM_DEVICE, server3_handle), SUCCESS);

    int32_t src = 10;
    int32_t dst = 12;
    TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
    auto transfer_async_func = [&client_exhausted, &desc](const AscendString &server_addr) {
      TransferReq req = nullptr;
      client_exhausted.TransferAsync(server_addr, READ, {desc}, {}, req);
    };

    std::thread transfer_thread1(transfer_async_func, kResourceExhaustedServer0Addr);
    std::thread transfer_thread2(transfer_async_func, kResourceExhaustedServer1Addr);
    std::thread transfer_thread3(transfer_async_func, kResourceExhaustedServer2Addr);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(client_exhausted.Connect(kResourceExhaustedServer3Addr), RESOURCE_EXHAUSTED);
    JoinThread(transfer_thread1);
    JoinThread(transfer_thread2);
    JoinThread(transfer_thread3);
    client_exhausted.Finalize();
    for (int i = 0; i < 4; ++i) {
      servers_async[i].Finalize();
    }
  }

  std::map<AscendString, AscendString> options_;
};

TEST_F(ChannelPoolSystemTest, ClientChannelPoolSystemTest) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl client_;
  client_.Initialize(kClientPoolClientAddr, options_);
  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl server_;
  server_.Initialize(kClientPoolServer0Addr, options_);
  // set device 2
  llm::AutoCommResRuntimeMock::SetDevice(2);
  Hixl server1_;
  server1_.Initialize(kClientPoolServer1Addr, options_);
  // set device 3
  llm::AutoCommResRuntimeMock::SetDevice(3);
  Hixl server2_;
  server2_.Initialize(kClientPoolServer2Addr, options_);
  // set device 4
  llm::AutoCommResRuntimeMock::SetDevice(4);
  Hixl server3_;
  server3_.Initialize(kClientPoolServer3Addr, options_);
  EXPECT_EQ(client_.Connect(kClientPoolServer0Addr), SUCCESS);
  EXPECT_EQ(client_.Connect(kClientPoolServer0Addr), ALREADY_CONNECTED);
  EXPECT_EQ(client_.Connect(kClientPoolServer1Addr), SUCCESS);
  EXPECT_EQ(client_.Connect(kClientPoolServer2Addr), SUCCESS);
  EXPECT_EQ(client_.Connect(kClientPoolServer3Addr), SUCCESS);
  // sleep 500 ms for eviction
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  EXPECT_EQ(client_.Disconnect(kClientPoolServer0Addr), NOT_CONNECTED);
  EXPECT_EQ(client_.Disconnect(kClientPoolServer1Addr), NOT_CONNECTED);
  EXPECT_EQ(client_.Connect(kClientPoolServer0Addr), SUCCESS);

  client_.Finalize();
  server_.Finalize();
  server1_.Finalize();
  server2_.Finalize();
  server3_.Finalize();
}

TEST_F(ChannelPoolSystemTest, ServerChannelPoolSystemTest) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl server_;
  server_.Initialize(kServerPoolServerAddr, options_);
  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl client_;
  client_.Initialize(kServerPoolClient0Addr, options_);
  // set device 2
  llm::AutoCommResRuntimeMock::SetDevice(2);
  Hixl client1_;
  client1_.Initialize(kServerPoolClient1Addr, options_);
  // set device 3
  llm::AutoCommResRuntimeMock::SetDevice(3);
  Hixl client2_;
  client2_.Initialize(kServerPoolClient2Addr, options_);
  // set device 4
  llm::AutoCommResRuntimeMock::SetDevice(4);
  Hixl client3_;
  client3_.Initialize(kServerPoolClient3Addr, options_);
  EXPECT_EQ(client_.Connect(kServerPoolServerAddr), SUCCESS);
  EXPECT_EQ(client_.Connect(kServerPoolServerAddr), ALREADY_CONNECTED);
  EXPECT_EQ(client1_.Connect(kServerPoolServerAddr), SUCCESS);
  EXPECT_EQ(client2_.Connect(kServerPoolServerAddr), SUCCESS);
  EXPECT_EQ(client3_.Connect(kServerPoolServerAddr), SUCCESS);
  // sleep 2000 ms for eviction
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  EXPECT_EQ(client_.Disconnect(kServerPoolServerAddr), NOT_CONNECTED);
  EXPECT_EQ(client1_.Disconnect(kServerPoolServerAddr), NOT_CONNECTED);
  EXPECT_EQ(client_.Connect(kServerPoolServerAddr), SUCCESS);

  server_.Finalize();
  client_.Finalize();
  client1_.Finalize();
  client2_.Finalize();
  client3_.Finalize();
}

TEST_F(ChannelPoolSystemTest, ClientDisconnectHandling) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  EXPECT_EQ(engine1.Initialize(kTransferEvictionServerAddr, options_), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  EXPECT_EQ(engine2.Initialize(kTransferEvictionClient0Addr, options_), SUCCESS);

  hixl::MemDesc mem{};
  // mock addr 1234
  mem.addr = 1234;
  // mock len 10
  mem.len = 10;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(mem, MEM_DEVICE, handle1), SUCCESS);

  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(mem, MEM_DEVICE, handle2), SUCCESS);

  int32_t src = 1;
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync(kTransferEvictionClient0Addr, READ, {desc}), SUCCESS);
  // after transfer, src set to 2
  EXPECT_EQ(src, 2);
  src = 1;
  EXPECT_EQ(engine1.TransferSync(kTransferEvictionClient0Addr, WRITE, {desc}), SUCCESS);
  EXPECT_EQ(dst, 1);

  EXPECT_EQ(engine1.Disconnect(kTransferEvictionClient0Addr), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(ChannelPoolSystemTest, TestEvictionWithTransfer) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  EXPECT_EQ(engine1.Initialize(kTransferEvictionServerAddr, options_), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  EXPECT_EQ(engine2.Initialize(kTransferEvictionClient0Addr, options_), SUCCESS);
  // set device 2
  llm::AutoCommResRuntimeMock::SetDevice(2);
  Hixl engine3;
  EXPECT_EQ(engine3.Initialize(kTransferEvictionClient1Addr, options_), SUCCESS);
  // set device 3
  llm::AutoCommResRuntimeMock::SetDevice(3);
  Hixl engine4;
  EXPECT_EQ(engine4.Initialize(kTransferEvictionClient2Addr, options_), SUCCESS);
  // set device 4
  llm::AutoCommResRuntimeMock::SetDevice(4);
  Hixl engine5;
  EXPECT_EQ(engine5.Initialize(kTransferEvictionClient3Addr, options_), SUCCESS);

  hixl::MemDesc mem_{};
  // mock addr 1234
  mem_.addr = 1234;
  // mock len 10
  mem_.len = 10;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(mem_, MEM_DEVICE, handle1), SUCCESS);

  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(mem_, MEM_DEVICE, handle2), SUCCESS);

  int32_t src = 1;
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.Connect(kTransferEvictionClient0Addr), SUCCESS);
  EXPECT_EQ(engine1.TransferSync(kTransferEvictionClient0Addr, READ, {desc}), SUCCESS);
  EXPECT_EQ(engine1.Connect(kTransferEvictionClient1Addr), SUCCESS);
  EXPECT_EQ(engine1.Connect(kTransferEvictionClient2Addr), SUCCESS);
  EXPECT_EQ(engine1.Connect(kTransferEvictionClient3Addr), SUCCESS);
  // sleep 200 ms for eviction
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  EXPECT_EQ(engine1.Disconnect(kTransferEvictionClient1Addr), NOT_CONNECTED);
  EXPECT_EQ(engine1.Disconnect(kTransferEvictionClient2Addr), NOT_CONNECTED);
  EXPECT_EQ(engine1.Connect(kTransferEvictionClient0Addr), ALREADY_CONNECTED);
  EXPECT_EQ(engine1.Disconnect(kTransferEvictionClient0Addr), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
  engine3.Finalize();
  engine4.Finalize();
  engine5.Finalize();
}

TEST_F(ChannelPoolSystemTest, TestWaterline) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;

  options_["GlobalResourceConfig"] = "invalid json string";
  EXPECT_EQ(engine1.Initialize(kWaterlineTestAddr, options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] =
      R"({"channel_pool.max_channel":"10","channel_pool.high_waterline":"0.1","channel_pool.low_waterline":"0.3"})";
  EXPECT_EQ(engine1.Initialize(kWaterlineTestAddr, options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] =
      R"({"channel_pool.max_channel":"0","channel_pool.high_waterline":"0.3","channel_pool.low_waterline":"0.1"})";
  EXPECT_EQ(engine1.Initialize(kWaterlineTestAddr, options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] =
      R"({"channel_pool.max_channel":"999","channel_pool.high_waterline":"0.3","channel_pool.low_waterline":"0.1"})";
  EXPECT_EQ(engine1.Initialize(kWaterlineTestAddr, options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] = R"({"channel_pool.max_channel":"10"})";
  EXPECT_EQ(engine1.Initialize(kWaterlineTestAddr, options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] =
      R"({"channel_pool.max_channel":"10","channel_pool.high_waterline":"1.0","channel_pool.low_waterline":"0.1"})";
  EXPECT_EQ(engine1.Initialize(kWaterlineTestAddr, options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] =
      R"({"channel_pool.max_channel":"10","channel_pool.high_waterline":"0.3","channel_pool.low_waterline":"0.0"})";
  EXPECT_EQ(engine1.Initialize(kWaterlineTestAddr, options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] =
      R"({"channel_pool.max_channel":"10","channel_pool.high_waterline":"NaN","channel_pool.low_waterline":"0.1"})";
  EXPECT_EQ(engine1.Initialize(kWaterlineTestAddr, options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] =
      R"({"channel_pool.max_channel":"NaN","channel_pool.high_waterline":"0.3","channel_pool.low_waterline":"0.1"})";
  EXPECT_EQ(engine1.Initialize(kWaterlineTestAddr, options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] =
      R"({"channel_pool.max_channel":"10","channel_pool.high_waterline":"0.3","channel_pool.low_waterline":"0.1"})";
  EXPECT_EQ(engine1.Initialize(kWaterlineTestAddr, options_), SUCCESS);
}

TEST_F(ChannelPoolSystemTest, ClientDisconnectHandlingAsync) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1_;
  EXPECT_EQ(engine1_.Initialize(kTransferEvictionServerAddr, options_), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2_;
  EXPECT_EQ(engine2_.Initialize(kTransferEvictionClient0Addr, options_), SUCCESS);

  hixl::MemDesc mem{};
  // mock addr 1234
  mem.addr = 1234;
  // mock len 10
  mem.len = 10;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(engine1_.RegisterMem(mem, MEM_DEVICE, handle1), SUCCESS);

  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2_.RegisterMem(mem, MEM_DEVICE, handle2), SUCCESS);

  // set src context to 1
  int32_t src = 1;
  // set dst context to 2
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  TransferReq req = nullptr;
  EXPECT_EQ(engine1_.TransferAsync(kTransferEvictionClient0Addr, READ, {desc}, {}, req), SUCCESS);
  // after transfer, src set to 2
  EXPECT_EQ(src, 2);
  src = 1;
  EXPECT_EQ(engine1_.TransferAsync(kTransferEvictionClient0Addr, WRITE, {desc}, {}, req), SUCCESS);
  EXPECT_EQ(dst, 1);

  EXPECT_EQ(engine1_.Disconnect(kTransferEvictionClient0Addr), SUCCESS);
  EXPECT_EQ(engine1_.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2_.DeregisterMem(handle2), SUCCESS);
  engine1_.Finalize();
  engine2_.Finalize();
}

TEST_F(ChannelPoolSystemTest, ConcurrentTransferSyncAndEviction) {
  const char *server_addrs[] = {
      kConcurrentSyncServer0Addr,
      kConcurrentSyncServer1Addr,
      kConcurrentSyncServer2Addr,
      kConcurrentSyncServer3Addr,
  };
  RunConcurrentTransferEvictionTest(kConcurrentSyncClientAddr, server_addrs, kConcurrentSyncServer2Addr,
                                    kConcurrentSyncServer3Addr, kConcurrentSyncServer0Addr, kConcurrentSyncServer1Addr,
                                    10, false);
}

TEST_F(ChannelPoolSystemTest, ConcurrentTransferAsyncAndEviction) {
  const char *server_addrs[] = {
      kConcurrentAsyncServer0Addr,
      kConcurrentAsyncServer1Addr,
      kConcurrentAsyncServer2Addr,
      kConcurrentAsyncServer3Addr,
  };
  RunConcurrentTransferEvictionTest(kConcurrentAsyncClientAddr, server_addrs, kConcurrentAsyncServer2Addr,
                                    kConcurrentAsyncServer3Addr, kConcurrentAsyncServer0Addr,
                                    kConcurrentAsyncServer1Addr, 11, true);
}

// Test multiple concurrent TransferSync calls, ensure none return NOT_CONNECTED or ALREADY_CONNECTED
TEST_F(ChannelPoolSystemTest, ConcurrentTransfersWithoutConnectStatusErrors) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl client;
  EXPECT_EQ(client.Initialize(kConcurrentSyncSingleClientAddr, options_), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl server;
  EXPECT_EQ(server.Initialize(kConcurrentSyncSingleServerAddr, options_), SUCCESS);

  // Register memory for transfer
  hixl::MemDesc mem{};
  mem.addr = 1234;  // mock memory address 1234
  mem.len = 1024;   // mock memory length 1024
  MemHandle client_handle = nullptr;
  EXPECT_EQ(client.RegisterMem(mem, MEM_DEVICE, client_handle), SUCCESS);

  MemHandle server_handle = nullptr;
  EXPECT_EQ(server.RegisterMem(mem, MEM_DEVICE, server_handle), SUCCESS);
  // make 10 concurrent threads
  const int total_threads = 5;
  // set timeout to 1000 ms
  const int32_t timeout = 5000;

  // Create a vector of threads
  std::vector<std::thread> threads;
  threads.reserve(total_threads);

  // Prepare transfer data
  int32_t src = 1;
  // mock dst content 2
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};

  for (int i = 0; i < total_threads; ++i) {
    threads.emplace_back([&client, &desc, timeout]() {
      Status ret = client.TransferSync(kConcurrentSyncSingleServerAddr, READ, {desc}, timeout);
      EXPECT_EQ(ret, SUCCESS);
    });
  }

  for (auto &thread : threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  EXPECT_EQ(client.DeregisterMem(client_handle), SUCCESS);
  EXPECT_EQ(server.DeregisterMem(server_handle), SUCCESS);
  client.Finalize();
  server.Finalize();
}

TEST_F(ChannelPoolSystemTest, ConcurrentAsyncTransfersWithoutConnectStatusErrors) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl client;
  EXPECT_EQ(client.Initialize(kConcurrentAsyncSingleClientAddr, options_), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl server;
  EXPECT_EQ(server.Initialize(kConcurrentAsyncSingleServerAddr, options_), SUCCESS);

  // Register memory for transfer
  hixl::MemDesc mem{};
  mem.addr = 5678;  // mock memory address 5678
  mem.len = 2048;   // mock memory length 2048
  MemHandle client_handle = nullptr;
  EXPECT_EQ(client.RegisterMem(mem, MEM_DEVICE, client_handle), SUCCESS);

  MemHandle server_handle = nullptr;
  EXPECT_EQ(server.RegisterMem(mem, MEM_DEVICE, server_handle), SUCCESS);
  // make 25 concurrent threads
  const int total_threads = 25;
  // Create a vector of threads
  std::vector<std::thread> threads;
  threads.reserve(total_threads);
  // Launch multiple concurrent threads, all calling TransferAsync
  for (int i = 0; i < total_threads; ++i) {
    threads.emplace_back([&client]() {
      // mock src content 1
      int32_t src = 1;
      // mock dst content 2
      int32_t dst = 2;
      TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
      // This will trigger ConnectWhenTransfer internally
      TransferReq req = nullptr;
      Status ret = client.TransferAsync(kConcurrentAsyncSingleServerAddr, READ, {desc}, {}, req);
      EXPECT_EQ(ret, SUCCESS);
    });
  }

  for (auto &t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }
  EXPECT_EQ(client.DeregisterMem(client_handle), SUCCESS);
  EXPECT_EQ(server.DeregisterMem(server_handle), SUCCESS);
  client.Finalize();
  server.Finalize();
}

TEST_F(ChannelPoolSystemTest, TestResourceExhausted) {
  RunResourceExhaustedTest();
}
}  // namespace hixl
