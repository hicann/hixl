/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <numeric>
#include <cstdio>
#include <thread>
#include <iostream>
#include <fstream>
#include "acl/acl.h"
#include "hixl/hixl.h"

using namespace hixl;
namespace {
constexpr int32_t kWaitRegTime = 5;
constexpr int32_t kWaitTransTime = 20;
constexpr int32_t kClientExpectedArgCnt = 4;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalEngine = 2;
constexpr uint32_t kArgIndexRemoteEngine = 3;
constexpr int32_t kServerExpectedArgCnt = 3;
constexpr int32_t kSrcValue = 2;

#define CHECK_ACL(x)                                                                  \
  do {                                                                                \
    aclError __ret = x;                                                               \
    if (__ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl; \
    }                                                                                 \
  } while (0)

const char *GetRecentErrMsg() {
  const char *errmsg = aclGetRecentErrMsg();
  if (errmsg == nullptr) {
    return "no error";
  }
  return errmsg;
}
}  // namespace

int Initialize(Hixl &hixl_engine, const char *local_engine) {
  std::map<AscendString, AscendString> options;
  auto ret = hixl_engine.Initialize(local_engine, options);
  if (ret != SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] Initialize success\n");
  return 0;
}

int Connect(Hixl &hixl_engine, const char *remote_engine) {
  auto ret = hixl_engine.Connect(remote_engine);
  if (ret != SUCCESS) {
    printf("[ERROR] Connect failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] Connect success\n");
  return 0;
}

int Disconnect(Hixl &hixl_engine, const char *remote_engine) {
  auto ret = hixl_engine.Disconnect(remote_engine);
  if (ret != SUCCESS) {
    printf("[ERROR] Disconnect failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] Disconnect success\n");
  return 0;
}

int32_t Transfer(Hixl &hixl_engine, int32_t &src, const char *remote_engine) {
  uintptr_t dst_addr;
  std::ifstream("./tmp") >> std::hex >> dst_addr;

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(dst_addr), sizeof(int32_t)};
  auto ret = hixl_engine.TransferSync(remote_engine, READ, {desc});
  if (ret != SUCCESS) {
    printf("[ERROR] TransferSync read failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] TransferSync read success, src = %d\n", src);

  src = kSrcValue;
  ret = hixl_engine.TransferSync(remote_engine, WRITE, {desc});
  if (ret != SUCCESS) {
    printf("[ERROR] TransferSync write failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] TransferSync write success, src = %d\n", src);
  return 0;
}

void ClientFinalize(Hixl &hixl_engine, bool connected, const char *remote_engine,
                    const std::vector<MemHandle> handles, const std::vector<void *> host_buffers = {}) {
  if (connected) {
    auto ret = Disconnect(hixl_engine, remote_engine);
    if (ret != 0) {
      printf("[ERROR] Disconnect failed, ret = %d\n", ret);
    } else {
      printf("[INFO] Disconnect success\n");
    }
  }

  for (auto handle : handles) {
    auto ret = hixl_engine.DeregisterMem(handle);
    if (ret != 0) {
      printf("[ERROR] DeregisterMem failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    } else {
      printf("[INFO] DeregisterMem success\n");
    }
  }
  for (auto buffer : host_buffers) {
    aclrtFreeHost(buffer);
  }
  hixl_engine.Finalize();
}

void ServerFinalize(Hixl &hixl_engine, const std::vector<MemHandle> handles,
                    const std::vector<void *> dev_buffers = {}) {
  for (auto handle : handles) {
    auto ret = hixl_engine.DeregisterMem(handle);
    if (ret != 0) {
      printf("[ERROR] DeregisterMem failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    } else {
      printf("[INFO] DeregisterMem success\n");
    }
  }
  for (auto buffer : dev_buffers) {
    aclrtFree(buffer);
  }
  hixl_engine.Finalize();
}

int32_t RunClient(const char *local_engine, const char *remote_engine) {
  printf("[INFO] client start\n");
  // 1. 初始化
  Hixl hixl_engine;
  if (Initialize(hixl_engine, local_engine) != 0) {
    printf("[ERROR] Initialize Hixl failed\n");
    return -1;
  }
  // 2. 注册内存地址
  int32_t *src = nullptr;
  CHECK_ACL(aclrtMallocHost(reinterpret_cast<void **>(&src), sizeof(int32_t)));
  bool connected = false;
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(src);
  desc.len = sizeof(int32_t);
  MemHandle handle = nullptr;
  auto ret = hixl_engine.RegisterMem(desc, MEM_HOST, handle);
  if (ret != SUCCESS) {
    printf("[ERROR] RegisterMem failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
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
  if (Transfer(hixl_engine, *src, remote_engine) != 0) {
    ClientFinalize(hixl_engine, connected, remote_engine, {handle}, {src});
    return -1;
  }

  // 5. 释放Cache与llmDataDist
  ClientFinalize(hixl_engine, connected, remote_engine, {handle}, {src});
  printf("[INFO] Finalize success\n");
  printf("[INFO] Prompt Sample end\n");
  return 0;
}

int32_t RunServer(const char *local_engine) {
  printf("[INFO] server start\n");
  // 1. 初始化
  Hixl hixl_engine;
  if (Initialize(hixl_engine, local_engine) != 0) {
    printf("[ERROR] Initialize Hixl failed\n");
    return -1;
  }
  // 2. 注册内存地址
  int32_t dst = 1;
  int32_t *buffer = nullptr;
  CHECK_ACL(aclrtMalloc((void **)&buffer, sizeof(int32_t), ACL_MEM_MALLOC_HUGE_ONLY));
  // init device buffer
  CHECK_ACL(aclrtMemcpy(buffer, sizeof(int32_t), &dst, sizeof(int32_t), ACL_MEMCPY_HOST_TO_DEVICE));

  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(buffer);
  desc.len = sizeof(int32_t);
  MemHandle handle = nullptr;
  auto ret = hixl_engine.RegisterMem(desc, MEM_DEVICE, handle);
  if (ret != SUCCESS) {
    printf("[ERROR] RegisterMem failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    ServerFinalize(hixl_engine, {handle}, {buffer});
    return -1;
  }
  // 3. RegisterMem成功后，将地址保存到本地文件中等待client读取
  printf("[INFO] RegisterMem success, dst addr:%p\n", buffer);
  std::ofstream tmp_file("./tmp");  // 默认就是 std::ios::out | std::ios::trunc
  if (tmp_file) {
    tmp_file << buffer << std::endl;
  }

  // 4. 等待client transfer
  std::this_thread::sleep_for(std::chrono::seconds(kWaitTransTime));

  CHECK_ACL(aclrtMemcpy(&dst, sizeof(int32_t), buffer, sizeof(int32_t), ACL_MEMCPY_DEVICE_TO_HOST));
  printf("[INFO] After transfer, dst value:%d\n", dst);

  // 5. 释放Cache与llmDataDist
  ServerFinalize(hixl_engine, {handle}, {buffer});
  printf("[INFO] Finalize success\n");
  printf("[INFO] server Sample end\n");
  return 0;
}

int main(int32_t argc, char **argv) {
  bool is_client = false;
  std::string device_id;
  std::string local_engine;
  std::string remote_engine;
  if (argc == kClientExpectedArgCnt) {
    is_client = true;
    device_id = argv[kArgIndexDeviceId];
    local_engine = argv[kArgIndexLocalEngine];
    remote_engine = argv[kArgIndexRemoteEngine];
    printf("[INFO] device_id = %s, local_engine = %s, remote_engine = %s\n", device_id.c_str(), local_engine.c_str(),
           remote_engine.c_str());
  } else if (argc == kServerExpectedArgCnt) {
    device_id = argv[kArgIndexDeviceId];
    local_engine = argv[kArgIndexLocalEngine];
    printf("[INFO] device_id = %s, local_engine = %s\n", device_id.c_str(), local_engine.c_str());
  } else {
    printf(
        "[ERROR] client expect 3 args(device_id, local_engine, remote_engine), "
        "server expect 2 args(device_id, local_engine), but got %d\n",
        argc - 1);
    return -1;
  }
  int32_t device = std::stoi(device_id);
  CHECK_ACL(aclrtSetDevice(device));

  int32_t ret = 0;
  if (is_client) {
    ret = RunClient(local_engine.c_str(), remote_engine.c_str());
  } else {
    ret = RunServer(local_engine.c_str());
  }
  CHECK_ACL(aclrtResetDevice(device));
  return ret;
}