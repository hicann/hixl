/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <numeric>
#include <cstdio>
#include <thread>
#include <iostream>
#include <fstream>
#include <string.h>
#include <vector>
#include "acl/acl.h"
#include "securec.h"
#include "hixl/hixl.h"

using namespace hixl;
namespace {
constexpr int32_t kExpectedArgCnt = 4;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalEngine = 2;
constexpr uint32_t kArgIndexRemoteEngine = 3;
constexpr size_t kMemSize = 2 * 1024UL * 1024UL;
constexpr size_t kWriteSize = 1024UL * 1024UL;
constexpr size_t kTimeoutInMillis = 60000;

#define CHECK_ACL(x)                                                                  \
  do {                                                                                \
    aclError __ret = x;                                                               \
    if (__ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl; \
      return -1;                                                                      \
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
  options[OPTION_ENABLE_USE_FABRIC_MEM] = "1";
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

bool FileExists(const std::string &name) {
  return std::ifstream(name).good();
}

bool WaitFile(const std::string &name) {
  auto start = std::chrono::steady_clock::now();
  while (!FileExists(name)) {
    uint64_t time_cost =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    if (time_cost > kTimeoutInMillis) {
      printf("[ERROR] Wait file:%s timeout.\n", name.c_str());
      return false;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  return true;
}

int32_t Transfer(Hixl &hixl_engine, void *va, const char *local_engine,
                 const char *remote_engine, uint32_t &remote_dev_id) {
  if (!WaitFile(remote_engine)) {
    return -1;
  }
  uintptr_t remote_addr;
  std::ifstream(remote_engine) >> remote_addr >> remote_dev_id;

  printf("[INFO] Local engine test write, remote engine:%s\n", remote_engine);
  // Write local first 512K to remote last 512K
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(va), reinterpret_cast<uintptr_t>(remote_addr) + kWriteSize, kWriteSize};
  auto ret = hixl_engine.TransferSync(remote_engine, WRITE, {desc});
  if (ret != SUCCESS) {
    printf("[ERROR] TransferSync write failed, remote_addr:%lu, ret = %u, errmsg: %s\n", remote_addr, ret,
           GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] TransferSync write success, remote_addr:%lu, value:%s\n", remote_addr,
         local_engine);
  return 0;
}

void Finalize(Hixl &hixl_engine, bool connected, const char *remote_engine, const std::vector<MemHandle> handles) {
  if (connected) {
    auto ret = hixl_engine.Disconnect(remote_engine);
    if (ret != 0) {
      printf("[ERROR] Disconnect failed, ret = %d, errmsg: %s\n", ret, GetRecentErrMsg());
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
  hixl_engine.Finalize();
}

int AllocateBuffer(int32_t device_id, size_t mem_size, void *&va, aclrtDrvMemHandle &pa_handle) {
  CHECK_ACL(aclrtReserveMemAddress(&va, mem_size, 0, nullptr, 1));
  aclrtPhysicalMemProp prop{};
  prop.handleType = ACL_MEM_HANDLE_TYPE_NONE;
  prop.allocationType = ACL_MEM_ALLOCATION_TYPE_PINNED;
  prop.reserve = 0;
  prop.memAttr = ACL_HBM_MEM_HUGE;
  prop.location.type = ACL_MEM_LOCATION_TYPE_DEVICE;
  prop.location.id = device_id;
  CHECK_ACL(aclrtMallocPhysical(&pa_handle, mem_size, &prop, 0));
  CHECK_ACL(aclrtMapMem(va, mem_size, 0, pa_handle, 0));
  void *host_data;
  CHECK_ACL(aclrtMallocHost(&host_data, mem_size));
  memset_s(host_data, mem_size, device_id, mem_size);
  CHECK_ACL(aclrtMemcpy(va, kMemSize, host_data, mem_size, ACL_MEMCPY_HOST_TO_DEVICE));
  CHECK_ACL(aclrtFreeHost(host_data));
  return 0;
}

int VerifyBuffer(void *va, int32_t expected_val) {
  void *host_data;
  CHECK_ACL(aclrtMallocHost(&host_data, kWriteSize));
  // Check the second half of the buffer
  void *dev_ptr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(va) + kWriteSize);
  CHECK_ACL(aclrtMemcpy(host_data, kWriteSize, dev_ptr, kWriteSize, ACL_MEMCPY_DEVICE_TO_HOST));

  uint8_t *data = static_cast<uint8_t *>(host_data);
  uint8_t val = static_cast<uint8_t>(expected_val);
  for (size_t i = 0; i < kWriteSize; ++i) {
    if (data[i] != val) {
      printf("[ERROR] Verify failed, index:%zu, expected:%u, actual:%u\n", i, val, data[i]);
      CHECK_ACL(aclrtFreeHost(host_data));
      return -1;
    }
  }
  CHECK_ACL(aclrtFreeHost(host_data));
  printf("[INFO] Verify success, value:%u\n", val);
  return 0;
}

int32_t TransferAndVerify(Hixl &hixl_engine, void *va, const char *local_engine, const char *remote_engine,
                          MemHandle &handle) {
  bool connected = true;
  uint32_t remote_dev_id = 0;
  if (Transfer(hixl_engine, va, local_engine, remote_engine, remote_dev_id) != 0) {
    Finalize(hixl_engine, connected, remote_engine, {handle});
    return -1;
  }

  std::string local_done_file = std::string(local_engine) + ".done";
  std::ofstream done_file(local_done_file);
  if (done_file) {
    done_file << "done" << std::endl;
    done_file.close();
  }
  std::string remote_done_file = std::string(remote_engine) + ".done";
  printf("[INFO] Wait for remote write done signal:%s\n", remote_done_file.c_str());
  if (!WaitFile(remote_done_file)) {
    printf("[ERROR] Wait remote write done failed.\n");
    Finalize(hixl_engine, connected, remote_engine, {handle});
    return -1;
  }

  if (VerifyBuffer(va, remote_dev_id) != 0) {
    Finalize(hixl_engine, connected, remote_engine, {handle});
    return -1;
  }
  Finalize(hixl_engine, connected, remote_engine, {handle});
  printf("[INFO] run Sample end\n");
  return 0;
}

int32_t Run(int32_t device_id, const char *local_engine, const char *remote_engine) {
  printf("[INFO] run start\n");
  Hixl hixl_engine;
  if (Initialize(hixl_engine, local_engine) != 0) {
    printf("[ERROR] Initialize Hixl failed\n");
    return -1;
  }
  void *va;
  aclrtDrvMemHandle pa_handle;
  if (AllocateBuffer(device_id, kMemSize, va, pa_handle) != 0) {
    printf("[ERROR] allocate failed, dev_id: %d\n", device_id);
    return -1;
  }
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(va);
  desc.len = kMemSize;
  MemHandle handle = nullptr;
  bool connected = false;
  auto ret = hixl_engine.RegisterMem(desc, MEM_DEVICE, handle);
  if (ret != SUCCESS) {
    printf("[ERROR] RegisterMem failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    Finalize(hixl_engine, connected, remote_engine, {});
    return -1;
  }
  printf("[INFO] RegisterMem success.\n");
  std::ofstream tmp_file(local_engine);
  if (tmp_file) {
    tmp_file << reinterpret_cast<uintptr_t>(va) << " " << device_id << std::endl;
    tmp_file.close();
  }
  std::string local_init_done_file = std::string(local_engine) + ".init_done";
  std::ofstream init_done_file(local_init_done_file);
  if (init_done_file) {
    init_done_file << "done" << std::endl;
    init_done_file.close();
  }
  std::string remote_init_done_file = std::string(remote_engine) + ".init_done";
  printf("[INFO] Wait for remote write done signal:%s\n", remote_init_done_file.c_str());
  if (!WaitFile(remote_init_done_file)) {
    printf("[ERROR] Wait remote write done failed.\n");
    Finalize(hixl_engine, connected, remote_engine, {handle});
    return -1;
  }
  if (Connect(hixl_engine, remote_engine) != 0) {
    Finalize(hixl_engine, connected, remote_engine, {handle});
    return -1;
  }
  return TransferAndVerify(hixl_engine, va, local_engine, remote_engine, handle);
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
  CHECK_ACL(aclInit(nullptr));
  int32_t device = std::stoi(device_id);
  CHECK_ACL(aclrtSetDevice(device));
  int32_t ret = Run(device, local_engine.c_str(), remote_engine.c_str());
  CHECK_ACL(aclrtResetDevice(device));
  aclFinalize();
  return ret;
}
