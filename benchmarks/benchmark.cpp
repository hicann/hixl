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
constexpr int32_t kWaitTransTime = 20;
constexpr int32_t kExpectedArgCnt = 8;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalEngine = 2;
constexpr uint32_t kArgIndexRemoteEngine = 3;
constexpr uint32_t kArgIndexTcpPort = 4;
constexpr uint32_t kArgIndexTransferMode = 5;
constexpr uint32_t kArgIndexTransferOp = 6;
constexpr uint32_t kArgIndexUseBufferPool = 7;
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

int32_t Initialize(Hixl &hixl_engine, const char *local_engine, bool use_buffer_pool) {
  std::map<AscendString, AscendString> options;
  // 在不需要使用中转buffer进行传输的场景下，关闭中转内存池
  if (!use_buffer_pool) {
    options["BufferPool"] = "0:0";
  }
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

void Disconnect(Hixl &hixl_engine, const char *remote_engine, bool connected) {
  if (connected) {
    auto ret = hixl_engine.Disconnect(remote_engine);
    if (ret != SUCCESS) {
      printf("[ERROR] Disconnect failed, ret = %u\n", ret);
    } else {
      printf("[INFO] Disconnect success\n");
    }
  }
}

int32_t Transfer(Hixl &hixl_engine, int32_t &src, const char *remote_engine, uint64_t dst_addr, TransferOp transfer_op) {
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
    auto ret = hixl_engine.TransferSync(remote_engine, transfer_op, descs, 1000 * kWaitTransTime);
    if (ret != SUCCESS) {
      printf("[ERROR] TransferSync failed, ret = %u\n", ret);
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

void Finalize(Hixl &hixl_engine, bool need_register, bool is_host, const std::vector<MemHandle> &handles,
                    const std::vector<void *> &buffers = {}) {
  if (need_register) {
    for (const auto &handle : handles) {
      auto ret = hixl_engine.DeregisterMem(handle);
      if (ret != 0) {
        printf("[ERROR] DeregisterMem failed, ret = %u\n", ret);
      } else {
        printf("[INFO] DeregisterMem success\n");
      }
    }
  }
  if (is_host) {
    for (const auto &buffer : buffers) {
      aclrtFreeHost(buffer);
    }
  } else {
    for (const auto &buffer : buffers) {
      aclrtFree(buffer);
    }
  }
  hixl_engine.Finalize();
}

int32_t RunClient(const char *local_engine, const char *remote_engine, uint16_t tcp_port, const std::string &transfer_mode, 
                  TransferOp transfer_op, bool use_buffer_pool) {
  printf("[INFO] client start\n");

  // 通过TCP接收Server侧的内存地址
  TCPServer tcp_server;
  uint64_t remote_addr = 0;
  if (!tcp_server.StartServer(tcp_port)) {
    printf("[ERROR] Failed to start TCP server.\n");
    return -1;
  }
  printf("[INFO] TCP server started.\n");
  if (!tcp_server.AcceptConnection()) {
    return -1;
  }
  remote_addr = tcp_server.ReceiveUint64();
  if (remote_addr != 0) {
    printf("[INFO] Success to receive server mem addr: 0x%lx\n", remote_addr);
  }

  // 1. 初始化
  Hixl hixl_engine;
  if (Initialize(hixl_engine, local_engine, use_buffer_pool) != 0) {
    printf("[ERROR] Initialize Hixl failed\n");
    return -1;
  }

  // 2. 注册内存地址
  int32_t *src = nullptr;
  MemHandle handle = nullptr;
  bool connected = false;
  bool is_host = (transfer_mode == "h2d" || transfer_mode == "h2h");
  if (is_host) {
    CHECK_ACL(aclrtMallocHost(reinterpret_cast<void **>(&src), kTransferMemSize)); 
  } else {
    CHECK_ACL(aclrtMalloc(reinterpret_cast<void **>(&src), kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY));
  }

  bool need_register = !(is_host && use_buffer_pool);
  if (need_register) {
    MemDesc desc{};
    desc.addr = reinterpret_cast<uintptr_t>(src);
    desc.len = kTransferMemSize;
    auto ret = hixl_engine.RegisterMem(desc, is_host ? MemType::MEM_HOST : MEM_DEVICE, handle);
    if (ret != SUCCESS) {
      printf("[ERROR] RegisterMem failed, ret = %u\n", ret);
      Finalize(hixl_engine, need_register, is_host, {handle}, {src});
      return -1;
    }
    printf("[INFO] RegisterMem success\n");
  }

  // 等待server注册完成
  if (tcp_server.ReceiveTaskStatus()) {
    printf("[INFO] Server RegisterMem success\n");
  }

  // 3. 与server建链
  if (Connect(hixl_engine, remote_engine) != 0) {
    Finalize(hixl_engine, need_register, is_host, {handle}, {src});
    return -1;
  }
  connected = true;

  // 4. 与server进行内存传输
  if (Transfer(hixl_engine, *src, remote_engine, remote_addr, transfer_op) != 0) {
    Disconnect(hixl_engine, remote_engine, connected);
    Finalize(hixl_engine, need_register, is_host, {handle}, {src});
    return -1;
  }

  // 断链
  Disconnect(hixl_engine, remote_engine, connected);
  // 通过TCP通知Server侧已传输完成
  (void)tcp_server.SendTaskStatus();
  tcp_server.DisConnectClient();
  tcp_server.StopServer();

  // 5. 解注册，释放内存，析构
  Finalize(hixl_engine, need_register, is_host, {handle}, {src});
  printf("[INFO] Client Sample end\n");
  return 0;
}

int32_t RunServer(const char *local_engine, const char *remote_engine, uint16_t tcp_port, const std::string &transfer_mode, 
                  bool use_buffer_pool) {
  printf("[INFO] server start\n");
  // 1. 初始化
  Hixl hixl_engine;
  if (Initialize(hixl_engine, local_engine, use_buffer_pool) != 0) {
    printf("[ERROR] Initialize Hixl failed\n");
    return -1;
  }
  // 2. 注册内存地址
  void *buffer = nullptr;
  bool is_host = (transfer_mode == "d2h" || transfer_mode == "h2h");
  if (is_host){
    CHECK_ACL(aclrtMallocHost(&buffer, kTransferMemSize));
  } else{
    CHECK_ACL(aclrtMalloc(&buffer, kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY));
  }
  auto addr = reinterpret_cast<uintptr_t>(buffer);

  // 通过TCP传输内存地址到Client侧
  TCPClient tcp_client;
  if (!tcp_client.ConnectToServer(remote_engine, tcp_port)) {
    return -1;
  }
  (void)tcp_client.SendUint64(addr);

  MemHandle handle = nullptr;
  auto mem_type = is_host ? MemType::MEM_HOST : MemType::MEM_DEVICE;

  bool need_register = !(use_buffer_pool && transfer_mode == "d2h");
  if (need_register){
    MemDesc desc{};
    desc.addr = addr;
    desc.len = kTransferMemSize;
    auto ret = hixl_engine.RegisterMem(desc, mem_type, handle);
    if (ret != SUCCESS) {
      printf("[ERROR] RegisterMem failed, ret = %u\n", ret);
      Finalize(hixl_engine, need_register, is_host, {handle}, {buffer});
      return -1;
    }
    // 3. RegisterMem成功后，将地址保存到本地文件中等待client读取
    printf("[INFO] RegisterMem success, addr:%p\n", buffer);
  }

  // 通过TCP通知Client侧内存已注册
  (void)tcp_client.SendTaskStatus();

  // 4. 等待client transfer
  printf("[INFO] Wait transfer begin\n");
  if (tcp_client.ReceiveTaskStatus()) {
    printf("[INFO] Wait transfer end\n");
  }
  tcp_client.Disconnect();

  // 5. 解注册，释放内存，析构
  Finalize(hixl_engine, need_register, is_host, {handle}, {buffer});
  printf("[INFO] Server Sample end\n");
  return 0;
}

int32_t main(int32_t argc, char **argv) {
  bool is_client = false;
  bool use_buffer_pool = false;
  std::string device_id;
  std::string local_engine;
  std::string remote_engine;
  std::string tcp_port_str;
  std::string transfer_mode;
  std::string transfer_op_str;
  std::string use_buffer_pool_str;
  if (argc == kExpectedArgCnt) {
    device_id = argv[kArgIndexDeviceId];
    local_engine = argv[kArgIndexLocalEngine];
    remote_engine = argv[kArgIndexRemoteEngine];
    tcp_port_str = argv[kArgIndexTcpPort];
    transfer_mode = argv[kArgIndexTransferMode];
    transfer_op_str = argv[kArgIndexTransferOp];
    use_buffer_pool_str = argv[kArgIndexUseBufferPool];
    use_buffer_pool = (use_buffer_pool_str == "true");
    is_client = (remote_engine.find(':') != std::string::npos);
    printf("[INFO] device_id = %s, local_engine = %s, remote_engine = %s, tcp_port = %s, transfer_mode = %s, transfer_op = %s, use_buffer_pool = %s\n", 
            device_id.c_str(), local_engine.c_str(), remote_engine.c_str(), tcp_port_str.c_str(), 
            transfer_mode.c_str(), transfer_op_str.c_str(), use_buffer_pool_str.c_str());
  } else {
    printf("[ERROR] Expect 7 args(device_id, local_engine, remote_engine, tcp_port, transfer_mode, transfer_op, use_buffer_pool), but got %d\n", argc - 1);
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

  if (transfer_mode != "d2d" && transfer_mode != "h2d" && transfer_mode != "d2h" && transfer_mode != "h2h"){
    printf("[ERROR] Invalid value for transfer_mode: %s\n", transfer_mode.c_str());
    return -1;
  }

  if (transfer_op_str != "write" && transfer_op_str != "read") {
    printf("[ERROR] Invalid value for transfer_op: %s\n", transfer_op_str.c_str());
    return -1;
  }
  TransferOp transfer_op = (transfer_op_str == "read") ? TransferOp::READ : TransferOp::WRITE;

  int32_t ret = 0;
  if (is_client) {
    ret = RunClient(local_engine.c_str(), remote_engine.c_str(), tcp_port, transfer_mode, transfer_op, use_buffer_pool);
  } else {
    ret = RunServer(local_engine.c_str(), remote_engine.c_str(), tcp_port, transfer_mode, use_buffer_pool);
  }
  CHECK_ACL(aclrtResetDevice(device));
  return ret;
}