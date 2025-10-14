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
#include <string.h>
#include "acl/acl.h"
#include "hixl/hixl.h"

using namespace hixl;
namespace {
constexpr int32_t kWaitTime = 5;
constexpr int32_t kExpectedArgCnt = 4;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalEngine = 2;
constexpr uint32_t kArgIndexRemoteEngine = 3;
constexpr uint32_t kMaxEngineNameLen = 30;

#define CHECK_ACL(x)                                                                  \
  do {                                                                                \
    aclError __ret = x;                                                               \
    if (__ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl; \
    }                                                                                 \
  } while (0)
}  // namespace

int Initialize(Hixl &hixl_engine, const char *local_engine) {
  std::map<AscendString, AscendString> options;
  auto ret = hixl_engine.Initialize(local_engine, options);
  if (ret != SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] Initialize success\n");
  return 0;
}

int Connect(Hixl &hixl_engine, const char *remote_engine) {
  auto ret = hixl_engine.Connect(remote_engine);
  if (ret != SUCCESS) {
    printf("[ERROR] Connect failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] Connect success\n");
  return 0;
}

int32_t Transfer(Hixl &hixl_engine, uint8_t *&buffer, uint8_t *&buffer2, const char *local_engine,
                 const char *remote_engine) {
  uintptr_t remote_addr;
  uintptr_t remote_addr2;
  std::ifstream(remote_engine) >> std::hex >> remote_addr >> remote_addr2;
  printf("[INFO] Get remote addr success, addr:%p, add2:%p\n", reinterpret_cast<void *>(remote_addr),
         reinterpret_cast<void *>(remote_addr2));
  if (std::string(local_engine) == std::min(std::string(local_engine), std::string(remote_engine))) {
    // init device buffer
    printf("[INFO] Local engine test write, write value:%s\n", local_engine);
    CHECK_ACL(aclrtMemcpy(buffer, kMaxEngineNameLen, local_engine, strlen(local_engine), ACL_MEMCPY_HOST_TO_DEVICE));
    TransferOpDesc desc{reinterpret_cast<uintptr_t>(buffer), remote_addr, strlen(local_engine)};
    auto ret = hixl_engine.TransferSync(remote_engine, WRITE, {desc});
    if (ret != SUCCESS) {
      printf("[ERROR] TransferSync write failed, remote_addr:%p, ret = %u\n", reinterpret_cast<void *>(remote_addr), ret);
      return -1;
    }
    printf("[INFO] TransferSync write success, remote_addr:%p, value:%s\n", reinterpret_cast<void *>(remote_addr),
           local_engine);

    // 等待对端读
    CHECK_ACL(aclrtMemcpy(buffer2, kMaxEngineNameLen, local_engine, strlen(local_engine), ACL_MEMCPY_HOST_TO_DEVICE));
    std::this_thread::sleep_for(std::chrono::seconds(kWaitTime));
  } else {
    // 等待对端写内存完成
    printf("[INFO] Local engine test read, expect read value:%s\n", remote_engine);
    std::this_thread::sleep_for(std::chrono::seconds(kWaitTime));
    char value[kMaxEngineNameLen] = {};
    CHECK_ACL(aclrtMemcpy(value, kMaxEngineNameLen, buffer, strlen(remote_engine), ACL_MEMCPY_DEVICE_TO_HOST));
    printf("[INFO] Wait peer TransferSync write end, remote_addr:%p, value = %s\n", reinterpret_cast<void *>(remote_addr),
           value);
    if (std::string(remote_engine) != value) {
      printf("[ERROR] Failed to check peer write value:%s, expect:%s\n", value, remote_engine);
      return -1;
    } else {
      printf("[INFO] Check peer write value success\n");
    }

    TransferOpDesc desc{reinterpret_cast<uintptr_t>(buffer2), remote_addr2, strlen(remote_engine)};
    auto ret = hixl_engine.TransferSync(remote_engine, READ, {desc});
    if (ret != SUCCESS) {
      printf("[ERROR] TransferSync read failed, remote_addr:%p, ret = %u\n", reinterpret_cast<void *>(remote_addr2), ret);
      return -1;
    }

    char value2[kMaxEngineNameLen] = {};
    CHECK_ACL(aclrtMemcpy(value2, kMaxEngineNameLen, buffer2, strlen(remote_engine), ACL_MEMCPY_DEVICE_TO_HOST));
    printf("[INFO] TransferSync read success, remote_addr:%p, value = %s\n", reinterpret_cast<void *>(remote_addr2),
           value2);
    if (std::string(remote_engine) != value2) {
      printf("[ERROR] Failed to check read value:%s, expect:%s\n", value, remote_engine);
      return -1;
    } else {
      printf("[INFO] Check read value success\n");
    }
  }
  return 0;
}

void Finalize(Hixl &hixl_engine, bool connected, const char *remote_engine, const std::vector<MemHandle> handles) {
  if (connected) {
    auto ret = hixl_engine.Disconnect(remote_engine);
    if (ret != 0) {
      printf("[ERROR] Disconnect failed, ret = %d\n", ret);
    } else {
      printf("[INFO] Disconnect success\n");
    }
    // 等待对端写disconnect完成, 销毁本地链路后进行解注册
    std::this_thread::sleep_for(std::chrono::seconds(kWaitTime));
  }

  for (auto handle : handles) {
    auto ret = hixl_engine.DeregisterMem(handle);
    if (ret != 0) {
      printf("[ERROR] DeregisterMem failed, ret = %u\n", ret);
    } else {
      printf("[INFO] DeregisterMem success\n");
    }
  }
  hixl_engine.Finalize();
}

int32_t Run(const char *local_engine, const char *remote_engine) {
  printf("[INFO] run start\n");
  // 1. 初始化
  Hixl hixl_engine;
  if (Initialize(hixl_engine, local_engine) != 0) {
    printf("[ERROR] Initialize Hixl failed\n");
    return -1;
  }
  // 2. 注册内存地址
  uint8_t *buffer = nullptr;  // 用于write
  CHECK_ACL(aclrtMalloc((void **)&buffer, kMaxEngineNameLen, ACL_MEM_MALLOC_HUGE_ONLY));
  uint8_t *buffer2 = nullptr;  // 用于read
  CHECK_ACL(aclrtMalloc((void **)&buffer2, kMaxEngineNameLen, ACL_MEM_MALLOC_HUGE_ONLY));

  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(buffer);
  desc.len = kMaxEngineNameLen;
  MemHandle handle = nullptr;
  bool connected = false;
  auto ret = hixl_engine.RegisterMem(desc, MEM_DEVICE, handle);
  if (ret != SUCCESS) {
    printf("[ERROR] RegisterMem failed, ret = %u\n", ret);
    Finalize(hixl_engine, connected, remote_engine, {});
    return -1;
  }
  MemHandle handle2 = nullptr;
  desc.addr = reinterpret_cast<uintptr_t>(buffer2);
  ret = hixl_engine.RegisterMem(desc, MEM_DEVICE, handle2);
  if (ret != SUCCESS) {
    printf("[ERROR] RegisterMem failed, ret = %u\n", ret);
    Finalize(hixl_engine, connected, remote_engine, {handle});
    return -1;
  }
  // RegisterMem成功后，将地址保存到本地文件中等待对端读取
  printf("[INFO] RegisterMem success, addr:%p, add2:%p\n", buffer, buffer2);
  std::ofstream tmp_file(local_engine);  // 默认就是 std::ios::out | std::ios::trunc
  if (tmp_file) {
    tmp_file << reinterpret_cast<void *>(buffer) << " " << reinterpret_cast<void *>(buffer2) << std::endl;
  }

  // 等待对端server注册完成
  std::this_thread::sleep_for(std::chrono::seconds(kWaitTime));

  // 3. 与对端server建链
  if (Connect(hixl_engine, remote_engine) != 0) {
    Finalize(hixl_engine, connected, remote_engine, {handle, handle2});
    return -1;
  }
  connected = true;

  // 4. 测试d2d write和read
  if (Transfer(hixl_engine, buffer, buffer2, local_engine, remote_engine) != 0) {
    Finalize(hixl_engine, connected, remote_engine, {handle, handle2});
    return -1;
  }

  // 5. 释放Cache与llmDataDist
  Finalize(hixl_engine, connected, remote_engine, {handle, handle2});
  printf("[INFO] run Sample end\n");
  return 0;
}

int main(int32_t argc, char **argv) {
  std::string device_id;
  std::string local_engine;
  std::string remote_engine;
  if (argc == kExpectedArgCnt) {
    device_id = argv[kArgIndexDeviceId];
    local_engine = argv[kArgIndexLocalEngine];
    remote_engine = argv[kArgIndexRemoteEngine];
    printf("[INFO] device_id = %s, local_engine = %s, remote_engine = %s\n", device_id.c_str(), local_engine.c_str(),
           remote_engine.c_str());
  } else {
    printf("[ERROR] expect 3 args(device_id, local_engine, remote_engine), but got %d\n", argc - 1);
    return -1;
  }
  int32_t device = std::stoi(device_id);
  CHECK_ACL(aclrtSetDevice(device));
  int32_t ret = Run(local_engine.c_str(), remote_engine.c_str());
  CHECK_ACL(aclrtResetDevice(device));
  return ret;
}