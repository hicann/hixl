/**
* This program is free software, you can redistribute it and/or modify it.
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This file is a part of the CANN Open Software.
* Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/

#include <cstdio>
#include <thread>
#include <iostream>
#include <vector>
#include "common/tcp_client_server.h"
#include "acl/acl.h"
#include "hixl/hixl.h"

using namespace hixl;
namespace {
constexpr int32_t kWaitRegTime = 5;
constexpr int32_t kWaitTransTime = 20;
constexpr int32_t kExpectedArgCnt = 5;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalEngine = 2;
constexpr uint32_t kArgIndexRemoteEngine = 3;
constexpr uint32_t kArgIndexTcpPort = 4;
constexpr uint32_t kTransferMemSize = 134217728;  // 128M
constexpr uint32_t kBaseBlockSize = 262144;       // 0.25M
constexpr uint32_t kExecuteRepeatNum = 5;
constexpr int32_t kPortMaxValue = 65535;

#define CHECK_ACL(x)                                                                  \
  do {                                                                                \
    aclError __ret = x;                                                               \
    if (__ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl; \
    }                                                                                 \
  } while (0)
}  // namespace

int32_t Initialize(Hixl &hixl_engine, const char *local_engine) {
  std::map<AscendString, AscendString> options;
  auto ret = hixl_engine.Initialize(local_engine, options);
  if (ret != SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] Initialize success\n");
  return 0;
}

int32_t Connect(Hixl &hixl_engine, const char *remote_engine) {
  auto ret = hixl_engine.Connect(remote_engine);
  if (ret != SUCCESS) {
    printf("[ERROR] Connect failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] Connect success\n");
  return 0;
}

int32_t Disconnect(Hixl &hixl_engine, const char *remote_engine) {
  auto ret = hixl_engine.Disconnect(remote_engine);
  if (ret != SUCCESS) {
    printf("[ERROR] Disconnect failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] Disconnect success\n");
  return 0;
}

int32_t Transfer(Hixl &hixl_engine, int32_t &src, const char *remote_engine, uint64_t dst_addr) {
  for (uint32_t i = 0; i <= kExecuteRepeatNum; i++) {
    auto block_size = kBaseBlockSize * (1 << i);
    auto trans_num = kTransferMemSize / block_size;
    std::vector<TransferOpDesc> descs;
    descs.reserve(trans_num);
    for (uint32_t j = 0; j < trans_num; j++) {
      TransferOpDesc desc{};
      desc.local_addr = (reinterpret_cast<uintptr_t>(&src) + j * block_size);
      desc.remote_addr = (reinterpret_cast<uintptr_t>(dst_addr) + j * block_size);
      desc.len = block_size;
      descs.emplace_back(desc);
    }
    const auto start = std::chrono::steady_clock::now();
    auto ret = hixl_engine.TransferSync(remote_engine, WRITE, descs, 1000 * kWaitTransTime);
    if (ret != SUCCESS) {
      printf("[ERROR] TransferSync write failed, ret = %u\n", ret);
      return -1;
    }
    auto time_cost =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    double time_second = static_cast<double>(time_cost) / 1000 / 1000;
    double throughput = static_cast<double>(kTransferMemSize) / 1024 / 1024 / 1024 / time_second;
    printf(
        "[INFO] Transfer success, block size: %u Bytes, transfer num: %u, time cost: %ld us, throughput: %.3lf GB/s\n",
        block_size, trans_num, time_cost, throughput);
  }
  return 0;
}

void ClientFinalize(Hixl &hixl_engine, bool connected, const char *remote_engine, const std::vector<MemHandle> &handles,
                    const std::vector<void *> &host_buffers = {}) {
  if (connected) {
    auto ret = Disconnect(hixl_engine, remote_engine);
    if (ret != 0) {
      printf("[ERROR] Disconnect failed, ret = %d\n", ret);
    } else {
      printf("[INFO] Disconnect success\n");
    }
  }

  for (const auto &handle : handles) {
    auto ret = hixl_engine.DeregisterMem(handle);
    if (ret != 0) {
      printf("[ERROR] DeregisterMem failed, ret = %u\n", ret);
    } else {
      printf("[INFO] DeregisterMem success\n");
    }
  }
  for (const auto &buffer : host_buffers) {
    aclrtFreeHost(buffer);
  }
  hixl_engine.Finalize();
}

void ServerFinalize(Hixl &hixl_engine, const std::vector<MemHandle> &handles,
                    const std::vector<void *> &dev_buffers = {}) {
  for (const auto &handle : handles) {
    auto ret = hixl_engine.DeregisterMem(handle);
    if (ret != 0) {
      printf("[ERROR] DeregisterMem failed, ret = %u\n", ret);
    } else {
      printf("[INFO] DeregisterMem success\n");
    }
  }
  for (const auto &buffer : dev_buffers) {
    aclrtFree(buffer);
  }
  hixl_engine.Finalize();
}

int32_t RunClient(const char *local_engine, const char *remote_engine, uint16_t tcp_port) {
  printf("[INFO] client start\n");

  // 通过TCP接收Server侧的内存地址
  TCPServer tcp_server;
  uint64_t remote_addr = 0;
  if (tcp_server.StartServer(tcp_port)) {
    if (tcp_server.AcceptConnection()) {
      remote_addr = tcp_server.ReceiveUint64();
      if (remote_addr != 0) {
        printf("[INFO] Success to receive server mem addr: 0x%lx\n", remote_addr);
      }
      tcp_server.DisConnectClient();
    }
    tcp_server.StopServer();
  }

  // 1. 初始化
  Hixl hixl_engine;
  if (Initialize(hixl_engine, local_engine) != 0) {
    printf("[ERROR] Initialize Hixl failed\n");
    return -1;
  }
  // 2. 注册内存地址
  int32_t *src = nullptr;
  CHECK_ACL(aclrtMalloc(reinterpret_cast<void **>(&src), kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY));
  bool connected = false;
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(src);
  desc.len = kTransferMemSize;
  MemHandle handle = nullptr;
  auto ret = hixl_engine.RegisterMem(desc, MEM_DEVICE, handle);
  if (ret != SUCCESS) {
    printf("[ERROR] RegisterMem failed, ret = %u\n", ret);
    ClientFinalize(hixl_engine, connected, remote_engine, {handle}, {src});
    return -1;
  }
  printf("[INFO] RegisterMem success\n");

  // 等待server注册完成
  std::this_thread::sleep_for(std::chrono::seconds(kWaitRegTime));

  // 3. 与server建链
  if (Connect(hixl_engine, remote_engine) != 0) {
    ClientFinalize(hixl_engine, connected, remote_engine, {handle}, {src});
    return -1;
  }
  connected = true;

  // 4. 从server get内存，并向server put内存
  if (Transfer(hixl_engine, *src, remote_engine, remote_addr) != 0) {
    ClientFinalize(hixl_engine, connected, remote_engine, {handle}, {src});
    return -1;
  }

  // 5. 解注册，释放内存，析构
  ClientFinalize(hixl_engine, connected, remote_engine, {handle}, {src});
  printf("[INFO] Client Sample end\n");
  return 0;
}

int32_t RunServer(const char *local_engine, const char *remote_engine, uint16_t tcp_port) {
  printf("[INFO] server start\n");
  // 1. 初始化
  Hixl hixl_engine;
  if (Initialize(hixl_engine, local_engine) != 0) {
    printf("[ERROR] Initialize Hixl failed\n");
    return -1;
  }
  // 2. 注册内存地址
  void *buffer = nullptr;
  CHECK_ACL(aclrtMalloc(&buffer, kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY));
  auto addr = reinterpret_cast<uintptr_t>(buffer);

  // 通过TCP传输内存地址到Client侧
  TCPClient tcp_client;
  if (tcp_client.ConnectToServer(remote_engine, tcp_port)) {
    (void)tcp_client.SendUint64(addr);
    tcp_client.Disconnect();
  }

  MemDesc desc{};
  desc.addr = addr;
  desc.len = kTransferMemSize;
  MemHandle handle = nullptr;
  auto ret = hixl_engine.RegisterMem(desc, MEM_DEVICE, handle);
  if (ret != SUCCESS) {
    printf("[ERROR] RegisterMem failed, ret = %u\n", ret);
    ServerFinalize(hixl_engine, {handle}, {buffer});
    return -1;
  }
  // 3. RegisterMem成功后，将地址保存到本地文件中等待client读取
  printf("[INFO] RegisterMem success, addr:%p\n", buffer);

  // 4. 等待client transfer
  printf("[INFO] Wait transfer begin\n");
  std::this_thread::sleep_for(std::chrono::seconds(kWaitTransTime));
  printf("[INFO] Wait transfer end\n");

  // 5. 解注册，释放内存，析构
  ServerFinalize(hixl_engine, {handle}, {buffer});
  printf("[INFO] Server Sample end\n");
  return 0;
}

int32_t main(int32_t argc, char **argv) {
  bool is_client = false;
  std::string device_id;
  std::string local_engine;
  std::string remote_engine;
  std::string tcp_port_str;
  if (argc == kExpectedArgCnt) {
    device_id = argv[kArgIndexDeviceId];
    local_engine = argv[kArgIndexLocalEngine];
    remote_engine = argv[kArgIndexRemoteEngine];
    tcp_port_str = argv[kArgIndexTcpPort];
    is_client = (remote_engine.find(':') != std::string::npos);
    printf("[INFO] device_id = %s, local_engine = %s, remote_engine = %s, tcp_port = %s\n", device_id.c_str(),
           local_engine.c_str(), remote_engine.c_str(), tcp_port_str.c_str());
  } else {
    printf("[ERROR] Expect 4 args(device_id, local_engine, remote_engine, tcp_port), but got %d\n", argc - 1);
    return -1;
  }
  int32_t device = std::stoi(device_id);
  int32_t input_tcp_port = std::stoi(tcp_port_str);
  if (input_tcp_port < 0 || input_tcp_port > kPortMaxValue) {
    printf("[ERROR] Invalid port: %d, should be in 0~65535\n", input_tcp_port);
    return -1;
  }
  auto tcp_port = static_cast<uint16_t>(input_tcp_port);
  CHECK_ACL(aclrtSetDevice(device));

  int32_t ret = 0;
  if (is_client) {
    ret = RunClient(local_engine.c_str(), remote_engine.c_str(), tcp_port);
  } else {
    ret = RunServer(local_engine.c_str(), remote_engine.c_str(), tcp_port);
  }
  CHECK_ACL(aclrtResetDevice(device));
  return ret;
}
