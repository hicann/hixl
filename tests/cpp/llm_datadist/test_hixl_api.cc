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
#include <gmock/gmock.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "hixl/hixl.h"
#include "adxl/channel_manager.h"
#include "dlog_pub.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"

using namespace std;
using namespace llm;
using namespace ::testing;
using ::testing::Invoke;
using ::testing::Mock;

namespace hixl {
namespace {
constexpr size_t kTransferSize = 1024;
constexpr uint8_t kDataPattern = 0xAA;
constexpr uint8_t kInitPattern = 0;
constexpr int32_t kDeviceId0 = 0;
constexpr int32_t kDeviceId1 = 1;
const std::string kIp = "127.0.0.1";
const std::string kPort = "26001";
const std::string kEngine1Ip = kIp;
const std::string kEngine2Ip = kIp + ":" + kPort;
const char *const kEnableFabricMem = "1";
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

 private:
  bool CheckIpv6Supported() {
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) {
      return false;
    }
    struct sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    (void) inet_pton(AF_INET6, "::1", &addr.sin6_addr);
    addr.sin6_port = htons(0U);
    bool ok = (connect(fd, (sockaddr*)&addr, sizeof(addr)) != -1 || errno != EADDRNOTAVAIL);
    close(fd);
    return ok;
  }
};

TEST_F(HixlSTest, TestHixl) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  hixl::MemDesc mem{};
  mem.addr = 1234;
  mem.len = 10;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(mem, MEM_DEVICE, handle1), SUCCESS);
  EXPECT_EQ(engine2.RegisterMem(mem, MEM_DEVICE, handle2), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  int32_t src = 1;
  int32_t dst = 2;
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

TEST_F(HixlSTest, TestHixlWithIpv6) {
  auto support_ipv6 = CheckIpv6Supported();
  if (support_ipv6) {
    std::cout << "support ipv6" << std::endl;
    llm::AutoCommResRuntimeMock::SetDevice(0);
    Hixl engine1;
    std::map<AscendString, AscendString> options1;
    EXPECT_EQ(engine1.Initialize("[::1]:26002", options1), SUCCESS);

    llm::AutoCommResRuntimeMock::SetDevice(1);
    Hixl engine2;
    std::map<AscendString, AscendString> options2;
    EXPECT_EQ(engine2.Initialize("[::1]:26003", options2), SUCCESS);

    hixl::MemDesc mem{};
    mem.addr = 1;
    mem.len = 1;
    MemHandle handle1 = nullptr;
    MemHandle handle2 = nullptr;
    EXPECT_EQ(engine1.RegisterMem(mem, MEM_DEVICE, handle1), SUCCESS);
    EXPECT_EQ(engine2.RegisterMem(mem, MEM_DEVICE, handle2), SUCCESS);
    EXPECT_EQ(engine1.Connect("[::1]:26003"), SUCCESS);
    int32_t src = 1;
    int32_t dst = 2;
    TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
    EXPECT_EQ(engine1.TransferSync("[::1]:26003", READ, {desc}), SUCCESS);
    EXPECT_EQ(src, dst);
    src = 1;
    EXPECT_EQ(engine1.TransferSync("[::1]:26003", WRITE, {desc}), SUCCESS);
    EXPECT_EQ(dst, src);
    EXPECT_EQ(engine1.Disconnect("[::1]:26003"), SUCCESS);
    EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
    EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
    engine1.Finalize();
    engine2.Finalize();
  } else {
    std::cout << "not support ipv6" << std::endl;
    Hixl engine1;
    std::map<AscendString, AscendString> options1;
    EXPECT_NE(engine1.Initialize("[::1]:26002", options1), SUCCESS);
  }
}

TEST_F(HixlSTest, TestHixlH2HWithBuffer) {
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
  size_t block_size = 256 * 1024;
  size_t block_num = 4 * 16;
  std::vector<TransferOpDesc> descs;
  for (size_t i = 0; i < block_num; ++i) {
    descs.emplace_back(TransferOpDesc{reinterpret_cast<uintptr_t>(src.data()) + i * block_size,
                                      reinterpret_cast<uintptr_t>(dst.data()) + i * block_size, block_size});
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", WRITE, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }
  dst.assign(size, 2);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlSTest, TestHixlD2HWithBuffer) {
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
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", WRITE, {desc}), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlSTest, TestHixlDefaultBufferPoolD2D) {
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

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);

  // still 16M data.
  size_t block_size = 128 * 1024;
  size_t block_num = 8 * 16;
  std::vector<TransferOpDesc> descs;
  for (size_t i = 0; i < block_num; ++i) {
    descs.emplace_back(TransferOpDesc{reinterpret_cast<uintptr_t>(src.data()) + i * block_size,
                                      reinterpret_cast<uintptr_t>(dst.data()) + i * block_size, block_size});
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", WRITE, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }
  dst.assign(size, 2);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlSTest, TestHixlDisableBufferPoolD2D) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "1";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  options1["BufferPool"] = "0:0";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  options2["BufferPool"] = "0:0";
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

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

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);

  // still 16M data.
  size_t block_size = 128 * 1024;
  size_t block_num = 8 * 16;
  std::vector<TransferOpDesc> descs;
  for (size_t i = 0; i < block_num; ++i) {
    descs.emplace_back(TransferOpDesc{reinterpret_cast<uintptr_t>(src.data()) + i * block_size,
                                      reinterpret_cast<uintptr_t>(dst.data()) + i * block_size, block_size});
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", WRITE, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }
  dst.assign(size, 2);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlSTest, TestHeartbeat) {
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

TEST_F(HixlSTest, TestHixlServerDown) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  engine2.Finalize();
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  engine1.Finalize();
}

TEST_F(HixlSTest, TestHixlFabricMem) {
  llm::AutoCommResRuntimeMock::SetDevice(kDeviceId0);
  Hixl engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_ENABLE_USE_FABRIC_MEM] = kEnableFabricMem;
  EXPECT_EQ(engine1.Initialize(kEngine1Ip.c_str(), options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(kDeviceId1);
  Hixl engine2;
  std::map<AscendString, AscendString> options2;
  options2[OPTION_ENABLE_USE_FABRIC_MEM] = kEnableFabricMem;
  EXPECT_EQ(engine2.Initialize(kEngine2Ip.c_str(), options2), SUCCESS);

  size_t size = kTransferSize;
  std::vector<uint8_t> src(size, kDataPattern);
  std::vector<uint8_t> dst(size, kInitPattern);

  // Register remote mem
  std::vector<uint8_t> remote_buf(size, kInitPattern);
  hixl::MemDesc remote_mem{};
  remote_mem.addr = reinterpret_cast<uintptr_t>(remote_buf.data());
  remote_mem.len = size;
  MemHandle handle2 = nullptr;
  // Fabric mode in stub environment seems to require MEM_HOST to bypass "Device mem is ignored" check in ChannelMsgHandler
  EXPECT_EQ(engine2.RegisterMem(remote_mem, MEM_HOST, handle2), SUCCESS);

  EXPECT_EQ(engine1.Connect(kEngine2Ip.c_str()), SUCCESS);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(remote_buf.data()), size};

  // Write to remote (local representation of remote)
  EXPECT_EQ(engine1.TransferSync(kEngine2Ip.c_str(), WRITE, {desc}), SUCCESS);

  // Read back from remote
  TransferOpDesc read_desc{reinterpret_cast<uintptr_t>(dst.data()), reinterpret_cast<uintptr_t>(remote_buf.data()), size};
  EXPECT_EQ(engine1.TransferSync(kEngine2Ip.c_str(), READ, {read_desc}), SUCCESS);

  // Verify read back data matches written data
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], kDataPattern) << "Verification failed at index " << i;
  }

  EXPECT_EQ(engine1.Disconnect(kEngine2Ip.c_str()), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

}  // namespace hixl
